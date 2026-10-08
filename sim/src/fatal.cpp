#include "sim/fatal.h"

#include <cstdio>
#include <cstdlib>

namespace sim {

void fatal(const char* message) {
    std::fputs("sim fatal error: ", stderr);
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

}  // namespace sim
