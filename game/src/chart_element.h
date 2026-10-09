#pragma once

// <price-chart resource="0" days="90" mode="line"/>: a chart of a series
// drawn with RmlUi geometry. Attributes:
//   resource  index of the resource whose daily prices are drawn
//   series    "price" (default) or "cash" (the player's cash, header sparkline)
//   days      how many trailing days (default 90)
//   mode      "line" (default): sparkline with a faint area, a dashed base
//             line and an endpoint mark, scaled to the data range;
//             "columns": absolute columns from zero (the old market chart)
// Data comes from providers the app installs, so the element stays
// decoupled from the session type.

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Geometry.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace game {

struct ChartSeries {
    std::vector<std::int64_t> values;  // oldest first
    std::int64_t baseline = 0;         // drawn as a reference line (base price)
    bool hasBaseline = true;
};

using ChartProvider = std::function<ChartSeries(int resourceIndex, int days)>;
using CashProvider = std::function<ChartSeries(int days)>;

class PriceChartElement final : public Rml::Element {
public:
    explicit PriceChartElement(const Rml::String& tagName);

    // Called by the app whenever prices may have changed.
    static void setProvider(ChartProvider provider);
    static void setCashProvider(CashProvider provider);
    static void invalidateAll();

protected:
    void OnRender() override;
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
    void OnResize() override;

private:
    void rebuild();
    void buildColumns(const ChartSeries& series, Rml::Vector2f size);
    void buildLine(const ChartSeries& series, Rml::Vector2f size);

    int resource_ = 0;
    int days_ = 90;
    bool lineMode_ = true;
    bool cashSeries_ = false;
    bool dirty_ = true;
    std::uint64_t builtGeneration_ = 0;
    Rml::Geometry body_;
    Rml::Geometry baseline_;
    Rml::Geometry mark_;
};

void registerChartElement();

}  // namespace game
