// Headless layout probe: loads the real interface with a stub renderer and
// prints computed boxes, so layout problems can be diagnosed without a
// display. Also a regression test: exits non-zero if the content panel's
// first child collapses to zero width. Not part of the shipped game.
#include "app.h"
#include "chart_element.h"
#include "content/loader.h"
#include "platform/services.h"

#include <RmlUi/Core.h>
#include <cstdio>
#include <filesystem>
#include <memory>
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
        std::printf("%-12s missing\n", label);
        return;
    }
    const Rml::Vector2f size = e->GetBox().GetSize(Rml::BoxArea::Border);
    const Rml::Vector2f pos = e->GetAbsoluteOffset(Rml::BoxArea::Border);
    std::printf("%-12s at %7.1f,%7.1f size %7.1f x %7.1f display=%s\n", label,
                static_cast<double>(pos.x), static_cast<double>(pos.y), static_cast<double>(size.x),
                static_cast<double>(size.y), e->GetProperty<Rml::String>("display").c_str());
}

void dump(Rml::Element* e, int depth, int maxDepth) {
    if (e == nullptr || depth > maxDepth) {
        return;
    }
    const Rml::Vector2f size = e->GetBox().GetSize(Rml::BoxArea::Border);
    const Rml::Vector2f pos = e->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::String display = e->GetProperty<Rml::String>("display");
    if (display == "0") {
        return;  // display: none
    }
    std::printf("%*s<%s id=%s class=%s> at %.0f,%.0f size %.0f x %.0f display=%s\n", depth * 2, "",
                e->GetTagName().c_str(), e->GetId().c_str(), e->GetClassNames().c_str(),
                static_cast<double>(pos.x), static_cast<double>(pos.y), static_cast<double>(size.x),
                static_cast<double>(size.y), display.c_str());
    for (int i = 0; i < e->GetNumChildren(); ++i) {
        dump(e->GetChild(i), depth + 1, maxDepth);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path assets = argc > 1 ? argv[1] : DC_ASSETS_DIR;
    const content::LoadResult loaded = content::loadFromDirectory(DC_DATA_DIR);
    if (!loaded.content) {
        return 1;
    }
    StubRender render;
    Rml::SetRenderInterface(&render);
    Rml::Initialise();
    Rml::LoadFontFace((assets / "fonts" / "DejaVuSans.ttf").string());
    Rml::LoadFontFace((assets / "fonts" / "DejaVuSans-Bold.ttf").string());
    game::registerChartElement();
    Rml::Context* context = Rml::CreateContext("main", Rml::Vector2i(1366, 820));
    auto services = platform::makeNullServices(std::filesystem::temp_directory_path(), "Probe");
    {
        game::App app(*context, *services, *loaded.content, assets);
        if (!app.loadDocument()) {
            return 2;
        }
        context->Update();
        Rml::ElementDocument* doc = context->GetDocument(0);
        print(doc, "body");
        for (const char* id : {"header", "body", "nav", "content", "status"}) {
            print(doc->GetElementById(id), id);
        }
        dump(doc->GetElementById("content"), 0, 4);
        Rml::Element* content = doc->GetElementById("content");
        int exitCode = 0;
        if (content == nullptr || content->GetNumChildren() == 0 ||
            content->GetChild(0)->GetBox().GetSize(Rml::BoxArea::Border).x < 100.0F) {
            std::printf("LAYOUT FAILURE: content panel children have no width\n");
            exitCode = 3;
        }
        Rml::ElementList tables;
        doc->GetElementsByTagName(tables, "table");
        std::printf("tables: %zu\n", tables.size());
        if (!tables.empty()) {
            print(tables[0], "table[0]");
        }
        Rml::ElementList h1s;
        doc->GetElementsByTagName(h1s, "h1");
        if (!h1s.empty()) {
            print(h1s[0], "h1[0]");
            print(h1s[0]->GetParentNode(), "h1 parent");
        }
        if (exitCode != 0) {
            Rml::Shutdown();
            return exitCode;
        }
    }
    Rml::Shutdown();
    return 0;
}
