#include "sim/tech.h"

#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/world_state.h"

#include <algorithm>

namespace sim {

Fixed techMultiplier(const Content& content, const Company& company, TechEffectType type) {
    Fixed product = Fixed::one();
    for (const TechId tech : company.completedTechs) {
        for (const TechEffect& effect : content.techs[tech.index()].effects) {
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
