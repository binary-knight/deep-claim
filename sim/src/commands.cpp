#include "sim/commands.h"

#include <variant>

namespace sim {

CompanyId commandCompany(const Command& command) {
    return std::visit([](const auto& concrete) { return concrete.company; }, command);
}

}  // namespace sim
