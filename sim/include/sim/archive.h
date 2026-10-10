#pragma once

#include "sim/hash.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace sim {

// Serialization visitor. Each serializable type provides
//     template <class Archive> void serialize(Archive& ar, T& value) { ar(a, b, c); }
// and the same function drives hashing, saving and loading, so the three can
// never disagree. Primitives are written little-endian at their natural
// width, bools as one byte, enums at their underlying width, and vectors and
// strings with a uint32 length prefix.
//
// Reading never throws: a truncated or corrupt stream sets a failure flag,
// further reads yield zero, and the caller checks failed() once at the end.

template <class T>
struct IsVector : std::false_type {};
template <class T, class A>
struct IsVector<std::vector<T, A>> : std::true_type {};

template <class T>
struct IsOptional : std::false_type {};
template <class T>
struct IsOptional<std::optional<T>> : std::true_type {};

template <class T>
struct IsVariant : std::false_type {};
template <class... Ts>
struct IsVariant<std::variant<Ts...>> : std::true_type {};

template <class Derived>
class ArchiveBase {
public:
    template <class... Ts>
    void operator()(Ts&... values) {
        (field(values), ...);
    }

    template <class T>
    void field(T& value) {
        if constexpr (std::is_same_v<T, bool>) {
            if constexpr (Derived::kReading) {
                std::uint8_t byte = 0;
                integral(byte);
                if (byte > 1) {
                    self().fail();
                }
                value = byte == 1;
            } else {
                std::uint8_t byte = value ? 1 : 0;
                integral(byte);
            }
        } else if constexpr (std::is_enum_v<T>) {
            using Underlying = std::underlying_type_t<T>;
            if constexpr (Derived::kReading) {
                Underlying underlying = 0;
                integral(underlying);
                value = static_cast<T>(underlying);
            } else {
                auto underlying = static_cast<Underlying>(value);
                integral(underlying);
            }
        } else if constexpr (std::is_integral_v<T>) {
            integral(value);
        } else if constexpr (IsVector<T>::value) {
            vector(value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            string(value);
        } else if constexpr (IsOptional<T>::value) {
            optional(value);
        } else {
            serialize(self(), value);
        }
    }

private:
    Derived& self() { return static_cast<Derived&>(*this); }

    template <class T>
    void integral(T& value) {
        using Unsigned = std::make_unsigned_t<T>;
        if constexpr (Derived::kReading) {
            value = static_cast<T>(static_cast<Unsigned>(self().take(sizeof(T))));
        } else {
            self().emit(static_cast<std::uint64_t>(static_cast<Unsigned>(value)), sizeof(T));
        }
    }

    // Smallest serialized size of one T, used to bound allocations when
    // reading. For aggregates it is measured once by serializing a default
    // constructed value, whose nested containers are empty. A variant's
    // default is its first alternative, which need not be its smallest, so
    // variants take the smallest alternative plus the tag.
    template <class T>
    static std::size_t minSerializedSize() {
        if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
            return sizeof(T);
        } else if constexpr (IsVariant<T>::value) {
            static const std::size_t kSize = minVariantSize(std::type_identity<T>{});
            return kSize;
        } else {
            static const std::size_t kSize = serializedSizeOfDefault<T>();
            return kSize;
        }
    }

    template <class T>
    static std::size_t serializedSizeOfDefault();

    // The tag is whatever the variant's serialize() writes ahead of the
    // alternative; it is measured on the default (first) alternative, so this
    // header does not need to know its width.
    template <class... Alternatives>
    static std::size_t minVariantSize(std::type_identity<std::variant<Alternatives...>> /*tag*/) {
        using Variant = std::variant<Alternatives...>;
        using First = std::variant_alternative_t<0, Variant>;
        const std::size_t tagSize =
            serializedSizeOfDefault<Variant>() - serializedSizeOfDefault<First>();
        return tagSize + std::min({minSerializedSize<Alternatives>()...});
    }

    template <class T>
    void vector(std::vector<T>& values) {
        auto count = static_cast<std::uint32_t>(values.size());
        integral(count);
        if constexpr (Derived::kReading) {
            if (!self().checkLength(count, minSerializedSize<T>())) {
                return;
            }
            const std::size_t bounded = std::min<std::size_t>(count, Derived::kMaxLength);
            if (!self().allocate(values, bounded)) {
                return;
            }
        }
        for (T& element : values) {
            field(element);
        }
    }

    void string(std::string& value) {
        auto count = static_cast<std::uint32_t>(value.size());
        integral(count);
        if constexpr (Derived::kReading) {
            if (!self().checkLength(count, 1)) {
                return;
            }
            const std::size_t bounded = std::min<std::size_t>(count, Derived::kMaxLength);
            if (!self().allocate(value, bounded)) {
                return;
            }
        }
        for (char& character : value) {
            integral(character);
        }
    }

    template <class T>
    void optional(std::optional<T>& value) {
        bool present = value.has_value();
        field(present);
        if constexpr (Derived::kReading) {
            if (present) {
                value.emplace();
                field(*value);
            } else {
                value.reset();
            }
        } else {
            if (present) {
                field(*value);
            }
        }
    }
};

class HashArchive : public ArchiveBase<HashArchive> {
public:
    static constexpr bool kReading = false;

    void emit(std::uint64_t bits, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            hasher_.addByte(static_cast<std::uint8_t>(bits >> (8U * i)));
        }
    }
    [[nodiscard]] std::uint64_t value() const { return hasher_.value(); }

private:
    Fnv1a64 hasher_;
};

class WriteArchive : public ArchiveBase<WriteArchive> {
public:
    static constexpr bool kReading = false;

    void emit(std::uint64_t bits, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            bytes_.push_back(static_cast<std::uint8_t>(bits >> (8U * i)));
        }
    }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    [[nodiscard]] std::vector<std::uint8_t> release() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

class ReadArchive : public ArchiveBase<ReadArchive> {
public:
    static constexpr bool kReading = true;

    explicit ReadArchive(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::uint64_t take(std::size_t size) {
        if (failed_ || remaining() < size) {
            failed_ = true;
            return 0;
        }
        std::uint64_t bits = 0;
        for (std::size_t i = 0; i < size; ++i) {
            bits |= static_cast<std::uint64_t>(bytes_[position_ + i]) << (8U * i);
        }
        position_ += size;
        return bits;
    }

    // Largest element count any one container may claim. Real saves hold a few
    // thousand entities; the constant bound also lets the compiler see that
    // the allocation below it is of sane size.
    static constexpr std::uint32_t kMaxLength = 1U << 26U;

    // A length prefix can never need more bytes than remain, so reject it
    // before allocating anything.
    [[nodiscard]] bool checkLength(std::uint32_t count, std::size_t minElementSize) {
        if (failed_ || count > kMaxLength ||
            static_cast<std::uint64_t>(count) * minElementSize > remaining()) {
            failed_ = true;
            return false;
        }
        return true;
    }

    // For serialize() overloads that detect an invariant violation while
    // reading, such as an impossible RNG state.
    void fail() { failed_ = true; }

    // Sized construction that converts allocation failure into the error flag
    // so reading never throws.
    template <class Container>
    [[nodiscard]] bool allocate(Container& container, std::size_t count) {
        try {
            container = Container(count, typename Container::value_type{});
        } catch (const std::bad_alloc&) {
            failed_ = true;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool failed() const { return failed_; }
    [[nodiscard]] std::size_t remaining() const { return bytes_.size() - position_; }
    [[nodiscard]] bool atEnd() const { return position_ == bytes_.size(); }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
    bool failed_ = false;
};

// serialize() is written once against mutable references. Hashing and
// writing never modify the value, so the const_cast below is sound.

template <class T>
[[nodiscard]] std::uint64_t hashOf(const T& value) {
    HashArchive archive;             // NOLINT(misc-const-correctness) operator() mutates
    archive(const_cast<T&>(value));  // NOLINT(cppcoreguidelines-pro-type-const-cast)
    return archive.value();
}

template <class T>
[[nodiscard]] std::vector<std::uint8_t> toBytes(const T& value) {
    WriteArchive archive;
    archive(const_cast<T&>(value));  // NOLINT(cppcoreguidelines-pro-type-const-cast)
    return archive.release();
}

template <class Derived>
template <class T>
std::size_t ArchiveBase<Derived>::serializedSizeOfDefault() {
    T value{};
    return toBytes(value).size();
}

// Returns false if the stream is truncated, corrupt, or has trailing bytes.
// On failure `out` is unspecified.
template <class T>
[[nodiscard]] bool fromBytes(std::span<const std::uint8_t> bytes, T& out) {
    ReadArchive archive(bytes);  // NOLINT(misc-const-correctness) operator() mutates
    archive(out);
    return !archive.failed() && archive.atEnd();
}

}  // namespace sim
