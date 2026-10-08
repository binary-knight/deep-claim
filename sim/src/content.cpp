#include "sim/content.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace sim {

namespace {

template <class Id, class T>
Id findById(const std::vector<T>& items, std::string_view id) {
    for (std::uint32_t i = 0; i < items.size(); ++i) {
        if (items[i].id == id) {
            return Id(i);
        }
    }
    return Id();
}

}  // namespace

ResourceId Content::findResource(std::string_view id) const {
    return findById<ResourceId>(resources, id);
}
TechId Content::findTech(std::string_view id) const {
    return findById<TechId>(techs, id);
}
RegionId Content::findRegion(std::string_view id) const {
    return findById<RegionId>(regions, id);
}
EventId Content::findEvent(std::string_view id) const {
    return findById<EventId>(events, id);
}

}  // namespace sim
