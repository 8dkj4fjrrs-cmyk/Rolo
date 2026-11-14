#pragma once

// Rolo runtime application: opens the game window and runs the main loop.
//
// This header deliberately contains no Axmol (or other backend) types. Game
// code and tools talk to Rolo; Rolo talks to the backend in src/.

#include "rolo/core/Engine.hpp"
#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/gameplay/Input.hpp"
#include "rolo/scene/Scene.hpp"
#include "rolo/ui/Context.hpp"

#include <cstdint>
#include <optional>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rolo {

/// How the game window should look when it opens.
struct WindowSettings {
    std::string title = "Rolo Engine";
    int width = 1280;  // in screen points (a Retina screen draws 2x the pixels)
    int height = 720;
    bool resizable = true;
};

/// Where the game world is drawn.
enum class WorldDisplay {
    /// The world fills the window (letterboxed to keep its shape). Used by games.
    FullWindow,
    /// The world is drawn into an off-screen image that the UI layer shows
    /// (see UiFrame::worldImage()). Used by the editor's viewport.
    Offscreen,
};

/// How the runtime presents the world and the optional UI layer.
struct PresentationSettings {
    WorldDisplay worldDisplay = WorldDisplay::FullWindow;

    /// The game's own resolution in points. World coordinates run from (0, 0)
    /// at the bottom-left to (worldWidth, worldHeight) at the top-right.
    int worldWidth = 1280;
    int worldHeight = 720;

    /// The scene shown when the window opens. Empty: Rolo's start scene.
    std::shared_ptr<const Scene> scene;

    /// Called when the user clicks the window's close button. Return false to
    /// keep the window open (e.g. to ask about unsaved changes first).
    std::function<bool()> onCloseRequested;

    /// Turns on the Rolo UI layer (rolo_ui) drawn over the window.
    /// Called once when the window is ready: build your widgets here.
    std::function<void(class UiFrame&)> uiSetup;
    /// Called every frame before the UI is laid out and drawn: refresh data here.
    std::function<void(class UiFrame&)> uiUpdate;
    /// Called once before the UI shuts down (e.g. to save the layout).
    std::function<void(class UiFrame&)> uiShutdown;
};

/// Optional behaviour for a single run, mostly useful for automated tests.
struct RunOptions {
    /// Stop automatically after this many frames. 0 = run until the window is closed.
    /// (The backend may draw a few extra frames while shutting down or saving
    /// a screenshot; RunReport counts every frame actually drawn.)
    std::uint64_t maxFrames = 0;

    /// If set, save a PNG of the final frame here before stopping.
    /// Requires maxFrames > 0 so there *is* a final frame.
    std::filesystem::path screenshotPath;

    /// Show the backend's frame-rate / draw-call overlay in the corner.
    bool showStats = true;

    /// Close the window when Escape is pressed (games: yes; editor: no).
    bool escapeQuits = true;
};

/// Passed to the per-frame callback.
struct FrameInfo {
    std::uint64_t frameIndex = 0;  // 0 for the first frame
    double deltaSeconds = 0.0;     // time since the previous frame
};

/// Summary returned after the window closes.
struct RunReport {
    std::uint64_t framesRendered = 0;
    double elapsedSeconds = 0.0;
    int finalWindowWidth = 0;   // window size (points) when the run ended
    int finalWindowHeight = 0;
    /// Only in a web browser: run() returned although the game goes on (the
    /// browser calls its frames from now on). Keep the Application, and all
    /// it was given, alive: do not destroy them.
    bool continuesInBackground = false;
    double averageFps() const { return elapsedSeconds > 0.0 ? static_cast<double>(framesRendered) / elapsedSeconds : 0.0; }
};

/// The off-screen world image, ready for a ui::ImageView.
struct WorldImage {
    ui::TextureId texture = 0;  // 0 when the world is not drawn off-screen
    ui::Size size;              // the image's size, in points
    ui::Vec2 uv0;               // texture corners (the image may be stored upside down)
    ui::Vec2 uv1;
};

/// Which part of the world the off-screen image shows (the editor's view).
/// The image is `pixelWidth` x `pixelHeight` pixels; world point `center`
/// is in its middle, and one world point covers `pixelsPerPoint` pixels.
struct WorldView {
    Vec2 center;
    float pixelsPerPoint = 1.0f;
    int pixelWidth = 0;    // 0 = the game's resolution
    int pixelHeight = 0;
};

/// Where the scene's Camera component looks (see rolo::Camera).
struct GameCameraView {
    Vec2 center;
    float zoom = 1.0f;
};

/// An image file loaded for the UI (e.g. a thumbnail).
struct UiImage {
    ui::TextureId texture = 0;
    ui::Size size;   // in pixels
};

/// Runtime services for the UI layer. Valid inside the ui* callbacks only.
class UiFrame {
public:
    /// The UI: set its root widget, theme, shortcuts...
    ui::Context& ui();

    WorldImage worldImage() const;
    int worldWidth() const noexcept;
    int worldHeight() const noexcept;

    /// Shows `scene` in the world from the next frame on. The runtime keeps
    /// it alive while shown and redraws it whenever its revision() changes.
    void showScene(std::shared_ptr<const Scene> scene);

    /// Changes the window's title bar text.
    void setWindowTitle(const std::string& title);

    /// Sets which part of the world the off-screen image shows, and its size.
    void setWorldView(const WorldView& view);
    /// The scene's game camera, if it has one.
    std::optional<GameCameraView> gameCamera() const;

    /// The project's assets, used to draw sprites (null when no project is
    /// open). The database must stay alive while set.
    void setAssets(const AssetDatabase* assets);

    /// An image file as a UI texture (loaded once; again if the file
    /// changes). Nothing if it cannot be loaded.
    std::optional<UiImage> loadImage(const std::filesystem::path& path, bool pixelArt);

    /// Called with the files dropped onto the window (e.g. from Finder).
    void setDropHandler(std::function<void(const std::vector<std::filesystem::path>&)> handler);

    /// The animation clock for this frame (see setAnimationTime below).
    void setAnimationTime(double seconds);

    /// Frames per second, smoothed.
    float framesPerSecond() const noexcept;

    /// Asks the main loop to finish (same as Application::requestQuit()).
    void requestQuit();

    struct Access; // implementation detail
    explicit UiFrame(Access& access);

private:
    Access& m_access;
};

/// Owns the window and main loop for one run of the game.
///
/// Typical use:
///     rolo::Engine engine({.applicationName = "My Game"});
///     engine.initialize();
///     rolo::Application app(engine, {.title = "My Game"});
///     auto status = app.run();
///     engine.shutdown();
///
/// Only one Application may run per process (desktop platforms have one
/// main window and one graphics context), and run() may be called only once.
class Application {
public:
    Application(Engine& engine, WindowSettings window, PresentationSettings presentation = {});
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    /// Opens the window and blocks until it closes (user closes it, Escape is
    /// pressed, requestQuit() is called, or RunOptions::maxFrames is reached).
    /// In a web browser it returns at once while the game goes on: see
    /// RunReport::continuesInBackground.
    Status run(const RunOptions& options = {});

    /// Asks the main loop to finish after the current frame. Safe to call from
    /// the per-frame or UI callback.
    void requestQuit();

    /// Shows `scene` (same as UiFrame::showScene); may be called before run().
    void showScene(std::shared_ptr<const Scene> scene);

    /// The assets sprites are drawn from (same as UiFrame::setAssets); may be
    /// called before run(). The database must outlive the run.
    void setAssets(const AssetDatabase* assets);

    /// Called once per frame before drawing: run the game here.
    void setFrameCallback(std::function<void(const FrameInfo&)> callback);

    /// Sets the animation clock for the frame being made, in seconds:
    /// Animators show the frame their clip has at this time (see
    /// rolo::Animator). Call it every frame with the game's time, from the
    /// frame callback, so animations pause with the game. In a frame where it
    /// is not called, a clock that counts real time is used.
    void setAnimationTime(double seconds);

    /// Reports the window's keyboard and mouse to `input` (e.g. a Game's
    /// input), with the pointer in world coordinates. For games (no UI
    /// layer); the editor feeds its play mode from its own UI instead.
    /// Call before run(); `input` must outlive the run.
    void setGameInput(Input* input);

    /// Statistics from the last completed run().
    const RunReport& lastRunReport() const noexcept;

    /// Implementation details (hidden so that no backend types leak out).
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace rolo
