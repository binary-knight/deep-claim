#pragma once

// <price-chart resource="0"/>: a column chart of a resource's recent prices,
// drawn with RmlUi geometry. Data comes from a provider the app installs, so
// the element stays decoupled from the session type.

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
};

using ChartProvider = std::function<ChartSeries(int resourceIndex)>;

class PriceChartElement final : public Rml::Element {
public:
    explicit PriceChartElement(const Rml::String& tagName);

    // Called by the app whenever prices may have changed.
    static void setProvider(ChartProvider provider);
    static void invalidateAll();

protected:
    void OnRender() override;
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
    void OnResize() override;

private:
    void rebuild();

    int resource_ = 0;
    bool dirty_ = true;
    std::uint64_t builtGeneration_ = 0;
    Rml::Geometry columns_;
    Rml::Geometry baseline_;
};

void registerChartElement();

}  // namespace game
