// Rolo runtime application — Axmol implementation.
//
// How the pieces fit together:
//
//   rolo::Application::run()
//     └─ creates BackendApp (an ax::Application) and calls its run()
//          └─ Axmol calls BackendApp::applicationDidFinishLaunching()
//               └─ Impl::startWindow(): window, diagnostics, the world
//                  (a SceneRenderer drawing a rolo::Scene),
//                  the optional UI layer (UiHost), the per-frame callback
//          └─ Axmol's main loop, every frame:
//               update → Impl::onFrame(): game callback, then the UI is
//                        updated, laid out and painted into a draw list
//               draw   → (Offscreen) WorldCaptureNode renders the world into
//                        a texture; then the UI's draw list is drawn on top
//          └─ loop ends when the window closes or Director::end() is called
//     └─ report statistics, restore logging, return a Status
//
// Axmol types never appear outside this folder.

#include "rolo/runtime/Gamepads.hpp"
#include "rolo/runtime/Application.hpp"

#include "AxmolLogBridge.hpp"
#include "SceneRenderer.hpp"
#include "TextureStore.hpp"
#include "UiHost.hpp"

#include <axmol.h>
#include <renderer/backend/DriverBase.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <utility>

namespace rolo {

namespace {

constexpr std::string_view kCategory = "runtime";

// Desktop platforms have exactly one main window and graphics context, and
// Axmol's Application/Director are process-wide singletons. This flag turns
// an accidental second Application into a clear error instead of a crash.
// (A deliberate, documented exception to "no global mutable state".)
std::atomic<bool> g_backendInUse{false};

std::string toString(ax::Vec2 size)
{
    return std::to_string(static_cast<int>(size.x)) + "x" + std::to_string(static_cast<int>(size.y));
}

// Renders the (hidden) world into a texture every frame. The world stays in
// the running scene so its animations and timers run normally; it is only
// made visible for the moment it is drawn into the texture. What it draws,
// and how, is decided by Application::Impl::captureWorld().
class WorldCaptureNode final : public ax::Node {
public:
    static WorldCaptureNode* create(std::function<void(ax::Renderer*)> capture)
    {
        auto* node = new WorldCaptureNode(std::move(capture));
        node->autorelease();
        return node;
    }

    void visit(ax::Renderer* renderer, const ax::Mat4& /*parentTransform*/, uint32_t /*parentFlags*/) override
    {
        m_capture(renderer);
    }

private:
    explicit WorldCaptureNode(std::function<void(ax::Renderer*)> capture)
        : m_capture(std::move(capture))
    {
    }

    std::function<void(ax::Renderer*)> m_capture;
};

// The transform that shows world point `center` in the middle of a
// `width` x `height` area, one world point covering `scale` units.
ax::Mat4 viewTransform(Vec2 center, float scale, float width, float height)
{
    ax::Mat4 m;
    ax::Mat4::createTranslation(width * 0.5f - center.x * scale, height * 0.5f - center.y * scale, 0.0f, &m);
    m.scale(scale, scale, 1.0f);
    return m;
}

// Axmol's key codes as Rolo keys (game code never sees Axmol's).
Key toRoloKey(ax::EventKeyboard::KeyCode code)
{
    using K = ax::EventKeyboard::KeyCode;
    const int c = static_cast<int>(code);
    // Letters, digits and F-keys are consecutive in both enums.
    if (c >= static_cast<int>(K::KEY_A) && c <= static_cast<int>(K::KEY_Z)) {
        return static_cast<Key>(static_cast<int>(Key::A) + (c - static_cast<int>(K::KEY_A)));
    }
    if (c >= static_cast<int>(K::KEY_CAPITAL_A) && c <= static_cast<int>(K::KEY_CAPITAL_Z)) {
        return static_cast<Key>(static_cast<int>(Key::A) + (c - static_cast<int>(K::KEY_CAPITAL_A)));
    }
    if (c >= static_cast<int>(K::KEY_0) && c <= static_cast<int>(K::KEY_9)) {
        return static_cast<Key>(static_cast<int>(Key::Num0) + (c - static_cast<int>(K::KEY_0)));
    }
    if (c >= static_cast<int>(K::KEY_F1) && c <= static_cast<int>(K::KEY_F12)) {
        return static_cast<Key>(static_cast<int>(Key::F1) + (c - static_cast<int>(K::KEY_F1)));
    }
    switch (code) {
    case K::KEY_SPACE: return Key::Space;
    case K::KEY_ENTER:
    case K::KEY_KP_ENTER:
    case K::KEY_RETURN: return Key::Enter;
    case K::KEY_ESCAPE: return Key::Escape;
    case K::KEY_TAB: return Key::Tab;
    case K::KEY_BACKSPACE: return Key::Backspace;
    case K::KEY_LEFT_ARROW: return Key::Left;
    case K::KEY_RIGHT_ARROW: return Key::Right;
    case K::KEY_UP_ARROW: return Key::Up;
    case K::KEY_DOWN_ARROW: return Key::Down;
    case K::KEY_LEFT_SHIFT: return Key::LeftShift;
    case K::KEY_RIGHT_SHIFT: return Key::RightShift;
    case K::KEY_LEFT_CTRL: return Key::LeftControl;
    case K::KEY_RIGHT_CTRL: return Key::RightControl;
    case K::KEY_LEFT_ALT: return Key::LeftAlt;
    case K::KEY_RIGHT_ALT: return Key::RightAlt;
    default: return Key::Unknown;
    }
}

std::optional<MouseButton> toRoloButton(ax::EventMouse::MouseButton button)
{
    switch (button) {
    case ax::EventMouse::MouseButton::BUTTON_LEFT: return MouseButton::Left;
    case ax::EventMouse::MouseButton::BUTTON_RIGHT: return MouseButton::Right;
    case ax::EventMouse::MouseButton::BUTTON_MIDDLE: return MouseButton::Middle;
    default: return std::nullopt;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Impl: all state for one Application, including the Axmol-facing logic.
// ---------------------------------------------------------------------------
struct Application::Impl {
    Impl(Engine& e, WindowSettings w, PresentationSettings p)
        : engine(e)
        , window(std::move(w))
        , presentation(std::move(p))
    {
    }

    Engine& engine;
    WindowSettings window;
    PresentationSettings presentation;
    RunOptions options;
    std::function<void(const FrameInfo&)> frameCallback;
    Input* gameInput = nullptr;   // filled from the window's keyboard and mouse (not owned)
    std::unique_ptr<GamepadReader> gamepads;   // ...and from gamepads, while there is a game input
    RunReport report;

    bool hasRun = false;
    bool quitRequested = false;
    bool finishing = false;        // Director::end() has been requested
    bool screenshotFailed = false;
    std::string startupError;

    ax::Node* world = nullptr;                  // owned by the scene
    std::unique_ptr<detail::TextureStore> textures;
    std::unique_ptr<detail::SceneRenderer> renderer;
    const AssetDatabase* assets = nullptr;      // the project's assets (not owned)
    WorldView worldView;                        // what the off-screen image shows
    std::uint64_t uiTextureGeneration = 0;
    std::shared_ptr<const Scene> pendingScene;  // shown once the window exists
    const Scene* shownScene = nullptr;          // for logging scene changes
    ax::RenderTexture* worldTexture = nullptr;  // retained here (Offscreen only)
    std::unique_ptr<detail::UiHost> ui;
    float fps = 0.0f;
    double realClock = 0.0;                     // seconds of frames shown: animations' default clock
    std::optional<double> animationTime;        // set for one frame by setAnimationTime()


    Logger& log() { return engine.logger(); }
    bool uiEnabled() const { return static_cast<bool>(presentation.uiSetup); }

    bool startWindow();
    void startUi(ax::Scene& scene, ax::RenderView& view);
    void logDiagnostics(ax::RenderView& view);
    void onFrame(float deltaSeconds);
    void finish();
    void releaseBackendObjects();
    void captureWorld(ax::Renderer* renderer);
    bool resizeWorldTexture(int width, int height);
    void applyGameCamera();
    void listenForGameInput(ax::Scene& scene);
    void setAssets(const AssetDatabase* db)
    {
        assets = db;
        if (renderer) {
            renderer->setAssets(db);
        }
    }
    void showScene(std::shared_ptr<const Scene> scene)
    {
        if (renderer) {
            renderer->show(std::move(scene));
        } else {
            pendingScene = std::move(scene);
        }
    }
};

// UiFrame talks to the Impl through this small bridge.
struct UiFrame::Access {
    Application::Impl& impl;
};

namespace {

// Axmol drives the program through an ax::Application subclass. This one
// simply forwards the interesting moments to Application::Impl.
class BackendApp final : public ax::Application {
public:
    explicit BackendApp(rolo::Application::Impl& impl)
        : m_impl(impl)
    {
    }

    void initGfxContextAttrs() override
    {
        ::GfxContextAttrs attrs{}; // Axmol declares this outside namespace ax
        attrs.redBits = 8;
        attrs.greenBits = 8;
        attrs.blueBits = 8;
        attrs.alphaBits = 8;
        attrs.depthBits = 24;
        attrs.stencilBits = 8;
        attrs.multisamplingCount = 0;
        ax::RenderView::setGfxContextAttrs(attrs);
    }

    bool applicationDidFinishLaunching() override { return m_impl.startWindow(); }

    // Pause the loop while the app is minimised / in the background.
    void applicationDidEnterBackground() override { ax::Director::getInstance()->stopAnimation(); }
    void applicationWillEnterForeground() override { ax::Director::getInstance()->startAnimation(); }

private:
    rolo::Application::Impl& m_impl;
};

} // namespace

bool Application::Impl::startWindow()
{
    ax::Director* director = ax::Director::getInstance();

    ax::RenderView* view = ax::RenderViewImpl::createWithRect(
        window.title,
        ax::Rect(0, 0, static_cast<float>(window.width), static_cast<float>(window.height)),
        1.0f,
        window.resizable);
    if (view == nullptr) {
        startupError = "Could not create the game window. Check that your graphics drivers are up to date.";
        return false;
    }

    const auto worldW = static_cast<float>(presentation.worldWidth);
    const auto worldH = static_cast<float>(presentation.worldHeight);

    director->setRenderView(view);
    director->setStatsDisplay(options.showStats);
    director->setAnimationInterval(1.0f / 60.0f);
    // World coordinates = the game's resolution, whatever the window size.
    view->setDesignResolutionSize(worldW, worldH, ResolutionPolicy::SHOW_ALL);

    logDiagnostics(*view);

    auto* scene = ax::Scene::create();
    scene->setName("Root");
    // The world: an empty node the Rolo scene is drawn under.
    world = ax::Node::create();
    world->setName("World");
    world->setContentSize(ax::Size(worldW, worldH));
    scene->addChild(world);
    textures = std::make_unique<detail::TextureStore>(log());
    renderer = std::make_unique<detail::SceneRenderer>(*world, textures.get());
    renderer->setAssets(assets);
    std::shared_ptr<const Scene> first = pendingScene != nullptr ? pendingScene : presentation.scene;
    if (first == nullptr) {
        first = std::make_shared<const Scene>(makeStartScene(worldW, worldH));
    }
    renderer->show(std::move(first));
    renderer->update();
    shownScene = renderer->scene().get();
    log().info(kCategory, "Showing scene '" + shownScene->name() + "' (" + std::to_string(shownScene->size())
                              + " entities)");

    if (presentation.worldDisplay == WorldDisplay::Offscreen) {
        worldView.center = {worldW * 0.5f, worldH * 0.5f};
        if (!resizeWorldTexture(presentation.worldWidth, presentation.worldHeight)) {
            startupError = "Could not create the off-screen world image.";
            return false;
        }
        world->setVisible(false); // drawn only by the capture node
        scene->addChild(WorldCaptureNode::create([this](ax::Renderer* r) { captureWorld(r); }), -100);
        director->setClearColor(ax::Color4F(0.08f, 0.08f, 0.10f, 1.0f));
    } else {
        applyGameCamera();
    }

    director->runWithScene(scene);

    if (gameInput != nullptr && !uiEnabled()) {
        listenForGameInput(*scene);
    }

    if (options.escapeQuits) {
        auto* keyboard = ax::EventListenerKeyboard::create();
        keyboard->onKeyPressed = [this](ax::EventKeyboard::KeyCode code, ax::Event*) {
            if (code == ax::EventKeyboard::KeyCode::KEY_ESCAPE) {
                log().info(kCategory, "Escape pressed; closing.");
                quitRequested = true;
            }
        };
        director->getEventDispatcher()->addEventListenerWithSceneGraphPriority(keyboard, scene);
    }

    if (uiEnabled()) {
        startUi(*scene, *view);
    }

    // The window's close button: let the application say "not yet" (e.g. to
    // ask about unsaved changes). Axmol asks through this event.
    director->getEventDispatcher()->addCustomEventListener(
        ax::RenderViewImpl::EVENT_WINDOW_CLOSE, [this](ax::EventCustom* event) {
            if (presentation.onCloseRequested && !finishing && !presentation.onCloseRequested()) {
                *static_cast<bool*>(event->getUserData()) = false;
            }
        });

    // Axmol announces EVENT_RESET while shutting down, while the window and
    // graphics context still exist: the right moment to release the UI layer
    // and the world texture (doing it after the window is gone would crash).
    director->getEventDispatcher()->addCustomEventListener(ax::Director::EVENT_RESET,
                                                           [this](ax::EventCustom*) { releaseBackendObjects(); });

    // Run onFrame() every frame (interval 0) for as long as the loop lives.
    director->getScheduler()->schedule([this](float dt) { onFrame(dt); }, this, 0.0f, false, "rolo.frame");
    return true;
}

bool Application::Impl::resizeWorldTexture(int width, int height)
{
    width = std::clamp(width, 1, 8192);
    height = std::clamp(height, 1, 8192);
    if (worldTexture != nullptr) {
        const ax::Vec2 current = worldTexture->getSprite()->getTexture()->getContentSizeInPixels();
        if (static_cast<int>(current.x) == width && static_cast<int>(current.y) == height) {
            return true;
        }
    }
    // The size is in pixels: with a content scale factor other than 1 Axmol
    // would multiply it, so divide first.
    const float scale = ax::Director::getInstance()->getContentScaleFactor();
    auto* texture = ax::RenderTexture::create(static_cast<int>(static_cast<float>(width) / scale),
                                              static_cast<int>(static_cast<float>(height) / scale),
                                              ax::backend::PixelFormat::RGBA8, ax::backend::PixelFormat::D24S8);
    if (texture == nullptr) {
        return false;
    }
    texture->retain();
    texture->setKeepMatrix(true); // captureWorld() sets the projection itself
    AX_SAFE_RELEASE_NULL(worldTexture);
    worldTexture = texture;
    if (ui) {
        ui->setWorldTexture(worldTexture->getSprite()->getTexture());
    }
    return true;
}

void Application::Impl::captureWorld(ax::Renderer* r)
{
    if (worldTexture == nullptr || world == nullptr) {
        return;
    }
    ax::Director* director = ax::Director::getInstance();
    const ax::Vec2 size = worldTexture->getSprite()->getTexture()->getContentSizeInPixels();

    // Draw in texture pixels: (0, 0) bottom-left to (width, height) top-right.
    // RenderTexture would otherwise stretch the window's design area over the
    // texture, which blurs or distorts when the panel and game sizes differ.
    director->pushMatrix(ax::MATRIX_STACK_TYPE::MATRIX_STACK_PROJECTION);
    ax::Mat4 projection;
    ax::Mat4::createOrthographicOffCenter(0.0f, size.x, 0.0f, size.y, -1024.0f, 1024.0f, &projection);
    director->loadMatrix(ax::MATRIX_STACK_TYPE::MATRIX_STACK_PROJECTION, projection);
    director->pushMatrix(ax::MATRIX_STACK_TYPE::MATRIX_STACK_MODELVIEW);
    director->loadIdentityMatrix(ax::MATRIX_STACK_TYPE::MATRIX_STACK_MODELVIEW);

    worldTexture->beginWithClear(0.106f, 0.106f, 0.114f, 1.0f); // the editor's backdrop
    world->setVisible(true);
    world->visit(r, viewTransform(worldView.center, worldView.pixelsPerPoint, size.x, size.y),
                 ax::Node::FLAGS_TRANSFORM_DIRTY);
    world->setVisible(false);
    worldTexture->end();

    director->popMatrix(ax::MATRIX_STACK_TYPE::MATRIX_STACK_MODELVIEW);
    director->popMatrix(ax::MATRIX_STACK_TYPE::MATRIX_STACK_PROJECTION);
}

void Application::Impl::applyGameCamera()
{
    if (world == nullptr || renderer == nullptr) {
        return;
    }
    // Without a camera the window shows the world from (0, 0) to the game's
    // resolution; with one, it is centred on the camera.
    const auto view = renderer->camera();
    const auto w = static_cast<float>(presentation.worldWidth);
    const auto h = static_cast<float>(presentation.worldHeight);
    const Vec2 center = view ? view->center : Vec2{w * 0.5f, h * 0.5f};
    const float zoom = view ? std::max(0.01f, view->zoom) : 1.0f;
    world->setScale(zoom);
    world->setPosition(w * 0.5f - center.x * zoom, h * 0.5f - center.y * zoom);
}

void Application::Impl::listenForGameInput(ax::Scene& scene)
{
    auto* dispatcher = ax::Director::getInstance()->getEventDispatcher();
    auto* keyboard = ax::EventListenerKeyboard::create();
    keyboard->onKeyPressed = [this](ax::EventKeyboard::KeyCode code, ax::Event*) { gameInput->press(toRoloKey(code)); };
    keyboard->onKeyReleased = [this](ax::EventKeyboard::KeyCode code, ax::Event*) {
        gameInput->release(toRoloKey(code));
    };
    dispatcher->addEventListenerWithSceneGraphPriority(keyboard, &scene);

    // The pointer in world coordinates: the world node carries the camera,
    // so converting into its space undoes the camera's move and zoom.
    auto* mouse = ax::EventListenerMouse::create();
    const auto track = [this](ax::EventMouse* event) {
        if (world != nullptr) {
            const ax::Vec2 p = world->convertToNodeSpace(event->getLocation());
            gameInput->setPointer({p.x, p.y});
        }
    };
    // Axmol's mouse callbacks return whether the event was used; returning
    // false lets other listeners see it too.
    mouse->onMouseMove = [track](ax::EventMouse* event) {
        track(event);
        return false;
    };
    mouse->onMouseDown = [this, track](ax::EventMouse* event) {
        track(event);
        if (const auto button = toRoloButton(event->getMouseButton())) {
            gameInput->press(*button);
        }
        return false;
    };
    mouse->onMouseUp = [this, track](ax::EventMouse* event) {
        track(event);
        if (const auto button = toRoloButton(event->getMouseButton())) {
            gameInput->release(*button);
        }
        return false;
    };
    dispatcher->addEventListenerWithSceneGraphPriority(mouse, &scene);
}

void Application::Impl::startUi(ax::Scene& scene, ax::RenderView& view)
{
    auto* glfwWindow = static_cast<ax::RenderViewImpl&>(view).getWindow();
    ax::Texture2D* worldImage = worldTexture != nullptr ? worldTexture->getSprite()->getTexture() : nullptr;
    ui = std::make_unique<detail::UiHost>(log(), glfwWindow, scene, worldImage);
    ui->beginFrame(0.0);
    UiFrame::Access access{*this};
    UiFrame frame(access);
    presentation.uiSetup(frame);
    log().info(kCategory, "UI layer: Rolo UI");
}

void Application::Impl::logDiagnostics(ax::RenderView& view)
{
#if AX_USE_METAL
    const std::string_view graphicsApi = "Metal";
#else
    const std::string_view graphicsApi = "OpenGL";
#endif
    auto* driver = ax::backend::DriverBase::getInstance();

    log().info(kCategory, std::string("Backend: ") + ax::axmolVersion());
    log().info(kCategory, "Graphics API: " + std::string(graphicsApi));
    if (driver != nullptr) {
        log().info(kCategory, std::string("GPU: ") + driver->getRenderer() + " (" + driver->getVendor() + ")");
        log().info(kCategory, std::string("Driver version: ") + driver->getVersion());
    }
    log().info(kCategory, "Window: '" + window.title + "', " + toString(view.getFrameSize())
                              + " points, retina factor " + std::to_string(view.getRetinaFactor()));
    log().info(kCategory, "World: " + std::to_string(presentation.worldWidth) + "x"
                              + std::to_string(presentation.worldHeight) + " points, "
                              + (presentation.worldDisplay == WorldDisplay::Offscreen ? "off-screen" : "full window"));
}

void Application::Impl::onFrame(float deltaSeconds)
{
    realClock += static_cast<double>(deltaSeconds);
    if (gameInput != nullptr) {
        if (!gamepads) {
            gamepads = std::make_unique<GamepadReader>(log());
        }
        gamepads->read(*gameInput);   // before the game's update reads them
    }
    if (frameCallback) {
        frameCallback(FrameInfo{report.framesRendered, static_cast<double>(deltaSeconds)});
    }
    ++report.framesRendered;

    // Smoothed frames per second (exponential moving average).
    if (deltaSeconds > 0.0f) {
        const float instant = 1.0f / deltaSeconds;
        fps = fps <= 0.0f ? instant : fps + (instant - fps) * 0.05f;
    }

    if (auto* view = ax::Director::getInstance()->getRenderView()) {
        const ax::Vec2 size = view->getFrameSize();
        report.finalWindowWidth = static_cast<int>(size.x);
        report.finalWindowHeight = static_cast<int>(size.y);
    }

    if (ui) {
        ui->beginFrame(static_cast<double>(deltaSeconds));
        if (presentation.uiUpdate) {
            UiFrame::Access access{*this};
            UiFrame frame(access);
            presentation.uiUpdate(frame);
        }
        ui->endFrame();
    }
    if (ui && textures && textures->generation() != uiTextureGeneration) {
        // A thumbnail's image was loaded again: let the UI ask for the new one.
        // (Done before the UI runs, so it never draws a forgotten texture.)
        uiTextureGeneration = textures->generation();
    }
    if (renderer) {
        const Scene* before = shownScene;
        renderer->update(); // redraw the scene if it changed this frame
        renderer->animate(animationTime.value_or(realClock));
        if (presentation.worldDisplay == WorldDisplay::FullWindow) {
            applyGameCamera();
        }
        shownScene = renderer->scene().get();
        animationTime.reset();
        if (shownScene != before && shownScene != nullptr) {
            log().info(kCategory, "Showing scene '" + shownScene->name() + "' ("
                                      + std::to_string(shownScene->size()) + " entities)");
        }
    }

    if (finishing) {
        return;
    }

    const bool frameLimitReached = options.maxFrames > 0 && report.framesRendered >= options.maxFrames;
    if (quitRequested) {
        finish();
    } else if (frameLimitReached) {
        if (options.screenshotPath.empty()) {
            finish();
            return;
        }
        // Capture what is on screen, then stop. Axmol saves the image after
        // the frame is drawn and calls us back with the result.
        finishing = true;
        const std::string path = options.screenshotPath.string();
        ax::utils::captureScreen(
            [this, path](bool ok, std::string_view savedTo) {
                if (ok) {
                    log().info(kCategory, "Screenshot saved: " + std::string(savedTo));
                } else {
                    screenshotFailed = true;
                    log().error(kCategory, "Screenshot could not be saved to " + path);
                }
                ax::Director::getInstance()->end();
            },
            path);
    }
}

void Application::Impl::finish()
{
    finishing = true;
    ax::Director::getInstance()->end(); // the loop exits after this frame
}

void Application::Impl::releaseBackendObjects()
{
    if (ui) {
        if (presentation.uiShutdown) {
            UiFrame::Access access{*this};
            UiFrame frame(access);
            presentation.uiShutdown(frame);
        }
        ui.reset(); // restores Axmol's input callbacks, frees UI textures
    }
    renderer.reset();
    textures.reset();
    AX_SAFE_RELEASE_NULL(worldTexture);
    world = nullptr;
}

// ---------------------------------------------------------------------------
// UiFrame
// ---------------------------------------------------------------------------
UiFrame::UiFrame(Access& access)
    : m_access(access)
{
}

ui::Context& UiFrame::ui()
{
    return m_access.impl.ui->context();
}

WorldImage UiFrame::worldImage() const
{
    const Application::Impl& impl = m_access.impl;
    if (impl.worldTexture == nullptr) {
        return {};
    }
    WorldImage image;
    image.texture = detail::UiHost::kWorldTexture;
    const ax::Vec2 pixels = impl.worldTexture->getSprite()->getTexture()->getContentSizeInPixels();
    const float pixelScale = impl.ui ? impl.ui->pixelScale() : 1.0f;
    image.size = {pixels.x / pixelScale, pixels.y / pixelScale};
#if AX_USE_GL
    // OpenGL stores render textures upside down.
    image.uv0 = {0.0f, 1.0f};
    image.uv1 = {1.0f, 0.0f};
#else
    image.uv0 = {0.0f, 0.0f};
    image.uv1 = {1.0f, 1.0f};
#endif
    return image;
}

int UiFrame::worldWidth() const noexcept
{
    return m_access.impl.presentation.worldWidth;
}

int UiFrame::worldHeight() const noexcept
{
    return m_access.impl.presentation.worldHeight;
}

float UiFrame::framesPerSecond() const noexcept
{
    return m_access.impl.fps;
}

void UiFrame::requestQuit()
{
    m_access.impl.quitRequested = true;
}

void UiFrame::showScene(std::shared_ptr<const Scene> scene)
{
    m_access.impl.showScene(std::move(scene));
}

void UiFrame::setWorldView(const WorldView& view)
{
    Application::Impl& impl = m_access.impl;
    impl.worldView = view;
    impl.worldView.pixelsPerPoint = std::clamp(view.pixelsPerPoint, 0.01f, 256.0f);
    const int width = view.pixelWidth > 0 ? view.pixelWidth : impl.presentation.worldWidth;
    const int height = view.pixelHeight > 0 ? view.pixelHeight : impl.presentation.worldHeight;
    if (impl.presentation.worldDisplay == WorldDisplay::Offscreen && !impl.resizeWorldTexture(width, height)) {
        impl.log().error(kCategory, "Could not resize the off-screen world image.");
    }
}

std::optional<GameCameraView> UiFrame::gameCamera() const
{
    const Application::Impl& impl = m_access.impl;
    if (!impl.renderer) {
        return std::nullopt;
    }
    if (const auto view = impl.renderer->camera()) {
        return GameCameraView{view->center, view->zoom};
    }
    return std::nullopt;
}

void UiFrame::setAssets(const AssetDatabase* assets)
{
    m_access.impl.setAssets(assets);
}

void UiFrame::setAnimationTime(double seconds)
{
    m_access.impl.animationTime = seconds;
}

std::optional<UiImage> UiFrame::loadImage(const std::filesystem::path& path, bool pixelArt)
{
    Application::Impl& impl = m_access.impl;
    if (!impl.textures || !impl.ui) {
        return std::nullopt;
    }
    ax::Texture2D* texture = impl.textures->get(path, pixelArt);
    if (texture == nullptr) {
        return std::nullopt;
    }
    const ax::Vec2 size = texture->getContentSizeInPixels();
    return UiImage{impl.ui->textureId(texture), {size.x, size.y}};
}

void UiFrame::setDropHandler(std::function<void(const std::vector<std::filesystem::path>&)> handler)
{
    if (m_access.impl.ui) {
        m_access.impl.ui->setDropHandler(std::move(handler));
    }
}

void UiFrame::setWindowTitle(const std::string& title)
{
    if (auto* view = ax::Director::getInstance()->getRenderView()) {
        view->setViewName(title);
        if (auto* glfwView = dynamic_cast<ax::RenderViewImpl*>(view)) {
            glfwSetWindowTitle(glfwView->getWindow(), title.c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
Application::Application(Engine& engine, WindowSettings window, PresentationSettings presentation)
    : m_impl(std::make_unique<Impl>(engine, std::move(window), std::move(presentation)))
{
}

Application::~Application() = default;

void Application::showScene(std::shared_ptr<const Scene> scene)
{
    m_impl->showScene(std::move(scene));
}

void Application::setAssets(const AssetDatabase* assets)
{
    m_impl->setAssets(assets);
}

void Application::setAnimationTime(double seconds)
{
    m_impl->animationTime = seconds;
}

void Application::requestQuit()
{
    m_impl->quitRequested = true;
}

void Application::setGameInput(Input* input)
{
    m_impl->gameInput = input;
}

void Application::setFrameCallback(std::function<void(const FrameInfo&)> callback)
{
    m_impl->frameCallback = std::move(callback);
}

const RunReport& Application::lastRunReport() const noexcept
{
    return m_impl->report;
}

Status Application::run(const RunOptions& options)
{
    Impl& impl = *m_impl;

    // --- Check everything we can before touching the backend. -------------
    if (impl.hasRun) {
        return Status::failure("Application::run() can only be called once.");
    }
    if (!impl.engine.isRunning()) {
        return Status::failure("Call Engine::initialize() before Application::run().");
    }
    if (impl.window.width <= 0 || impl.window.height <= 0) {
        return Status::failure("Window size must be positive, got " + std::to_string(impl.window.width) + "x"
                               + std::to_string(impl.window.height) + ".");
    }
    if (impl.presentation.worldWidth <= 0 || impl.presentation.worldHeight <= 0) {
        return Status::failure("World size must be positive.");
    }
    if (impl.presentation.worldDisplay == WorldDisplay::Offscreen && !impl.uiEnabled()) {
        return Status::failure("An off-screen world needs a UI layer to show it (PresentationSettings::uiSetup).");
    }
    if (!options.screenshotPath.empty() && options.maxFrames == 0) {
        return Status::failure("A screenshot needs a frame limit (RunOptions::maxFrames > 0).");
    }
    if (g_backendInUse.exchange(true)) {
        return Status::failure("Only one Rolo Application can run at a time in a process.");
    }

    impl.hasRun = true;
    impl.options = options;
    impl.report = {};

    // --- Run. ----------------------------------------------------------------
    int exitCode = 0;
    const auto start = std::chrono::steady_clock::now();
    {
        detail::AxmolLogBridge logBridge(impl.engine.logger()); // backend logs → our logger
        impl.log().info(kCategory, "Opening window...");
        BackendApp backend(impl);
        exitCode = backend.run(); // blocks until the window closes
        if (impl.ui || impl.worldTexture != nullptr) {
            // Normally released on EVENT_RESET (see startWindow). If the backend
            // stopped without announcing it, leaking is safer than touching a
            // destroyed window.
            impl.log().warning(kCategory, "Backend stopped without a reset event; UI resources not released.");
            static_cast<void>(impl.ui.release());
        }
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    impl.report.elapsedSeconds = elapsed.count();
    g_backendInUse = false;

    // --- Report. -------------------------------------------------------------
    impl.log().info(kCategory, "Window closed after " + std::to_string(impl.report.framesRendered) + " frames ("
                                   + std::to_string(static_cast<int>(impl.report.averageFps() + 0.5))
                                   + " fps average).");

    if (!impl.startupError.empty()) {
        return Status::failure(impl.startupError);
    }
    if (exitCode != 0) {
        return Status::failure("The runtime backend stopped with error code " + std::to_string(exitCode) + ".");
    }
    if (impl.screenshotFailed) {
        return Status::failure("The screenshot could not be saved.");
    }
    return Status::success();
}

} // namespace rolo
