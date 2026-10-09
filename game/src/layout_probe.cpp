// Headless layout probe: loads the real interface with a stub renderer and
// prints computed boxes, so layout problems can be diagnosed without a
// display. Also a regression test: exits non-zero if the content panel's
// first child collapses to zero width. Not part of the shipped game.
#include "app.h"
#include "chart_element.h"
#include "content/loader.h"
#include "platform/services.h"

#include <RmlUi/Core.h>
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
        dimensions = {1, 1};
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

void print(Rml::Element* e, const char* label) {
    if (e == nullptr) {
        std::cout << std::format("{:<12} missing\n", label);
        return;
    }
    const Rml::Vector2f size = e->GetBox().GetSize(Rml::BoxArea::Border);
    const Rml::Vector2f pos = e->GetAbsoluteOffset(Rml::BoxArea::Border);
    std::cout << std::format("{:<12} at {:7.1f},{:7.1f} size {:7.1f} x {:7.1f} display={}\n", label,
                             pos.x, pos.y, size.x, size.y, e->GetProperty<Rml::String>("display"));
}

void dump(Rml::Element* e, int depth, int maxDepth) {
    if (e == nullptr || depth > maxDepth) {
        return;
    }
    const Rml::Vector2f size = e->GetBox().GetSize(Rml::BoxArea::Border);
    const Rml::Vector2f pos = e->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto display = e->GetProperty<Rml::String>("display");
    if (display == "0") {
        return;  // display: none
    }
    std::cout << std::format(
        "{:{}}<{} id={} class={}> at {:.0f},{:.0f} size {:.0f} x {:.0f} display={}\n", "",
        depth * 2, e->GetTagName(), e->GetId(), e->GetClassNames(), pos.x, pos.y, size.x, size.y,
        display);
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
    Rml::SetRenderInterface(&render);
    Rml::Initialise();
    for (const char* face : {"Barlow-Regular.ttf", "Barlow-Medium.ttf", "Barlow-SemiBold.ttf",
                             "BarlowCondensed-SemiBold.ttf", "BarlowCondensed-Bold.ttf",
                             "JetBrainsMono-Regular.ttf", "JetBrainsMono-Medium.ttf"}) {
        Rml::LoadFontFace((assets / "fonts" / face).string());
    }
    game::registerChartElement();
    Rml::Context* context = Rml::CreateContext("main", Rml::Vector2i(1366, 820));
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
        context->Update();
        Rml::ElementDocument* doc = context->GetDocument(0);
        print(doc, "body");
        for (const char* id : {"header", "body", "nav", "content", "status"}) {
            print(doc->GetElementById(id), id);
        }
        dump(doc->GetElementById("content"), 0, 4);
        Rml::Element* content = doc->GetElementById("content");
        if (content == nullptr || content->GetNumChildren() == 0 ||
            content->GetChild(0)->GetBox().GetSize(Rml::BoxArea::Border).x < 100.0F) {
            std::cout << "LAYOUT FAILURE: content panel children have no width\n";
            exitCode = 3;
        }
        Rml::ElementList tables;
        doc->GetElementsByTagName(tables, "table");
        std::cout << std::format("tables: {}\n", tables.size());
        if (!tables.empty()) {
            print(tables[0], "table[0]");
        }
        Rml::ElementList h1s;
        doc->GetElementsByTagName(h1s, "h1");
        if (!h1s.empty()) {
            print(h1s[0], "h1[0]");
            print(h1s[0]->GetParentNode(), "h1 parent");
        }
    }
    // The app, its document and data model are gone before RmlUi shuts down.
    Rml::Shutdown();
    return exitCode;
}
