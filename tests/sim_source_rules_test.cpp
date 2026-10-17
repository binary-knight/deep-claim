// Machine-checks the determinism rules R-005, R-006 and R-007 (ARCHITECTURE.md):
// no file under sim/ may use unordered containers, std::hash or <chrono>, because
// their behaviour differs between standard libraries or reads the wall clock.
// Comments are stripped first, so a warning such as "Never use std::hash" passes.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Removes // line comments and /* */ block comments. Every newline is kept,
// including those inside block comments, so line numbers stay true. String
// literals get no special treatment; no sim source puts a comment marker in one.
std::string stripComments(std::string_view source) {
    std::string out;
    out.reserve(source.size());
    std::size_t i = 0;
    while (i < source.size()) {
        if (source.compare(i, 2, "//") == 0) {
            i = std::min(source.find('\n', i), source.size());
        } else if (source.compare(i, 2, "/*") == 0) {
            const std::size_t close = source.find("*/", i + 2);
            const std::size_t end = close == std::string_view::npos ? source.size() : close + 2;
            const auto body = source.substr(i, end - i);
            out.append(static_cast<std::size_t>(std::count(body.begin(), body.end(), '\n')), '\n');
            out.push_back(' ');
            i = end;
        } else {
            out.push_back(source[i]);
            ++i;
        }
    }
    return out;
}

// One-based line numbers of each occurrence of token in text.
std::vector<int> violationLines(std::string_view text, std::string_view token) {
    std::vector<int> lines;
    std::size_t pos = text.find(token);
    while (pos != std::string_view::npos) {
        const auto before = text.substr(0, pos);
        lines.push_back(static_cast<int>(std::count(before.begin(), before.end(), '\n')) + 1);
        pos = text.find(token, pos + token.size());
    }
    return lines;
}

bool isUnderMeta(const fs::path& path) {
    return std::any_of(path.begin(), path.end(),
                       [](const fs::path& part) { return part == ".meta"; });
}

// Every .h and .cpp file under sim/include and sim/src, sorted.
std::vector<fs::path> simSourceFiles() {
    const fs::path root(DC_SOURCE_DIR);
    std::vector<fs::path> files;
    for (const char* dir : {"sim/include", "sim/src"}) {
        for (const auto& entry : fs::recursive_directory_iterator(root / dir)) {
            const fs::path& path = entry.path();
            const auto ext = path.extension();
            if (entry.is_regular_file() && (ext == ".h" || ext == ".cpp") && !isUnderMeta(path)) {
                files.push_back(path);
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// "<path relative to the repo>:<line>: <token>" for each match in the sim tree.
std::vector<std::string> simViolations(std::string_view token) {
    const auto files = simSourceFiles();
    REQUIRE(!files.empty());
    const fs::path root(DC_SOURCE_DIR);
    std::vector<std::string> found;
    for (const auto& path : files) {
        const std::string relative = path.lexically_relative(root).generic_string();
        for (const int line : violationLines(stripComments(readFile(path)), token)) {
            found.push_back(relative + ":" + std::to_string(line) + ": " + std::string(token));
        }
    }
    return found;
}

}  // namespace

TEST_CASE("source scanner finds a token and its line in code", "[sim][rules]") {
    const std::string source = "#include <map>\n\nstd::unordered_map<int, int> m;\n";
    CHECK(violationLines(stripComments(source), "unordered_") == std::vector<int>{3});
}

TEST_CASE("source scanner reports every occurrence of a token", "[sim][rules]") {
    const std::string source = "std::hash<int> a;\nint b;\nstd::hash<long> c;\n";
    CHECK(violationLines(stripComments(source), "std::hash") == std::vector<int>{1, 3});
}

TEST_CASE("source scanner ignores a token in a line comment", "[sim][rules]") {
    const std::string source = "// Never use std::hash for this purpose.\nint x;\n";
    CHECK(violationLines(stripComments(source), "std::hash").empty());
}

TEST_CASE("source scanner ignores a token in a block comment and keeps line numbers",
          "[sim][rules]") {
    const std::string source = "/* not <chrono>\n   still a comment */\n#include <chrono>\n";
    CHECK(violationLines(stripComments(source), "<chrono>") == std::vector<int>{3});
}

TEST_CASE("source scanner resumes code after a block comment on the same line", "[sim][rules]") {
    const std::string source = "int a; /* std::hash */ std::hash<int> h; // std::hash\n";
    CHECK(violationLines(stripComments(source), "std::hash") == std::vector<int>{1});
}

TEST_CASE("sim sources never contain unordered_ (R-005)", "[sim][rules]") {
    CHECK(simViolations("unordered_") == std::vector<std::string>{});
}

TEST_CASE("sim sources never contain std::hash (R-006)", "[sim][rules]") {
    CHECK(simViolations("std::hash") == std::vector<std::string>{});
}

TEST_CASE("sim sources never contain <chrono> (R-007)", "[sim][rules]") {
    CHECK(simViolations("<chrono>") == std::vector<std::string>{});
}
