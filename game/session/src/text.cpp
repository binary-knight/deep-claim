// The prose catalogue: template filling and the text of notices and
// rejections. Every sentence comes from data/strings.json (spec 002); this
// file only chooses keys and supplies the figures.
#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/view.h"

#include "game/format.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace game {

namespace {

std::string companyName(const sim::CompanyView& view, sim::CompanyId id) {
    if (!id.valid() || id.index() >= view.companies.size()) {
        return "?";
    }
    return view.companies[id.index()].name;
}

std::string resourceName(const sim::Content& content, sim::ResourceId id) {
    if (!id.valid() || id.index() >= content.resources.size()) {
        return "?";
    }
    return content.resources[id.index()].name;
}

std::string techName(const sim::Content& content, sim::TechId id) {
    if (!id.valid() || id.index() >= content.techs.size()) {
        return "?";
    }
    return content.techs[id.index()].name;
}

std::string eventName(const sim::Content& content, sim::EventId id) {
    if (!id.valid() || id.index() >= content.events.size()) {
        return "?";
    }
    return content.events[id.index()].name;
}

// Plots are numbered from 1 in every piece of text the player sees.
std::string plotName(sim::PlotId id) {
    return "plot #" + std::to_string(id.value() + 1);
}

std::string rejectionText(const sim::Content& content, const sim::Notice& n) {
    const bool hasAmount = n.amount != sim::Money::zero();
    const std::string reason = rejectReasonText(content, n.reason);
    if (n.plot.valid()) {
        return hasAmount ? text(content, "notice.command_rejected.plot_amount",
                                {{"plot", plotName(n.plot)},
                                 {"amount", formatMoney(n.amount)},
                                 {"reason", reason}})
                         : text(content, "notice.command_rejected.plot",
                                {{"plot", plotName(n.plot)}, {"reason", reason}});
    }
    if (n.resource.valid()) {
        return text(content, "notice.command_rejected.sale",
                    {{"resource", resourceName(content, n.resource)}, {"reason", reason}});
    }
    if (n.tech.valid()) {
        return text(content, "notice.command_rejected.research",
                    {{"tech", techName(content, n.tech)}, {"reason", reason}});
    }
    if (n.loan.valid()) {
        return hasAmount ? text(content, "notice.command_rejected.loan_amount",
                                {{"amount", formatMoney(n.amount)}, {"reason", reason}})
                         : text(content, "notice.command_rejected.loan", {{"reason", reason}});
    }
    return hasAmount ? text(content, "notice.command_rejected.amount",
                            {{"amount", formatMoney(n.amount)}, {"reason", reason}})
                     : text(content, "notice.command_rejected", {{"reason", reason}});
}

}  // namespace

std::string fill(std::string_view tmpl, TextArgs args) {
    std::string out;
    out.reserve(tmpl.size() + 32);
    std::size_t pos = 0;
    while (pos < tmpl.size()) {
        const std::size_t open = tmpl.find('{', pos);
        if (open == std::string_view::npos) {
            out.append(tmpl.substr(pos));
            break;
        }
        const std::size_t close = tmpl.find('}', open + 1);
        if (close == std::string_view::npos) {
            out.append(tmpl.substr(pos));
            break;
        }
        out.append(tmpl.substr(pos, open - pos));
        const std::string_view name = tmpl.substr(open + 1, close - open - 1);
        bool replaced = false;
        for (const TextArg& arg : args) {
            if (arg.first == name) {
                out.append(arg.second);
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            out.append(tmpl.substr(open, close - open + 1));  // left visible on purpose
        }
        pos = close + 1;
    }
    return out;
}

std::string text(const sim::Content& content, std::string_view key) {
    const auto it = content.strings.find(key);  // no temporary string: std::less<>
    if (it == content.strings.end()) {
        return "[" + std::string(key) + "]";
    }
    return it->second;
}

std::string text(const sim::Content& content, std::string_view key, TextArgs args) {
    return fill(text(content, key), args);
}

std::string textPlural(const sim::Content& content, std::string_view key, std::int64_t count,
                       TextArgs args) {
    return fill(text(content, std::string(key) + (count == 1 ? ".one" : ".many")), args);
}

std::string rejectReasonText(const sim::Content& content, sim::RejectReason reason) {
    return text(content, std::string("reject.") + sim::rejectReasonName(reason));
}

std::string describeNotice(const sim::Content& content, const sim::CompanyView& view,
                           const sim::Notice& n) {
    using K = sim::NoticeKind;
    if (n.kind == K::CommandRejected) {
        return rejectionText(content, n);
    }
    const std::string key = std::string("notice.") + sim::noticeKindName(n.kind);
    // Every placeholder a notice template may use; the loader limits each
    // key to the ones that make sense for it.
    return text(content, key,
                {{"plot", plotName(n.plot)},
                 {"company", companyName(view, n.company)},
                 {"resource", resourceName(content, n.resource)},
                 {"tech", techName(content, n.tech)},
                 {"event", eventName(content, n.event)},
                 {"amount", formatMoney(n.amount)},
                 {"tonnes", formatTonnes(n.tonnes)},
                 {"months", std::to_string(n.tonnes)},
                 {"days", std::to_string(n.tonnes)}});
}

}  // namespace game
