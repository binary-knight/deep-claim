#include "chart_element.h"

#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Vertex.h>
#include <algorithm>
#include <cmath>
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

CashProvider& cashProvider() {
    static CashProvider instance;
    return instance;
}

std::uint64_t& generation() {
    static std::uint64_t value = 1;
    return value;
}

// Design tokens (docs/design/tokens.css): accent #2f8cff, text-3 #8fa0ba.
// Colours are premultiplied, so alpha scales the channels too.
Rml::ColourbPremultiplied premultiplied(int r, int g, int b, int alpha) {
    return {static_cast<Rml::byte>(r * alpha / 255), static_cast<Rml::byte>(g * alpha / 255),
            static_cast<Rml::byte>(b * alpha / 255), static_cast<Rml::byte>(alpha)};
}
const Rml::ColourbPremultiplied kAccent = premultiplied(47, 140, 255, 255);
const Rml::ColourbPremultiplied kArea = premultiplied(47, 140, 255, 46);
const Rml::ColourbPremultiplied kBaseline = premultiplied(143, 160, 186, 150);

constexpr float kLineWidth = 1.5F;
constexpr float kDash = 3.0F;
constexpr float kMark = 3.0F;

// A quad from two triangles given four corners in order.
void addQuad(Rml::Mesh& mesh, Rml::Vector2f a, Rml::Vector2f b, Rml::Vector2f c, Rml::Vector2f d,
             Rml::ColourbPremultiplied colour) {
    const auto base = static_cast<int>(mesh.vertices.size());
    for (const Rml::Vector2f& p : {a, b, c, d}) {
        Rml::Vertex vertex;
        vertex.position = p;
        vertex.colour = colour;
        vertex.tex_coord = Rml::Vector2f(0.0F, 0.0F);
        mesh.vertices.push_back(vertex);
    }
    for (const int i : {0, 1, 2, 0, 2, 3}) {
        mesh.indices.push_back(base + i);
    }
}

// A stroked segment of width `w` between two points.
void addSegment(Rml::Mesh& mesh, Rml::Vector2f from, Rml::Vector2f to, float w,
                Rml::ColourbPremultiplied colour) {
    const Rml::Vector2f d = to - from;
    const float length = std::sqrt(d.x * d.x + d.y * d.y);
    if (length <= 0.0F) {
        return;
    }
    const Rml::Vector2f n(-d.y / length * w * 0.5F, d.x / length * w * 0.5F);
    addQuad(mesh, from + n, to + n, to - n, from - n, colour);
}

}  // namespace

PriceChartElement::PriceChartElement(const Rml::String& tagName) : Rml::Element(tagName) {}

void PriceChartElement::setProvider(ChartProvider newProvider) {
    provider() = std::move(newProvider);
    invalidateAll();
}

void PriceChartElement::setCashProvider(CashProvider newProvider) {
    cashProvider() = std::move(newProvider);
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
    if (changed.find("days") != changed.end()) {
        days_ = std::clamp(GetAttribute<int>("days", 90), 2, 100000);
        dirty_ = true;
    }
    if (changed.find("mode") != changed.end()) {
        lineMode_ = GetAttribute<Rml::String>("mode", "line") != "columns";
        dirty_ = true;
    }
    if (changed.find("series") != changed.end()) {
        cashSeries_ = GetAttribute<Rml::String>("series", "price") == "cash";
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
    body_ = Rml::Geometry();
    baseline_ = Rml::Geometry();
    mark_ = Rml::Geometry();
    ChartSeries series;
    if (cashSeries_) {
        if (!cashProvider()) {
            return;
        }
        series = cashProvider()(days_);
    } else {
        if (!provider()) {
            return;
        }
        series = provider()(resource_, days_);
    }
    const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Content);
    if (series.values.empty() || size.x <= 2.0F || size.y <= 2.0F ||
        GetRenderManager() == nullptr) {
        return;
    }
    if (lineMode_) {
        buildLine(series, size);
    } else {
        buildColumns(series, size);
    }
}

void PriceChartElement::buildColumns(const ChartSeries& series, Rml::Vector2f size) {
    Rml::RenderManager* renderManager = GetRenderManager();
    std::int64_t high = series.hasBaseline ? series.baseline : 0;
    for (const std::int64_t value : series.values) {
        high = std::max(high, value);
    }
    // Always include zero so column heights read as absolute prices.
    const std::int64_t low = 0;
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
                                         Rml::Vector2f(columnWidth, h), kAccent);
    }
    body_ = renderManager->MakeGeometry(std::move(columns));
    if (series.hasBaseline) {
        Rml::Mesh line;
        const float y = size.y - heightOf(series.baseline);
        Rml::MeshUtilities::GenerateQuad(line, Rml::Vector2f(0.0F, y - 0.5F),
                                         Rml::Vector2f(size.x, 1.0F), kBaseline);
        baseline_ = renderManager->MakeGeometry(std::move(line));
    }
}

void PriceChartElement::buildLine(const ChartSeries& series, Rml::Vector2f size) {
    Rml::RenderManager* renderManager = GetRenderManager();
    // Scale to the data range (and the base line) so the trend is readable
    // at sparkline size; leave a little headroom top and bottom.
    std::int64_t high = series.values.front();
    std::int64_t low = series.values.front();
    for (const std::int64_t value : series.values) {
        high = std::max(high, value);
        low = std::min(low, value);
    }
    if (series.hasBaseline) {
        high = std::max(high, series.baseline);
        low = std::min(low, series.baseline);
    }
    if (high <= low) {
        high = low + 1;
    }
    const float pad = std::min(3.0F, size.y * 0.15F);
    const auto yOf = [&](std::int64_t value) {
        const auto fraction =
            static_cast<float>(static_cast<double>(value - low) / static_cast<double>(high - low));
        return pad + (1.0F - std::clamp(fraction, 0.0F, 1.0F)) * (size.y - 2.0F * pad);
    };
    const auto count = series.values.size();
    const auto xOf = [&](std::size_t i) {
        return count == 1 ? size.x * 0.5F
                          : static_cast<float>(i) * (size.x - 1.0F) / static_cast<float>(count - 1);
    };

    Rml::Mesh body;
    // Faint area under the line, then the line itself.
    for (std::size_t i = 1; i < count; ++i) {
        const Rml::Vector2f a(xOf(i - 1), yOf(series.values[i - 1]));
        const Rml::Vector2f b(xOf(i), yOf(series.values[i]));
        addQuad(body, a, b, Rml::Vector2f(b.x, size.y), Rml::Vector2f(a.x, size.y), kArea);
    }
    for (std::size_t i = 1; i < count; ++i) {
        addSegment(body, Rml::Vector2f(xOf(i - 1), yOf(series.values[i - 1])),
                   Rml::Vector2f(xOf(i), yOf(series.values[i])), kLineWidth, kAccent);
    }
    body_ = renderManager->MakeGeometry(std::move(body));

    if (series.hasBaseline) {
        Rml::Mesh dashes;
        const float y = yOf(series.baseline);
        for (float x = 0.0F; x < size.x; x += 2.0F * kDash) {
            const float w = std::min(kDash, size.x - x);
            Rml::MeshUtilities::GenerateQuad(dashes, Rml::Vector2f(x, y - 0.5F),
                                             Rml::Vector2f(w, 1.0F), kBaseline);
        }
        baseline_ = renderManager->MakeGeometry(std::move(dashes));
    }

    Rml::Mesh mark;
    const Rml::Vector2f last(xOf(count - 1), yOf(series.values.back()));
    Rml::MeshUtilities::GenerateQuad(mark, last - Rml::Vector2f(kMark * 0.5F, kMark * 0.5F),
                                     Rml::Vector2f(kMark, kMark), kAccent);
    mark_ = renderManager->MakeGeometry(std::move(mark));
}

void PriceChartElement::OnRender() {
    if (dirty_ || builtGeneration_ != generation()) {
        rebuild();
    }
    const Rml::Vector2f origin = GetAbsoluteOffset(Rml::BoxArea::Content);
    if (body_) {
        body_.Render(origin);
    }
    if (baseline_) {
        baseline_.Render(origin);
    }
    if (mark_) {
        mark_.Render(origin);
    }
}

void registerChartElement() {
    static Rml::ElementInstancerGeneric<PriceChartElement> instancer;
    Rml::Factory::RegisterElementInstancer("price-chart", &instancer);
}

}  // namespace game
