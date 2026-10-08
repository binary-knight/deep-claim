#include "sim/tech.h"

#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/world_state.h"

#include <algorithm>
#include <cstdint>

namespace sim {

Fixed techMultiplier(const Content& content, const Company& company, TechEffectType type) {
    // Accumulate in content order, not completion order, so the result
    // depends only on the set of completed techs (rounding is order-sensitive).
    Fixed product = Fixed::one();
    for (std::uint32_t i = 0; i < content.techs.size(); ++i) {
        if (std::find(company.completedTechs.begin(), company.completedTechs.end(), TechId(i)) ==
            company.completedTechs.end()) {
            continue;
        }
        for (const TechEffect& effect : content.techs[i].effects) {
            if (effect.type == type) {
                product = product * effect.value;
            }
        }
    }
    return product;
}

bool resourceUnlocked(const Content& content, const Company& company, ResourceId resource) {
    const TechId required = content.resources[resource.index()].unlockedBy;
    if (!required.valid()) {
        return true;
    }
    return std::find(company.completedTechs.begin(), company.completedTechs.end(), required) !=
           company.completedTechs.end();
}

}  // namespace sim
