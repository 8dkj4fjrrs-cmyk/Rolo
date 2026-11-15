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
// In a web browser and on Android the loop is not Axmol's to run: run()
// sets the game up and returns while it goes on (see run(), and for
// Android the section "Android" below).
//
// Axmol types never appear outside this folder.

#include "rolo/runtime/Gamepads.hpp"
#include "rolo/runtime/Application.hpp"

#include "rolo/assets/ImageEncode.hpp"

#include "AxmolLogBridge.hpp"
#include "SceneRenderer.hpp"
#include "TextSprites.hpp"
#include "TextureStore.hpp"
#ifdef __ANDROID__
#include "rolo/runtime/Android.hpp"
#include "UiHostNone.hpp"   // no desktop window, and no UI layer: games only
#else
#include "UiHost.hpp"
#endif

#include <axmol.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif
#include <renderer/backend/DriverBase.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
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
    // Android: the direction buttons of a TV remote or a controller.
    case K::KEY_DPAD_LEFT: return Key::Left;
    case K::KEY_DPAD_RIGHT: return Key::Right;
    case K::KEY_DPAD_UP: return Key::Up;
    case K::KEY_DPAD_DOWN: return Key::Down;
    case K::KEY_DPAD_CENTER: return Key::Enter;
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
    // Touch screens: the on-screen controls (not owned), their picture, and
    // which fingers the game was told about.
    TouchControls* touchControls = nullptr;
    bool mouseActsAsTouch = false;
    bool mouseFingerDown = false;
    ax::Node* touchNode = nullptr;              // owned by the scene
    ax::Node* touchStickBase = nullptr;
    ax::Node* touchStickKnob = nullptr;
    std::vector<ax::Node*> touchButtonNodes;    // one per button: [0] its resting look, [1] pressed
    unsigned touchDrawn = 0;                    // the controls' revision that is drawn
    unsigned touchLaidOut = 0;
    enum class TouchPhase { Began, Moved, Ended };
    void onTouch(TouchPhase phase, int id, ax::Vec2 screen);
    void listenForTouches(ax::Scene& scene);
    void updateTouchControls();
    Vec2 viewSize;      // how much of the world the window shows (the game size, or more: ScreenFit)
    ScreenInsets safeInsets;   // strips of it to keep scores and controls out of (a phone's notch)
    int insetsChecked = 0;     // frames since the notch was last asked about (Android)
    void fitScreen();
    bool fitLogged = false;
    // The program was set aside (minimised; on a phone, another app is in
    // front or the screen is off) or is back.
    std::function<void(bool)> backgroundCallback;
    void setInBackground(bool inBackground);
#ifdef __ANDROID__
    ax::Vec2 loggedScreen;       // the screen size last written to the log
    void screenChanged();
#endif
    std::unique_ptr<GamepadReader> gamepads;   // ...and from gamepads, while there is a game input
    RunReport report;

    bool hasRun = false;
    bool quitRequested = false;
    bool finishing = false;        // Director::end() has been requested
    bool screenshotFailed = false;
    std::string startupError;

    ax::Node* world = nullptr;                  // owned by the scene
    ax::Node* statsNode = nullptr;              // the frame-rate overlay; owned by the scene
    double statsSeconds = 0.0;                  // time and frames since the overlay's text was made
    int statsFrames = 0;
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
    void startStats(ax::Scene& scene);
    void updateStats(float deltaSeconds);
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

// What Rolo asks of the screen it draws on: full colour, and a depth and
// stencil buffer (clipping uses the stencil).
void chooseScreenFormat()
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

#ifndef __ANDROID__
// Axmol drives the program through an ax::Application subclass. This one
// simply forwards the interesting moments to Application::Impl.
class BackendApp final : public ax::Application {
public:
    explicit BackendApp(rolo::Application::Impl& impl)
        : m_impl(impl)
    {
    }

    void initGfxContextAttrs() override { chooseScreenFormat(); }

    bool applicationDidFinishLaunching() override { return m_impl.startWindow(); }

    // Pause the loop while the app is minimised / in the background.
    void applicationDidEnterBackground() override
    {
        ax::Director::getInstance()->stopAnimation();
        m_impl.setInBackground(true);
    }
    void applicationWillEnterForeground() override
    {
        ax::Director::getInstance()->startAnimation();
        m_impl.setInBackground(false);
    }

private:
    rolo::Application::Impl& m_impl;
};
#endif

} // namespace

#ifdef __ANDROID__
// --- Android -------------------------------------------------------------------------------------
//
// On a computer a program starts at main() and opens its window. An Android
// app is the other way round: Android makes the screen (a Java "activity"
// with a surface to draw on; Axmol's AxmolActivity), loads the program as a
// library, and calls into it: when the surface is ready, for every frame,
// when the player leaves the app and comes back.
//
// Axmol passes those calls to the one ax::Application object, which has to
// exist from the moment the library is loaded: long before there is a
// rolo::Application. AndroidHost is that object. When the surface is ready
// it runs the program's start function (rolo::android::setStart), which
// makes a rolo::Application and calls run(). run() finds the screen there
// already, sets the game up on it and returns while the game goes on, as in
// a web browser; from then on the host passes Android's calls to it.
//
// (The start function, the host and the running Application are process-wide
// because Android's calls are: a documented exception to "no global mutable
// state", like g_backendInUse above.)
namespace {

std::function<void()> g_androidStart;
Application::Impl* g_androidGame = nullptr;   // the running Application's, while a game runs

class AndroidHost final : public ax::Application {
public:
    void initGfxContextAttrs() override { chooseScreenFormat(); }

    // The surface is ready (called once; on the thread that draws).
    bool applicationDidFinishLaunching() override
    {
        if (g_androidStart) {
            g_androidStart();
        }
        return true;
    }

    // Another app is in front, or the screen is off: no frames until the
    // player is back. (Android may also end the app while it waits.)
    void applicationDidEnterBackground() override
    {
        ax::Director::getInstance()->stopAnimation();
        if (g_androidGame != nullptr) {
            g_androidGame->log().info(kCategory, "Set aside (another app is in front, or the screen is off): waiting.");
            g_androidGame->setInBackground(true);
        }
    }
    void applicationWillEnterForeground() override
    {
        ax::Director::getInstance()->startAnimation();
        if (g_androidGame != nullptr) {
            g_androidGame->log().info(kCategory, "Back in front: the game goes on.");
            g_androidGame->setInBackground(false);
        }
    }

    // The surface has another size: the device was turned, a folding phone
    // opened. (Also called once at the start, with the real size.)
    void applicationScreenSizeChanged(int /*newWidth*/, int /*newHeight*/) override
    {
        if (g_androidGame != nullptr) {
            g_androidGame->screenChanged();
        }
    }
};

std::unique_ptr<AndroidHost> g_androidHost;

} // namespace

void android::setStart(std::function<void()> start)
{
    g_androidStart = std::move(start);
    if (!g_androidHost) {
        g_androidHost = std::make_unique<AndroidHost>();
    }
}
#endif

#ifdef __EMSCRIPTEN__
// The page gives the game's canvas its size in pixels (it knows the page's
// layout and how sharp the screen is): Module._rolo_set_window_size(w, h).
// Asked before the window exists, the size is kept for when it does.
namespace {
int g_pageWidth = 0;
int g_pageHeight = 0;
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void rolo_set_window_size(int width, int height)
{
    if (width < 16 || height < 16 || width > 16384 || height > 16384) {
        return;
    }
    g_pageWidth = width;
    g_pageHeight = height;
    auto* view = dynamic_cast<ax::RenderViewImpl*>(ax::Director::getInstance()->getRenderView());
    if (view != nullptr && view->getWindow() != nullptr) {
        glfwSetWindowSize(view->getWindow(), width, height);   // Axmol hears of it and Rolo fits the screen
    }
}

namespace {
void applyPageSize()
{
    if (g_pageWidth > 0) {
        rolo_set_window_size(g_pageWidth, g_pageHeight);
    }
}
} // namespace
#endif

bool Application::Impl::startWindow()
{
    ax::Director* director = ax::Director::getInstance();

#ifdef __ANDROID__
    // Android made the screen already: the app's surface, the whole display.
    ax::RenderView* view = director->getRenderView();
    if (view == nullptr) {
        startupError = "The app's screen is not ready. On Android, call Application::run() from the function "
                       "given to rolo::android::setStart.";
        return false;
    }
#else
    ax::RenderView* view = ax::RenderViewImpl::createWithRect(
        window.title,
        ax::Rect(0, 0, static_cast<float>(window.width), static_cast<float>(window.height)),
        1.0f,
        window.resizable);
    if (view == nullptr) {
        startupError = "Could not create the game window. Check that your graphics drivers are up to date.";
        return false;
    }
    director->setRenderView(view);
#endif

    const auto worldW = static_cast<float>(presentation.worldWidth);
    const auto worldH = static_cast<float>(presentation.worldHeight);

    // Rolo draws its own frame-rate overlay (startStats): Axmol's needs a
    // PNG decoder inside Axmol, which Rolo builds it without (ADR 0024).
    director->setStatsDisplay(false);
    director->setAnimationInterval(1.0f / 60.0f);
    // World coordinates = the game's resolution, whatever the window size
    // (fitScreen() lets a game window of another shape show more).
    view->setDesignResolutionSize(worldW, worldH, ResolutionPolicy::SHOW_ALL);
    viewSize = {worldW, worldH};

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

    if (options.showStats) {
        startStats(*scene);
    }

    if (presentation.worldDisplay == WorldDisplay::FullWindow && !uiEnabled()) {
#ifdef __EMSCRIPTEN__
        applyPageSize();   // the size the page asked for before the window existed
#endif
#ifdef __ANDROID__
        screenChanged();   // later changes come through AndroidHost
        // Android may take the graphics context away from an app in the
        // background (its pictures in the graphics card's memory are gone).
        // Axmol then makes everything again from the copies it keeps; this
        // only says so, for makers reading a phone's log and for the tests.
        ax::Director::getInstance()->getEventDispatcher()->addCustomEventListener(EVENT_RENDERER_RECREATED, [this](ax::EventCustom*) {
            log().info(kCategory, "Graphics: the context was lost in the background; pictures were made again.");
        });
#else
        fitScreen();
        director->getEventDispatcher()->addCustomEventListener(ax::RenderViewImpl::EVENT_WINDOW_RESIZED,
                                                               [this](ax::EventCustom*) { fitScreen(); });
#endif
    }

    director->runWithScene(scene);

    if (gameInput != nullptr && !uiEnabled()) {
        listenForGameInput(*scene);
        listenForTouches(*scene);
        if (touchControls != nullptr) {
            touchNode = ax::Node::create();
            touchNode->setVisible(false);
            scene->addChild(touchNode, 90000);   // over the game, under the frame-rate line
        }
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

#ifndef __ANDROID__   // (an Android app has no close button)
    // The window's close button: let the application say "not yet" (e.g. to
    // ask about unsaved changes). Axmol asks through this event.
    director->getEventDispatcher()->addCustomEventListener(
        ax::RenderViewImpl::EVENT_WINDOW_CLOSE, [this](ax::EventCustom* event) {
            if (presentation.onCloseRequested && !finishing && !presentation.onCloseRequested()) {
                *static_cast<bool*>(event->getUserData()) = false;
            }
        });
#endif

    // Axmol announces EVENT_RESET while shutting down, while the window and
    // graphics context still exist: the right moment to release the UI layer
    // and the world texture (doing it after the window is gone would crash).
    director->getEventDispatcher()->addCustomEventListener(ax::Director::EVENT_RESET, [this](ax::EventCustom*) {
        releaseBackendObjects();
#ifdef __ANDROID__
        // The game was quit: Axmol closes the app's screen next, and
        // RoloActivity ends the app with it. (No run() is waiting to report.)
        log().info(kCategory, "The game has ended after " + std::to_string(report.framesRendered) + " frames.");
        setInBackground(true);   // sound stops now, not when the app is gone
        g_androidGame = nullptr;
        g_backendInUse = false;
#endif
    });

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
    // (When the window shows more than the game size, the extra is shared
    // by both sides: the same point stays in the middle.)
    const auto view = renderer->camera();
    const Vec2 center = view ? view->center
                             : Vec2{static_cast<float>(presentation.worldWidth) * 0.5f,
                                    static_cast<float>(presentation.worldHeight) * 0.5f};
    const float zoom = view ? std::max(0.01f, view->zoom) : 1.0f;
    world->setScale(zoom);
    world->setPosition(viewSize.x * 0.5f - center.x * zoom, viewSize.y * 0.5f - center.y * zoom);
}

// A game window: works out how much of the world it shows for its shape
// (ScreenFit) and tells Axmol, the renderer and the touch controls. Called
// at the start and whenever the window's size changes.
void Application::Impl::fitScreen()
{
    auto* view = ax::Director::getInstance()->getRenderView();
    if (view == nullptr || presentation.worldDisplay != WorldDisplay::FullWindow || uiEnabled()) {
        return;
    }
    const Vec2 game{static_cast<float>(presentation.worldWidth), static_cast<float>(presentation.worldHeight)};
    const ax::Vec2 frame = view->getFrameSize();
    const Vec2 visible = visibleSize(game, {frame.x, frame.y}, presentation.screenFit);
    if (visible != viewSize) {
        viewSize = visible;
        view->setDesignResolutionSize(visible.x, visible.y, ResolutionPolicy::SHOW_ALL);
        touchLaidOut = 0;   // the on-screen controls are placed anew
        if (!fitLogged) {
            fitLogged = true;   // once: a window being dragged larger would fill the log
            log().info(kCategory, "Screen: the window shows " + std::to_string(static_cast<int>(visible.x + 0.5f)) + "x"
                                      + std::to_string(static_cast<int>(visible.y + 0.5f))
                                      + " points of the world (more than the game's size: it fills the window)");
        }
    }
#ifdef __ANDROID__
    // Where the screen's camera notch is, if it has one. Asked again now and
    // then (onFrame): turning the phone round moves it to the other side
    // without the screen changing size.
    {
        const android::SafeInsets px = android::safeInsets();
        const ScreenInsets insets = insetsInView({static_cast<float>(px.left), static_cast<float>(px.right),
                                                  static_cast<float>(px.bottom), static_cast<float>(px.top)},
                                                 {frame.x, frame.y}, visible);
        if (!(insets == safeInsets)) {
            safeInsets = insets;
            touchLaidOut = 0;
            const auto whole = [](float v) { return std::to_string(static_cast<int>(v + 0.5f)); };
            log().info(kCategory, "Screen: kept clear for the camera notch: left " + whole(insets.left) + ", right "
                                      + whole(insets.right) + ", bottom " + whole(insets.bottom) + ", top "
                                      + whole(insets.top) + " points");
        }
    }
#endif
    if (renderer) {
        renderer->setScreen(game, visible, safeInsets);
    }
    applyGameCamera();
}

void Application::Impl::setInBackground(bool inBackground)
{
    if (inBackground && gamepads) {
        gamepads->stopRumble();
    }
    if (backgroundCallback) {
        backgroundCallback(inBackground);
    }
}

#ifdef __ANDROID__
// The app's surface has a (new) size: fit the game to it, and say so in the
// log once per size (tests, and makers looking at a phone's log, read it).
void Application::Impl::screenChanged()
{
    fitScreen();
    const auto* view = ax::Director::getInstance()->getRenderView();
    if (view == nullptr) {
        return;
    }
    const ax::Vec2 frame = view->getFrameSize();
    if (frame.x <= 0.0f || frame.y <= 0.0f || frame == loggedScreen) {
        return;
    }
    loggedScreen = frame;
    log().info(kCategory, "Screen: " + toString(frame) + " pixels, showing "
                              + std::to_string(static_cast<int>(viewSize.x + 0.5f)) + "x"
                              + std::to_string(static_cast<int>(viewSize.y + 0.5f)) + " points of the world");
}
#endif

// The frame-rate overlay: one small line in the bottom-left corner, above
// everything, in the scene and not the world, so the camera does not move it.
void Application::Impl::startStats(ax::Scene& scene)
{
    statsNode = ax::Node::create();
    scene.addChild(statsNode, 100000);
    statsSeconds = 1.0;   // write the first text on the first frame
}

void Application::Impl::updateStats(float deltaSeconds)
{
    if (statsNode == nullptr) {
        return;
    }
    statsSeconds += static_cast<double>(deltaSeconds);
    ++statsFrames;
    if (statsSeconds < 0.5) {
        return;
    }
    ax::Director* director = ax::Director::getInstance();
    char text[160];
    const double perFrame = statsSeconds / static_cast<double>(statsFrames);
    // What was drawn last frame (this overlay included).
    std::snprintf(text, sizeof(text), "%.1f fps   %.1f ms   %u draw calls   %u vertices", 1.0 / perFrame,
                  perFrame * 1000.0, static_cast<unsigned>(director->getRenderer()->getDrawnBatches()),
                  static_cast<unsigned>(director->getRenderer()->getDrawnVertices()));
    statsSeconds = 0.0;
    statsFrames = 0;

    // The same size on screen whatever the game's resolution: the text is
    // drawn at the window's own pixels and scaled back into the world's.
    const auto* view = director->getRenderView();
    const float pixelsPerPoint = view != nullptr && view->getScaleY() > 0.0f ? view->getScaleY() : 1.0f;
    const float retina = view != nullptr ? std::max(1.0f, static_cast<float>(view->getRetinaFactor())) : 1.0f;
    const float scale = 1.0f / (pixelsPerPoint * retina);
    statsNode->removeAllChildren();
    detail::TextSpriteOptions corner;
    corner.anchor = {0.0f, 0.0f};
    // A dark copy one pixel down and right, so the line reads on any background.
    for (const bool shadow : {true, false}) {
        const detail::TextSprite line = detail::makeTextSprite(
            std::string(), text, 15.0f * retina, shadow ? Color{0, 0, 0, 220} : Color{255, 255, 255, 255}, corner);
        if (line.sprite == nullptr) {
            return;
        }
        line.sprite->setScale(scale);
        line.sprite->setPosition((8.0f * retina + (shadow ? 1.0f : 0.0f)) * scale,
                                 (6.0f * retina - (shadow ? 1.0f : 0.0f)) * scale);
        statsNode->addChild(line.sprite);
    }
}

void Application::Impl::listenForGameInput(ax::Scene& scene)
{
    auto* dispatcher = ax::Director::getInstance()->getEventDispatcher();
    auto* keyboard = ax::EventListenerKeyboard::create();
    keyboard->onKeyPressed = [this](ax::EventKeyboard::KeyCode code, ax::Event*) {
#ifdef __ANDROID__
        // Android's Back button (or gesture) arrives as the Escape key, but
        // it is no keyboard: a player using the touch screen still is, and
        // the on-screen controls stay.
        if (code == ax::EventKeyboard::KeyCode::KEY_ESCAPE && gameInput->lastDevice() == InputDevice::Touch) {
            gameInput->press(Key::Escape);
            gameInput->noteTouchUsed();
            return;
        }
#endif
        gameInput->press(toRoloKey(code));
    };
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
    // With mouseActsAsTouch, the left button is a finger (number 0) instead.
    const auto finger = [this](ax::EventMouse* event) {
        return mouseActsAsTouch && event->getMouseButton() == ax::EventMouse::MouseButton::BUTTON_LEFT;
    };
    mouse->onMouseMove = [this, track](ax::EventMouse* event) {
        if (mouseActsAsTouch) {
            if (mouseFingerDown) {
                onTouch(TouchPhase::Moved, 0, event->getLocation());
            }
            return false;
        }
        track(event);
        return false;
    };
    mouse->onMouseDown = [this, track, finger](ax::EventMouse* event) {
        if (finger(event)) {
            mouseFingerDown = true;
            onTouch(TouchPhase::Began, 0, event->getLocation());
            return false;
        }
        track(event);
        if (const auto button = toRoloButton(event->getMouseButton())) {
            gameInput->press(*button);
        }
        return false;
    };
    mouse->onMouseUp = [this, track, finger](ax::EventMouse* event) {
        if (finger(event)) {
            mouseFingerDown = false;
            onTouch(TouchPhase::Ended, 0, event->getLocation());
            return false;
        }
        track(event);
        if (const auto button = toRoloButton(event->getMouseButton())) {
            gameInput->release(*button);
        }
        return false;
    };
    dispatcher->addEventListenerWithSceneGraphPriority(mouse, &scene);
}

// --- Touch screens -------------------------------------------------------------------------------
//
// A finger is offered to the on-screen controls first; one they do not take
// is the game's (Input::touches), in world coordinates. `screen` is in the
// game's resolution, (0, 0) at the bottom-left of the picture.
void Application::Impl::onTouch(TouchPhase phase, int id, ax::Vec2 screen)
{
    if (gameInput == nullptr) {
        return;
    }
    gameInput->noteTouchUsed();
    const Vec2 at{screen.x, screen.y};
    const auto inWorld = [&]() -> Vec2 {
        const ax::Vec2 p = world != nullptr ? world->convertToNodeSpace(screen) : screen;
        return {p.x, p.y};
    };
    switch (phase) {
    case TouchPhase::Began:
        if (touchControls != nullptr && touchControls->touchBegan(id, at)) {
            return;
        }
        gameInput->touchBegan(id, inWorld());
        break;
    case TouchPhase::Moved:
        if (touchControls != nullptr && touchControls->owns(id)) {
            touchControls->touchMoved(id, at);
            return;
        }
        gameInput->touchMoved(id, inWorld());
        break;
    case TouchPhase::Ended:
        if (touchControls != nullptr && touchControls->owns(id)) {
            touchControls->touchEnded(id);
            return;
        }
        gameInput->touchEnded(id);
        break;
    }
}

#ifdef __EMSCRIPTEN__
namespace {
// The browser's touch events, read directly: each event lists every finger
// on the screen and marks the ones it is about (isChanged). Returning true
// stops the browser from also scrolling the page, zooming, or making a
// mouse click out of the touch.
EM_BOOL onBrowserTouch(int type, const EmscriptenTouchEvent* event, void* user)
{
    auto* impl = static_cast<Application::Impl*>(user);
    const auto* view = ax::Director::getInstance()->getRenderView();
    int canvasWidth = 0;
    int canvasHeight = 0;
    double cssWidth = 0.0;
    double cssHeight = 0.0;
    emscripten_get_canvas_element_size("#canvas", &canvasWidth, &canvasHeight);
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    if (view == nullptr || cssWidth <= 0.0 || cssHeight <= 0.0 || view->getScaleX() <= 0.0f || view->getScaleY() <= 0.0f) {
        return EM_TRUE;
    }
    const ax::Rect& port = view->getViewPortRect();
    for (int i = 0; i < event->numTouches; ++i) {
        const EmscriptenTouchPoint& t = event->touches[i];
        if (!t.isChanged) {
            continue;
        }
        // Page pixels -> the canvas's own pixels (y up) -> the game's resolution.
        const double px = static_cast<double>(t.targetX) * canvasWidth / cssWidth;
        const double py = canvasHeight - static_cast<double>(t.targetY) * canvasHeight / cssHeight;
        const ax::Vec2 screen(static_cast<float>((px - port.origin.x) / view->getScaleX()),
                              static_cast<float>((py - port.origin.y) / view->getScaleY()));
        const auto phase = type == EMSCRIPTEN_EVENT_TOUCHSTART  ? Application::Impl::TouchPhase::Began
                           : type == EMSCRIPTEN_EVENT_TOUCHMOVE ? Application::Impl::TouchPhase::Moved
                                                                : Application::Impl::TouchPhase::Ended;
        impl->onTouch(phase, static_cast<int>(t.identifier), screen);
    }
    return EM_TRUE;
}
} // namespace
#endif

void Application::Impl::listenForTouches(ax::Scene& scene)
{
#ifdef __ANDROID__
    // Android's touches come through Axmol, each finger with its own number
    // for as long as it is down.
    auto* fingers = ax::EventListenerTouchAllAtOnce::create();
    const auto each = [this](TouchPhase phase) {
        return [this, phase](const std::vector<ax::Touch*>& touches, ax::Event*) {
            for (const ax::Touch* touch : touches) {
                onTouch(phase, touch->getID(), touch->getLocation());
            }
        };
    };
    fingers->onTouchesBegan = each(TouchPhase::Began);
    fingers->onTouchesMoved = each(TouchPhase::Moved);
    fingers->onTouchesEnded = each(TouchPhase::Ended);
    fingers->onTouchesCancelled = each(TouchPhase::Ended);   // Android took the touch away (a system gesture)
    ax::Director::getInstance()->getEventDispatcher()->addEventListenerWithSceneGraphPriority(fingers, &scene);
    // A phone or tablet: show the on-screen controls from the start.
    gameInput->noteTouchUsed();
#else
    static_cast<void>(scene);
#endif
#ifdef __EMSCRIPTEN__
    // On the canvas, not through Axmol: its web touch code ends every finger
    // when one lifts, which breaks holding the stick while pressing a button.
    emscripten_set_touchstart_callback("#canvas", this, 1, onBrowserTouch);
    emscripten_set_touchmove_callback("#canvas", this, 1, onBrowserTouch);
    emscripten_set_touchend_callback("#canvas", this, 1, onBrowserTouch);
    emscripten_set_touchcancel_callback("#canvas", this, 1, onBrowserTouch);
    // A phone or tablet (no mouse to hover with): show the controls from the start.
    const bool touchOnly = EM_ASM_INT({
        return window.matchMedia('(pointer: coarse)').matches && !window.matchMedia('(any-hover: hover)').matches;
    }) != 0;
    if (touchOnly) {
        gameInput->noteTouchUsed();
    }
#endif
}

// Applies the on-screen controls to the game's input and keeps their picture
// up to date. They are drawn only while touch is what the player is using.
void Application::Impl::updateTouchControls()
{
    if (touchControls == nullptr || gameInput == nullptr || touchNode == nullptr) {
        return;
    }
    if (touchLaidOut == 0) {
        touchControls->layout(viewSize.x, viewSize.y, safeInsets);
    }
    const bool shown = gameInput->lastDevice() == InputDevice::Touch;
    if (!shown && touchNode->isVisible()) {
        touchControls->releaseAll();   // the player went back to keys or a gamepad
    }
    touchNode->setVisible(shown);
    touchControls->apply(*gameInput);

    const auto colour = [](float r, float g, float b, float a) { return ax::Color4F(r, g, b, a); };
    const ax::Color4F kFill = colour(0.07f, 0.08f, 0.11f, 0.34f);
    const ax::Color4F kRim = colour(1.0f, 1.0f, 1.0f, 0.5f);
    const ax::Color4F kPressed = colour(0.95f, 0.66f, 0.23f, 0.62f);   // Rolo's orange
    const auto disc = [&](float radius, const ax::Color4F& fill) {
        auto* d = ax::DrawNode::create();
        // The fill, and around it a rim made of its own triangles (drawn as a
        // thick line, a see-through rim comes out dotted where pieces overlap).
        const float rim = std::max(1.5f, radius * 0.045f);
        const float inner = radius - rim;
        d->drawSolidCircle({0.0f, 0.0f}, inner, 0.0f, 64, fill);
        constexpr int kPieces = 64;
        for (int i = 0; i < kPieces; ++i) {
            const float a0 = 6.2831853f * static_cast<float>(i) / kPieces;
            const float a1 = 6.2831853f * static_cast<float>(i + 1) / kPieces;
            const ax::Vec2 in0(std::cos(a0) * inner, std::sin(a0) * inner);
            const ax::Vec2 in1(std::cos(a1) * inner, std::sin(a1) * inner);
            const ax::Vec2 out0(std::cos(a0) * radius, std::sin(a0) * radius);
            const ax::Vec2 out1(std::cos(a1) * radius, std::sin(a1) * radius);
            d->drawTriangle(in0, out0, out1, kRim);
            d->drawTriangle(in0, out1, in1, kRim);
        }
        return d;
    };

    if (touchLaidOut != touchControls->layoutRevision()) {
        // The controls changed (other buttons, another size): draw them anew.
        touchLaidOut = touchControls->layoutRevision();
        touchDrawn = 0;
        touchNode->removeAllChildren();
        touchButtonNodes.clear();
        const TouchStick& stick = touchControls->stick();
        touchStickBase = disc(stick.radius, kFill);
        touchStickKnob = disc(stick.radius * 0.45f, colour(1.0f, 1.0f, 1.0f, 0.38f));
        touchNode->addChild(touchStickBase);
        touchNode->addChild(touchStickKnob);
        for (const TouchButton& b : touchControls->buttons()) {
            auto* button = ax::Node::create();
            button->setPosition(b.center.x, b.center.y);
            button->addChild(disc(b.radius, kFill));
            button->addChild(disc(b.radius, kPressed));
            // The action's name, drawn at twice the size and scaled to fit inside.
            detail::TextSpriteOptions middle;
            middle.anchor = {0.5f, 0.5f};
            const float size = b.radius * (b.menu ? 0.52f : 0.42f);
            const detail::TextSprite label =
                detail::makeTextSprite(std::string(), b.label, size * 2.0f, Color{255, 255, 255, 235}, middle);
            if (label.sprite != nullptr) {
                const float room = b.radius * 1.7f;
                label.sprite->setScale(std::min(0.5f, room / std::max(1.0f, label.width)));
                button->addChild(label.sprite);
            }
            touchNode->addChild(button);
            touchButtonNodes.push_back(button);
        }
    }
    if (touchDrawn != touchControls->revision()) {
        touchDrawn = touchControls->revision();
        const TouchStick& stick = touchControls->stick();
        touchStickBase->setPosition(stick.base.x, stick.base.y);
        touchStickKnob->setPosition(stick.knob.x, stick.knob.y);
        touchStickBase->setOpacity(stick.held ? 255 : 150);
        const auto& buttons = touchControls->buttons();
        for (std::size_t i = 0; i < buttons.size() && i < touchButtonNodes.size(); ++i) {
            const auto& parts = touchButtonNodes[i]->getChildren();
            parts.at(0)->setVisible(!buttons[i].down);
            parts.at(1)->setVisible(buttons[i].down);
            touchButtonNodes[i]->setScale(buttons[i].down ? 0.94f : 1.0f);
        }
    }
}

void Application::Impl::startUi(ax::Scene& scene, ax::RenderView& view)
{
#ifdef __ANDROID__
    // Never reached: run() refuses a UI layer on Android (UiHostNone.hpp).
    static_cast<void>(scene);
    static_cast<void>(view);
#else
    auto* glfwWindow = static_cast<ax::RenderViewImpl&>(view).getWindow();
    ax::Texture2D* worldImage = worldTexture != nullptr ? worldTexture->getSprite()->getTexture() : nullptr;
    ui = std::make_unique<detail::UiHost>(log(), glfwWindow, scene, worldImage);
    ui->beginFrame(0.0);
    UiFrame::Access access{*this};
    UiFrame frame(access);
    presentation.uiSetup(frame);
    log().info(kCategory, "UI layer: Rolo UI");
#endif
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
#ifdef __ANDROID__
    if (++insetsChecked >= 30) {   // about twice a second
        insetsChecked = 0;
        fitScreen();
    }
#endif
    if (gameInput != nullptr) {
        if (!gamepads) {
            gamepads = std::make_unique<GamepadReader>(log());
        }
        gamepads->read(*gameInput);   // before the game's update reads them
#ifdef __ANDROID__
        // A keyboard's keys (RoloKeys.java collects them; Axmol passes on only a few).
        for (const android::KeyChange& change : android::takeKeys()) {
            if (change.down) {
                gameInput->press(change.key);
            } else {
                gameInput->release(change.key);
            }
        }
#endif
        updateTouchControls();
    }
    if (frameCallback) {
        frameCallback(FrameInfo{report.framesRendered, static_cast<double>(deltaSeconds)});
    }
    ++report.framesRendered;
    updateStats(deltaSeconds);

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
        // Capture what is on screen, then stop. Axmol hands over the pixels
        // after the frame is drawn; Rolo writes the PNG file itself (ADR 0024).
        finishing = true;
        const std::filesystem::path path = options.screenshotPath;
        ax::utils::captureScreen([this, path](ax::RefPtr<ax::Image> image) {
            std::string why = "the picture could not be read from the screen";
            bool ok = false;
            if (image && image->getData() != nullptr && image->getWidth() > 0 && image->getHeight() > 0) {
                // The screen's pixels are red, green, blue, alpha; the file gets
                // the colours only, as a screenshot has nothing behind it.
                const auto count = static_cast<std::size_t>(image->getWidth()) * static_cast<std::size_t>(image->getHeight());
                const std::uint8_t* rgba = image->getData();
                std::vector<std::uint8_t> rgb(count * 3);
                for (std::size_t i = 0; i < count; ++i) {
                    rgb[i * 3] = rgba[i * 4];
                    rgb[i * 3 + 1] = rgba[i * 4 + 1];
                    rgb[i * 3 + 2] = rgba[i * 4 + 2];
                }
                ok = writePngFile(path, rgb.data(), image->getWidth(), image->getHeight(), 3, &why);
            }
            if (ok) {
                log().info(kCategory, "Screenshot saved: " + path.string());
            } else {
                screenshotFailed = true;
                log().error(kCategory, "Screenshot could not be saved to " + path.string() + ": " + why + ".");
            }
            ax::Director::getInstance()->end();
        });
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
    statsNode = nullptr;
    touchNode = nullptr;
    touchStickBase = nullptr;
    touchStickKnob = nullptr;
    touchButtonNodes.clear();
    touchLaidOut = 0;
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
#ifndef __ANDROID__   // (an Android app's name is its own; there is no title bar)
        if (auto* glfwView = dynamic_cast<ax::RenderViewImpl*>(view)) {
            glfwSetWindowTitle(glfwView->getWindow(), title.c_str());
        }
#endif
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

Vec2 Application::viewSize() const noexcept
{
    return m_impl->viewSize.x > 0.0f ? m_impl->viewSize
                                     : Vec2{static_cast<float>(m_impl->presentation.worldWidth),
                                            static_cast<float>(m_impl->presentation.worldHeight)};
}

void Application::setTouchControls(TouchControls* controls, bool mouseActsAsTouch)
{
    m_impl->touchControls = controls;
    m_impl->mouseActsAsTouch = mouseActsAsTouch;
}

void Application::setFrameCallback(std::function<void(const FrameInfo&)> callback)
{
    m_impl->frameCallback = std::move(callback);
}

void Application::setBackgroundCallback(std::function<void(bool)> callback)
{
    m_impl->backgroundCallback = std::move(callback);
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
#ifdef __ANDROID__
    if (impl.uiEnabled()) {
        return Status::failure("The UI layer is not available on Android (it is the editor's; games do not use it).");
    }
#endif
    if (g_backendInUse.exchange(true)) {
        return Status::failure("Only one Rolo Application can run at a time in a process.");
    }

    impl.hasRun = true;
    impl.options = options;
    impl.report = {};

    // --- Run. ----------------------------------------------------------------
    int exitCode = 0;
    const auto start = std::chrono::steady_clock::now();
#ifdef __EMSCRIPTEN__
    // In a web browser a page's code must return to the browser after every
    // frame, so there the backend's run() only arranges for the browser to
    // call its frame function, and comes straight back: the game goes on
    // after this function has returned. What the frames need is therefore
    // made to outlive it (never destroyed: the page is simply closed), and
    // the caller is told the run goes on (RunReport::continuesInBackground),
    // so that it keeps this Application and everything it uses alive too.
    {
        auto* logBridge = new detail::AxmolLogBridge(impl.engine.logger()); // backend logs → our logger
        impl.log().info(kCategory, "Opening window...");
        auto* backend = new BackendApp(impl);
        exitCode = backend->run();
        if (exitCode == 0 && impl.startupError.empty()) {
            impl.report.continuesInBackground = true;
            return Status::success();
        }
        delete backend;
        delete logBridge;
    }
#elif defined(__ANDROID__)
    // Android made the screen and runs the loop (see "Android" above): the
    // game is set up on the screen that is there, and goes on after this
    // function has returned, as in a web browser. What the frames need is
    // made to outlive it the same way.
    {
        auto* logBridge = new detail::AxmolLogBridge(impl.engine.logger()); // backend logs → our logger
        impl.log().info(kCategory, "Starting on the app's screen...");
        g_androidGame = &impl;
        if (impl.startWindow() && impl.startupError.empty()) {
            impl.report.continuesInBackground = true;
            return Status::success();
        }
        g_androidGame = nullptr;
        g_backendInUse = false;
        delete logBridge;
        return Status::failure(impl.startupError.empty() ? std::string("The game could not be set up on the screen.")
                                                         : impl.startupError);
    }
#else
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
#endif
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

#ifdef __EMSCRIPTEN__
// Axmol's web main loop calls this once the game has ended (the "window"
// closed: game.quit(), or Escape where that quits). A web page cannot close
// itself, so the page is told, and shows that the game is over
// (player/web/shell.html).
void axmol_wasm_app_exit()
{
    // clang-format off
    EM_ASM({ if (Module["onRoloGameEnded"]) { Module["onRoloGameEnded"](); } });
    // clang-format on
}

#endif
