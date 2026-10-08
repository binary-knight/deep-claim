#include "sim/events.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/math.h"
#include "sim/random.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include <algorithm>
#include <cstdint>

namespace sim {

namespace {

const Fixed kMultMin = Fixed::fromRatio(1, 100);
const Fixed kMultMax = Fixed::fromInt(100);

Notice eventNotice(NoticeKind kind, EventId event) {
    Notice notice = makeNotice(kind, CompanyId(), PlotId());
    notice.event = event;
    return notice;
}

bool eventActive(const WorldState& state, EventId event) {
    return std::ranges::any_of(state.events, [&](const ActiveEvent& active) {
        return active.active != 0 && active.event == event;
    });
}

}  // namespace

Fixed eventMultiplier(const Content& content, const WorldState& state, EventEffectType type,
                      ResourceId resource) {
    Fixed product = Fixed::one();
    for (const ActiveEvent& active : state.events) {
        if (active.active == 0) {
            continue;
        }
        for (const EventEffect& effect : content.events[active.event.index()].effects) {
            if (effect.type != type) {
                continue;
            }
            if (type == EventEffectType::DemandMult && effect.resource != resource) {
                continue;
            }
            product = clampFixed(product * effect.value, kMultMin, kMultMax);
        }
    }
    return product;
}

void runEvents(const Content& content, WorldState& state, TickReport& report) {
    for (ActiveEvent& active : state.events) {
        if (active.active != 0 && active.endsDay <= state.day) {
            active.active = 0;
            report.notices.push_back(eventNotice(NoticeKind::EventEnded, active.event));
        }
    }
    const std::uint32_t year = state.day / kDaysPerYear + 1;
    for (std::uint32_t i = 0; i < content.events.size(); ++i) {
        const EventDef& def = content.events[i];
        const EventId id(i);
        // Roll for every event every day so the RNG stream advances the same
        // way regardless of what is active.
        const Fixed roll = uniformFixed(state.rng.event);
        if (year < def.minYear || eventActive(state, id) || roll >= def.dailyWeight) {
            continue;
        }
        ActiveEvent active;
        active.event = id;
        active.startedDay = state.day;
        active.endsDay = state.day + def.durationDays;
        state.events.push_back(active);
        report.notices.push_back(eventNotice(NoticeKind::EventStarted, id));
    }
}

}  // namespace sim
