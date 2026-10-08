#pragma once

namespace sim {

// Reports an unrecoverable invariant violation and aborts in every build
// type. A loud crash is easier to find than a silently corrupted balance.
[[noreturn]] void fatal(const char* message);

}  // namespace sim
