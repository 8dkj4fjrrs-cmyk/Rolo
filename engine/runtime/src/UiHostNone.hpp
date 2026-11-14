#pragma once

// UiHost for systems without a desktop window (Android).
//
// The real UiHost (UiHost.hpp) joins Rolo's UI toolkit to a GLFW window:
// keys, the mouse, the clipboard, cursor shapes. Android has no GLFW, and
// nothing there uses the UI layer: it is the editor's, and games draw
// their own text and controls. Application::run() refuses a UI layer on
// Android, so this class is never made; it exists so that the code written
// for the real one compiles unchanged.
//
// Internal to rolo_runtime.

#include "rolo/core/Log.hpp"
#include "rolo/ui/Context.hpp"

#include <axmol.h>   // (brings in Android's own RenderViewImpl; platform/RenderViewImpl.h is the desktop one)

#include <filesystem>
#include <functional>
#include <vector>

namespace rolo::detail {

class UiHost {
public:
    static constexpr ui::TextureId kWorldTexture = 2;

    ui::Context& context() noexcept { return m_context; }
    float pixelScale() const noexcept { return 1.0f; }
    void setWorldTexture(ax::Texture2D*) {}
    ui::TextureId textureId(ax::Texture2D*) { return 0; }
    void forgetTextures() {}
    void setDropHandler(std::function<void(const std::vector<std::filesystem::path>&)>) {}
    void beginFrame(double) {}
    void endFrame() {}

private:
    ui::Context m_context;
};

} // namespace rolo::detail
