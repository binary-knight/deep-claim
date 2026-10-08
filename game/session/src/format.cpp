#include "game/format.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/money.h"
#include "sim/types.h"
#include "sim/view.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>

namespace game {

namespace {

std::string withThousands(std::int64_t value) {
    const bool negative = value < 0;
    const std::string digits = std::to_string(negative ? -value : value);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (digits.size() - i) % 3 == 0) {
            out += ',';
        }
        out += digits[i];
    }
    return negative ? "-" + out : out;
}

std::string companyName(const sim::CompanyView& view, sim::CompanyId id) {
    if (!id.valid() || id.index() >= view.companies.size()) {
        return "someone";
    }
    return view.companies[id.index()].name;
}

std::string resourceName(const sim::Content& content, sim::ResourceId id) {
    return id.valid() && id.index() < content.resources.size() ? content.resources[id.index()].name
                                                               : "ore";
}

std::string plotName(sim::PlotId id) {
    return std::format("plot #{}", id.value() + 1);
}

}  // namespace

std::string formatMoney(sim::Money amount) {
    const std::int64_t cents = amount.cents();
    const bool negative = cents < 0;
    const std::int64_t magnitude = negative ? -cents : cents;
    const std::string sign = negative ? "-" : "";
    if (magnitude >= 100'000'000'000LL) {  // >= $1B
        return std::format("{}${}.{:02}B", sign, magnitude / 100'000'000'000LL,
                           (magnitude / 1'000'000'000LL) % 100);
    }
    if (magnitude >= 100'000'000LL) {  // >= $1M
        return std::format("{}${}.{:02}M", sign, magnitude / 100'000'000LL,
                           (magnitude / 1'000'000LL) % 100);
    }
    if (magnitude >= 10'000'000LL) {  // >= $100k: whole thousands
        return std::format("{}${}k", sign, magnitude / 100'000LL);
    }
    if (magnitude >= 100'000LL) {  // >= $1k
        return std::format("{}${}.{}k", sign, magnitude / 100'000LL, (magnitude / 10'000LL) % 10);
    }
    return std::format("{}${}.{:02}", sign, magnitude / 100, magnitude % 100);
}

std::string formatDay(sim::Day day) {
    return std::format("Y{} M{} D{}", day / sim::kDaysPerYear + 1,
                       (day % sim::kDaysPerYear) / sim::kDaysPerMonth + 1,
                       day % sim::kDaysPerMonth + 1);
}

std::string formatTonnes(std::int64_t tonnes) {
    return withThousands(tonnes) + " t";
}

std::string formatRatio(sim::Fixed ratio) {
    const std::int64_t hundredths = ratio.raw() / (sim::Fixed::kScale / 100);
    return std::format("x{}.{:02}", hundredths / 100, std::llabs(hundredths % 100));
}

std::string describeNotice(const sim::Content& content, const sim::CompanyView& view,
                           const sim::Notice& n) {
    using K = sim::NoticeKind;
    const std::string who = companyName(view, n.company);
    switch (n.kind) {
        case K::CommandRejected:
            return std::format("Order rejected: {}", sim::rejectReasonName(n.reason));
        case K::PlotListed:
            return std::format("{} listed for auction, asking {}", plotName(n.plot),
                               formatMoney(n.amount));
        case K::PlotRelisted:
            return std::format("{} unsold, relisted at {}", plotName(n.plot),
                               formatMoney(n.amount));
        case K::PlotSold:
            return std::format("{} bought {} for {}", who, plotName(n.plot), formatMoney(n.amount));
        case K::AuctionLost:
            return std::format("Outbid on {}", plotName(n.plot));
        case K::BidForfeited:
            return std::format("Could not pay for {}; bid forfeited", plotName(n.plot));
        case K::SurveyStarted:
            return std::format("Survey of {} commissioned for {}", plotName(n.plot),
                               formatMoney(n.amount));
        case K::SurveyComplete:
            return std::format("Survey of {} complete", plotName(n.plot));
        case K::MineStarted:
            return std::format("{} started building a mine on {}", who, plotName(n.plot));
        case K::MineOperational:
            return std::format("Mine on {} is operational", plotName(n.plot));
        case K::MineDepleted:
            return std::format("Mine on {} is depleted", plotName(n.plot));
        case K::MineSwitched:
            return std::format("Mine on {} changed state", plotName(n.plot));
        case K::OreSold:
            return std::format("Sold {} of {} for {}", formatTonnes(n.tonnes),
                               resourceName(content, n.resource), formatMoney(n.amount));
        case K::StorageCharged:
            return std::format("Storage charged: {}", formatMoney(n.amount));
        case K::LoanTaken:
            return std::format("Loan taken: {}", formatMoney(n.amount));
        case K::LoanPayment:
            return std::format("Loan payment: {}", formatMoney(n.amount));
        case K::LoanRepaid:
            return "Loan fully repaid";
        case K::InsolvencyWarning:
            return std::format("Insolvent: {} months before bankruptcy", n.tonnes);
        case K::Bankrupt:
            return std::format("{} is bankrupt", who);
        case K::ResearchStarted:
            return std::format("Research started: {}",
                               n.tech.valid() ? content.techs[n.tech.index()].name : "tech");
        case K::ResearchComplete:
            return std::format("Research complete: {}",
                               n.tech.valid() ? content.techs[n.tech.index()].name : "tech");
        case K::EventStarted:
            return std::format("Event: {}",
                               n.event.valid() ? content.events[n.event.index()].name : "event");
        case K::EventEnded:
            return std::format("Event over: {}",
                               n.event.valid() ? content.events[n.event.index()].name : "event");
    }
    return sim::noticeKindName(n.kind);
}

}  // namespace game
