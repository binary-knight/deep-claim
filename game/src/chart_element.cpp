#include "chart_element.h"

#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/Types.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>

namespace game {

namespace {

ChartProvider& provider() {
    static ChartProvider instance;
    return instance;
}

std::uint64_t& generation() {
    static std::uint64_t value = 1;
    return value;
}

const Rml::ColourbPremultiplied kColumnColour(70, 140, 220, 255);
const Rml::ColourbPremultiplied kBaselineColour(220, 220, 220, 160);

}  // namespace

PriceChartElement::PriceChartElement(const Rml::String& tagName) : Rml::Element(tagName) {}

void PriceChartElement::setProvider(ChartProvider newProvider) {
    provider() = std::move(newProvider);
    invalidateAll();
}

void PriceChartElement::invalidateAll() {
    ++generation();
}

void PriceChartElement::OnAttributeChange(const Rml::ElementAttributes& changed) {
    Rml::Element::OnAttributeChange(changed);
    if (changed.find("resource") != changed.end()) {
        resource_ = GetAttribute<int>("resource", 0);
        dirty_ = true;
    }
}

void PriceChartElement::OnResize() {
    Rml::Element::OnResize();
    dirty_ = true;
}

void PriceChartElement::rebuild() {
    dirty_ = false;
    builtGeneration_ = generation();
    columns_ = Rml::Geometry();
    baseline_ = Rml::Geometry();
    Rml::RenderManager* renderManager = GetRenderManager();
    if (renderManager == nullptr || !provider()) {
        return;
    }
    const ChartSeries series = provider()(resource_);
    const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Content);
    if (series.values.empty() || size.x <= 2.0F || size.y <= 2.0F) {
        return;
    }
    std::int64_t high = series.baseline;
    std::int64_t low = series.baseline;
    for (const std::int64_t value : series.values) {
        high = std::max(high, value);
        low = std::min(low, value);
    }
    // Always include zero so column heights read as absolute prices.
    low = 0;
    if (high <= low) {
        high = low + 1;
    }
    const auto count = static_cast<float>(series.values.size());
    const float columnWidth = std::max(1.0F, size.x / count - 1.0F);
    const auto heightOf = [&](std::int64_t value) {
        const auto fraction =
            static_cast<float>(static_cast<double>(value - low) / static_cast<double>(high - low));
        return std::clamp(fraction, 0.0F, 1.0F) * size.y;
    };

    Rml::Mesh columns;
    for (std::size_t i = 0; i < series.values.size(); ++i) {
        const float x = static_cast<float>(i) * size.x / count;
        const float h = heightOf(series.values[i]);
        Rml::MeshUtilities::GenerateQuad(columns, Rml::Vector2f(x, size.y - h),
                                         Rml::Vector2f(columnWidth, h), kColumnColour);
    }
    columns_ = renderManager->MakeGeometry(std::move(columns));

    Rml::Mesh line;
    const float y = size.y - heightOf(series.baseline);
    Rml::MeshUtilities::GenerateQuad(line, Rml::Vector2f(0.0F, y - 0.5F),
                                     Rml::Vector2f(size.x, 1.0F), kBaselineColour);
    baseline_ = renderManager->MakeGeometry(std::move(line));
}

void PriceChartElement::OnRender() {
    if (dirty_ || builtGeneration_ != generation()) {
        rebuild();
    }
    const Rml::Vector2f origin = GetAbsoluteOffset(Rml::BoxArea::Content);
    if (columns_) {
        columns_.Render(origin);
    }
    if (baseline_) {
        baseline_.Render(origin);
    }
}

void registerChartElement() {
    static Rml::ElementInstancerGeneric<PriceChartElement> instancer;
    Rml::Factory::RegisterElementInstancer("price-chart", &instancer);
}

}  // namespace game
