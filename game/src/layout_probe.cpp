// Headless layout probe: loads the real interface with a stub renderer and
// lays out every tab at the desktop frame (1366x820) and the Steam Deck
// frame (1280x800), so layout problems can be diagnosed without a display.
// Also a regression test (spec 003): exits non-zero when the content pane
// collapses, anything overflows its scroll pane horizontally, text in the
// deck frame falls below 14dp, RmlUi logs a warning or error (unknown RCSS
// property, unbound data variable ...), or the playtest trace has no header.
// Not part of the shipped game.
#include "app.h"
#include "chart_element.h"
#include "content/loader.h"
#include "platform/services.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

class StubRender final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> /*vertices*/,
                                                Rml::Span<const int> /*indices*/) override {
        return 1;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle /*geometry*/, Rml::Vector2f /*translation*/,
                        Rml::TextureHandle /*texture*/) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle /*geometry*/) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions,
                                   const Rml::String& /*source*/) override {
        dimensions = {144, 24};  // the icon sheet's size, so sprites resolve
        return 1;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> /*source*/,
                                       Rml::Vector2i /*dimensions*/) override {
        return 1;
    }
    void ReleaseTexture(Rml::TextureHandle /*texture*/) override {}
    void EnableScissorRegion(bool /*enable*/) override {}
    void SetScissorRegion(Rml::Rectanglei /*region*/) override {}
};

// Collects RmlUi's warnings and errors: a stylesheet or binding mistake is
// reported there and nowhere else.
class LoggingSystem final : public Rml::SystemInterface {
public:
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_WARNING ||
            type == Rml::Log::LT_ASSERT) {
            problems.push_back(message);
        }
        return Rml::SystemInterface::LogMessage(type, message);
    }
    std::vector<std::string> problems;
};

constexpr std::array<const char*, 6> kTabs = {"dashboard", "plots",   "market",
                                              "research",  "finance", "log"};
constexpr float kDeckWidth = 1300.0F;
constexpr float kMinDeckText = 14.0F;

struct Frame {
    int width;
    int height;
};
constexpr std::array<Frame, 2> kFrames = {{{1366, 820}, {1280, 800}}};

struct TabReport {
    float contentWidth = 0.0F;
    int elements = 0;
    int overflows = 0;
    float minText = 1000.0F;
    std::string minTextSample;
    std::vector<std::string> notes;
};

float rightEdge(Rml::Element* e) {
    return e->GetAbsoluteOffset(Rml::BoxArea::Border).x +
           e->GetBox().GetSize(Rml::BoxArea::Border).x;
}

std::string trimmed(const Rml::String& text) {
    std::string out;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            out += c;
        }
    }
    return out;
}

// Walks the visible tree. `clipRight` is the right edge of the nearest
// scrollable ancestor (or the window): anything past it would need a
// horizontal scrollbar, which the design never shows. Descendants of
// overflow-hidden elements (the rail, the company name) are meant to clip
// and are not checked.
void inspect(Rml::Element* e, float clipRight, TabReport& report) {
    if (e == nullptr || !e->IsVisible(true)) {
        return;
    }
    const Rml::String& tag = e->GetTagName();
    if (tag == "scrollbarvertical" || tag == "scrollbarhorizontal") {
        return;  // RmlUi's own widget, laid out in the padding box's margin
    }
    const Rml::Vector2f size = e->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0F && size.y <= 0.0F) {
        return;
    }
    ++report.elements;
    if (rightEdge(e) > clipRight + 0.5F) {
        ++report.overflows;
        if (report.notes.size() < 12) {
            report.notes.push_back(std::format(
                "overflow: <{} id={} class={}> right edge {:.0f} > {:.0f}", e->GetTagName(),
                e->GetId(), e->GetClassNames(), rightEdge(e), clipRight));
        }
    }
    const Rml::Style::Overflow overflowX = e->GetComputedValues().overflow_x();
    const Rml::Style::Overflow overflowY = e->GetComputedValues().overflow_y();
    float childClip = clipRight;
    if (overflowX == Rml::Style::Overflow::Hidden) {
        return;  // intentional clip
    }
    if (overflowX != Rml::Style::Overflow::Visible || overflowY != Rml::Style::Overflow::Visible) {
        // Scrollable: a vertical scrollbar takes its width from the inside.
        const float scrollbar = e->GetScrollHeight() > e->GetClientHeight() + 0.5F ? 12.0F : 0.0F;
        childClip = e->GetAbsoluteOffset(Rml::BoxArea::Padding).x +
                    e->GetBox().GetSize(Rml::BoxArea::Padding).x - scrollbar;
    }
    for (int i = 0; i < e->GetNumChildren(true); ++i) {
        Rml::Element* child = e->GetChild(i);
        if (auto* text = rmlui_dynamic_cast<Rml::ElementText*>(child)) {
            if (trimmed(text->GetText()).empty()) {
                continue;
            }
            const float fontSize = e->GetComputedValues().font_size();
            if (fontSize < report.minText) {
                report.minText = fontSize;
                report.minTextSample =
                    std::format("<{} id={} class={}> \"{}\"", e->GetTagName(), e->GetId(),
                                e->GetClassNames(), trimmed(text->GetText()).substr(0, 24));
            }
            continue;
        }
        inspect(child, childClip, report);
    }
}

void dump(Rml::Element* e, int depth, int maxDepth) {
    if (e == nullptr || depth > maxDepth || !e->IsVisible(true)) {
        return;
    }
    const Rml::Vector2f size = e->GetBox().GetSize(Rml::BoxArea::Border);
    const Rml::Vector2f pos = e->GetAbsoluteOffset(Rml::BoxArea::Border);
    std::cout << std::format("{:{}}<{} id={} class={}> at {:.0f},{:.0f} size {:.0f} x {:.0f}\n", "",
                             depth * 2, e->GetTagName(), e->GetId(), e->GetClassNames(), pos.x,
                             pos.y, size.x, size.y);
    for (int i = 0; i < e->GetNumChildren(); ++i) {
        dump(e->GetChild(i), depth + 1, maxDepth);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    const std::filesystem::path assets = args.size() > 1 ? args[1] : DC_ASSETS_DIR;
    const content::LoadResult loaded = content::loadFromDirectory(DC_DATA_DIR);
    if (!loaded.content) {
        return 1;
    }
    StubRender render;
    LoggingSystem system;
    Rml::SetRenderInterface(&render);
    Rml::SetSystemInterface(&system);
    Rml::Initialise();
    for (const char* face : {"Barlow-Regular.ttf", "Barlow-Medium.ttf", "Barlow-SemiBold.ttf",
                             "BarlowCondensed-SemiBold.ttf", "BarlowCondensed-Bold.ttf",
                             "JetBrainsMono-Regular.ttf", "JetBrainsMono-Medium.ttf"}) {
        Rml::LoadFontFace((assets / "fonts" / face).string());
    }
    game::registerChartElement();
    Rml::Context* context =
        Rml::CreateContext("main", Rml::Vector2i(kFrames[0].width, kFrames[0].height));
    auto services = platform::makeNullServices(std::filesystem::temp_directory_path(), "Probe");
    // The playtest trace is exercised too: the app writes one into a scratch
    // directory and it must start with a new_game record.
    const std::filesystem::path playtest =
        std::filesystem::temp_directory_path() / "deep_claim_probe_playtest";
    std::filesystem::remove_all(playtest);
    int exitCode = 0;
    {
        game::App app(*context, *services, *loaded.content, assets, playtest, "probe");
        if (!app.loadDocument()) {
            exitCode = 2;
        }
        std::ifstream trace(app.tracePath());
        std::string firstLine;
        std::getline(trace, firstLine);
        std::cout << std::format("trace: {}\n  {}\n", app.tracePath().string(), firstLine);
        if (firstLine.find(R"("t":"new_game")") == std::string::npos ||
            firstLine.find(R"("build":"probe")") == std::string::npos) {
            std::cout << "TRACE FAILURE: no new_game record\n";
            exitCode = 4;
        }
        // A few days of play so the log, strip and sparklines have data.
        app.update(0.0);
        for (const Frame& frame : kFrames) {
            const bool deck = static_cast<float>(frame.width) < kDeckWidth;
            context->SetDimensions(Rml::Vector2i(frame.width, frame.height));
            app.forceDeck(deck);
            std::cout << std::format("\n== frame {}x{}{} ==\n", frame.width, frame.height,
                                     deck ? " (deck)" : "");
            for (std::size_t tab = 0; tab < kTabs.size(); ++tab) {
                app.setTab(static_cast<int>(tab));
                context->Update();
                context->Update();  // data bindings settle on the second pass
                Rml::ElementDocument* doc = context->GetDocument(0);
                Rml::Element* content = doc->GetElementById("content");
                TabReport report;
                bool failed = false;
                if (content == nullptr || content->GetNumChildren() == 0) {
                    report.notes.emplace_back("content pane missing or empty");
                    failed = true;
                } else {
                    // The visible tab is the content pane's one visible child.
                    for (int i = 0; i < content->GetNumChildren(); ++i) {
                        Rml::Element* child = content->GetChild(i);
                        if (child->IsVisible(true)) {
                            report.contentWidth = child->GetBox().GetSize(Rml::BoxArea::Border).x;
                        }
                    }
                    if (report.contentWidth < 100.0F) {
                        report.notes.emplace_back("content width collapsed");
                        failed = true;
                    }
                }
                inspect(doc, static_cast<float>(frame.width), report);
                if (report.overflows > 0) {
                    failed = true;
                }
                if (deck && report.minText < kMinDeckText - 0.01F) {
                    report.notes.push_back(std::format("text below {:.0f}dp in the deck frame: {}",
                                                       kMinDeckText, report.minTextSample));
                    failed = true;
                }
                if (!system.problems.empty()) {
                    for (const std::string& problem : system.problems) {
                        report.notes.push_back("rmlui: " + problem);
                    }
                    system.problems.clear();
                    failed = true;
                }
                std::cout << std::format(
                    "{:<9} content {:4.0f}dp wide, {:4} elements, {} overflow, smallest text "
                    "{:.0f}dp{}\n",
                    kTabs.at(tab), report.contentWidth, report.elements, report.overflows,
                    report.minText, failed ? "  FAILED" : "");
                for (const std::string& note : report.notes) {
                    std::cout << "    " << note << '\n';
                }
                if (failed) {
                    exitCode = 3;
                    dump(content, 0, 5);
                }
            }
        }
        if (!system.problems.empty()) {
            for (const std::string& problem : system.problems) {
                std::cout << "rmlui: " << problem << '\n';
            }
            exitCode = 3;
        }
        std::cout << (exitCode == 0 ? "\nLAYOUT OK\n" : "\nLAYOUT FAILURE\n");
    }
    // The app, its document and data model are gone before RmlUi shuts down.
    Rml::Shutdown();
    return exitCode;
}
