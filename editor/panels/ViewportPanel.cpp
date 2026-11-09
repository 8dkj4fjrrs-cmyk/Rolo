#include "Panels.hpp"

#include "../model/Commands.hpp"

#include "rolo/gameplay/Collision.hpp"
#include "rolo/ui/Context.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <unordered_set>
#include <array>
#include <cmath>
#include <string>

namespace rolo::editor {

namespace {

constexpr float kMinZoom = 0.05f;    // screen points per world point
constexpr float kMaxZoom = 32.0f;
constexpr float kGizmoLength = 64.0f; // screen points
constexpr float kHandleSize = 9.0f;
constexpr float kPi = 3.14159265358979f;

float length(ui::Vec2 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

ui::Vec2 normalized(ui::Vec2 v)
{
    const float l = length(v);
    return l > 1e-5f ? ui::Vec2{v.x / l, v.y / l} : ui::Vec2{1.0f, 0.0f};
}

// Distance from p to the segment a-b.
float distanceToSegment(ui::Vec2 p, ui::Vec2 a, ui::Vec2 b)
{
    const ui::Vec2 ab{b.x - a.x, b.y - a.y};
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    const float t = len2 > 0.0f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
    return length({p.x - (a.x + ab.x * t), p.y - (a.y + ab.y * t)});
}

// The grid spacing (world points, a power of two) that keeps lines at least
// ~20 screen points apart at this zoom.
float gridStep(float zoom)
{
    float step = 1.0f;
    while (step * zoom < 20.0f && step < 65536.0f) {
        step *= 2.0f;
    }
    return step;
}

float roundTo(float value, float step)
{
    return step > 0.0f ? std::round(value / step) * step : value;
}

std::string percentText(float zoom)
{
    return std::to_string(static_cast<int>(std::lround(zoom * 100.0f))) + "%";
}

// The size property of the component that gives an entity its bounds.
const char* sizeComponentOf(const Entity& entity)
{
    if (entity.get<Shape>() != nullptr) {
        return "rolo.Shape";
    }
    if (entity.get<Sprite>() != nullptr) {
        return "rolo.Sprite";
    }
    return nullptr;
}

// The editor's keys as the game's keys (play mode).
Key toGameKey(ui::Key key)
{
    const auto k = static_cast<int>(key);
    if (key >= ui::Key::A && key <= ui::Key::Z) {
        return static_cast<Key>(static_cast<int>(Key::A) + (k - static_cast<int>(ui::Key::A)));
    }
    if (key >= ui::Key::Num0 && key <= ui::Key::Num9) {
        return static_cast<Key>(static_cast<int>(Key::Num0) + (k - static_cast<int>(ui::Key::Num0)));
    }
    if (key >= ui::Key::F1 && key <= ui::Key::F12) {
        return static_cast<Key>(static_cast<int>(Key::F1) + (k - static_cast<int>(ui::Key::F1)));
    }
    switch (key) {
    case ui::Key::Space: return Key::Space;
    case ui::Key::Enter: return Key::Enter;
    case ui::Key::Escape: return Key::Escape;
    case ui::Key::Tab: return Key::Tab;
    case ui::Key::Backspace: return Key::Backspace;
    case ui::Key::Left: return Key::Left;
    case ui::Key::Right: return Key::Right;
    case ui::Key::Up: return Key::Up;
    case ui::Key::Down: return Key::Down;
    case ui::Key::LeftShift: return Key::LeftShift;
    case ui::Key::RightShift: return Key::RightShift;
    case ui::Key::LeftControl: return Key::LeftControl;
    case ui::Key::RightControl: return Key::RightControl;
    case ui::Key::LeftAlt: return Key::LeftAlt;
    case ui::Key::RightAlt: return Key::RightAlt;
    default: return Key::Unknown;
    }
}

std::optional<rolo::MouseButton> toGameButton(ui::MouseButton button)
{
    switch (button) {
    case ui::MouseButton::Left: return rolo::MouseButton::Left;
    case ui::MouseButton::Right: return rolo::MouseButton::Right;
    case ui::MouseButton::Middle: return rolo::MouseButton::Middle;
    }
    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------
// SceneView: the world image plus the editor camera, grid, selection and tools.
// ---------------------------------------------------------------------------
class ViewportPanel::SceneView final : public ui::Widget {
public:
    SceneView(EditorServices& editor, ViewportPanel& panel)
        : m_editor(editor)
        , m_panel(panel)
    {
        setStretch(1.0f);
        setFocusable(true);
    }

    // --- Camera ------------------------------------------------------------------------
    Vec2 center() const noexcept { return m_center; }
    float zoom() const noexcept { return m_zoom; }

    void setCamera(Vec2 center, float zoom)
    {
        m_center = center;
        m_zoom = zoom;
    }

    // World coordinates (y up) <-> panel points (y down).
    ui::Vec2 toScreen(Vec2 world) const
    {
        const ui::Vec2 c = bounds().center();
        return {c.x + (world.x - m_center.x) * m_zoom, c.y - (world.y - m_center.y) * m_zoom};
    }
    Vec2 toWorld(ui::Vec2 screen) const
    {
        const ui::Vec2 c = bounds().center();
        return {m_center.x + (screen.x - c.x) / m_zoom, m_center.y - (screen.y - c.y) / m_zoom};
    }

    void setZoomAround(float zoom, ui::Vec2 screenPoint)
    {
        const Vec2 anchor = toWorld(screenPoint);
        m_zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
        const ui::Vec2 c = bounds().center();
        m_center = {anchor.x - (screenPoint.x - c.x) / m_zoom, anchor.y + (screenPoint.y - c.y) / m_zoom};
    }

    /// Shows the world rectangle (min corner, size) with a margin.
    void showArea(Vec2 min, Vec2 size, float margin = 0.9f, float maxZoom = kMaxZoom)
    {
        const ui::Rect b = bounds();
        if (b.width <= 0.0f || b.height <= 0.0f) {
            m_pendingFit = true;
            return;
        }
        const float sx = size.x > 0.0f ? b.width / size.x : kMaxZoom;
        const float sy = size.y > 0.0f ? b.height / size.y : kMaxZoom;
        m_zoom = std::clamp(std::min(sx, sy) * margin, kMinZoom, maxZoom);
        m_center = {min.x + size.x * 0.5f, min.y + size.y * 0.5f};
    }

    /// The area the player sees: the scene's camera, or (0, 0) to the game's resolution.
    std::pair<Vec2, Vec2> gameArea() const
    {
        if (m_gameCamera) {
            const Vec2 size{m_worldWidth / m_gameCamera->zoom, m_worldHeight / m_gameCamera->zoom};
            return {{m_gameCamera->center.x - size.x * 0.5f, m_gameCamera->center.y - size.y * 0.5f}, size};
        }
        return {{0.0f, 0.0f}, {m_worldWidth, m_worldHeight}};
    }

    void fitGameView()
    {
        const auto [min, size] = gameArea();
        showArea(min, size);
    }

    void frameSelection()
    {
        const Scene& scene = m_editor.document().scene();
        const EntityId id = m_editor.selection();
        if (id.isNull() || !scene.contains(id)) {
            fitGameView();
            return;
        }
        if (const auto corners = scene.worldCorners(id)) {
            Vec2 lo = (*corners)[0];
            Vec2 hi = lo;
            for (const Vec2& p : *corners) {
                lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
                hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
            }
            showArea(lo, hi - lo, 0.5f, 8.0f);
        } else {
            m_center = scene.worldTransform(id).apply({0.0f, 0.0f});
        }
    }

    void setFrameData(const WorldImage& image, float worldWidth, float worldHeight,
                      std::optional<GameCameraView> camera)
    {
        m_image = image;
        m_worldWidth = worldWidth;
        m_worldHeight = worldHeight;
        m_gameCamera = camera;
    }

    // --- Layout and painting ------------------------------------------------------------
    void arrange(ui::Context& ctx, ui::Rect rect) override
    {
        Widget::arrange(ctx, rect);
        if (m_pendingFit && !rect.isEmpty()) {
            m_pendingFit = false;
            fitGameView();
        }
    }

    void paint(ui::Context& ctx, ui::DrawList& draw) override
    {
        const ui::Theme& t = ctx.theme();
        const ui::Rect b = bounds();
        draw.fillRect(b, ui::Color::hex(0x1B1B1D));
        draw.pushClip(b);

        // The world image is rendered at this panel's size; until a resize has
        // been rendered, show it at its own size, centred.
        if (m_image.texture != 0) {
            const ui::Vec2 c = b.center();
            const ui::Rect r{std::round(c.x - m_image.size.width * 0.5f), std::round(c.y - m_image.size.height * 0.5f),
                             m_image.size.width, m_image.size.height};
            draw.image(m_image.texture, r, m_image.uv0, m_image.uv1);
        }
        if (m_editor.isPlaying()) {
            paintPlaying(ctx, draw);
            draw.popClip();
            return;
        }
        paintGrid(draw);
        paintGameFrame(ctx, draw);
        paintTilemap(ctx, draw);
        paintColliders(draw);
        paintSelection(ctx, draw);
        draw.popClip();
        if (isFocused()) {
            draw.strokeRoundedRect(b.inset(1.0f), 0.0f, t.focusRing.fade(0.5f));
        }
    }

    // While the game runs: only what the player sees, with the rest dimmed,
    // and a reminder of where the keyboard goes.
    void paintPlaying(ui::Context& ctx, ui::DrawList& draw) const
    {
        const ui::Theme& t = ctx.theme();
        const ui::Rect b = bounds();
        const auto [min, size] = gameArea();
        const ui::Vec2 a = toScreen(min);
        const ui::Vec2 c = toScreen(min + size);
        const ui::Rect game{std::round(a.x), std::round(c.y), std::round(c.x - a.x), std::round(a.y - c.y)};
        const ui::Color shade = ui::Color::hex(0x141416);
        draw.fillRect({b.x, b.y, b.width, game.y - b.y}, shade);
        draw.fillRect({b.x, game.bottom(), b.width, b.bottom() - game.bottom()}, shade);
        draw.fillRect({b.x, game.y, game.x - b.x, game.height}, shade);
        draw.fillRect({game.right(), game.y, b.right() - game.right(), game.height}, shade);
        const ui::Color accent = m_editor.isPaused() ? ui::Color::hex(0x8A8A8A) : t.accent;
        draw.strokeRoundedRect(b.inset(1.0f), 0.0f, accent, 2.0f);
        std::string note;
        if (m_editor.isPaused()) {
            note = "Paused";
        } else if (!isFocused()) {
            note = "Click the game to give it the keyboard";
        }
        if (!note.empty()) {
            const ui::Rect bar{b.x + 8.0f, b.y + 8.0f, 270.0f, 24.0f};
            draw.fillRoundedRect(bar, 4.0f, ui::Color{0, 0, 0, 170});
            draw.text(t.font, t.smallFontSize, {bar.x + 10.0f, bar.y + 5.0f}, ui::Color{255, 255, 255, 220}, note);
        }
    }

    // --- Input ------------------------------------------------------------------------------
    bool onScroll(ui::Context& ctx, const ui::ScrollEvent& event) override
    {
        if (m_editor.isPlaying()) {
            return true; // the view follows the game's camera
        }
        // Up/away zooms in, around the mouse; a wheel notch is about 20%.
        const float factor = std::clamp(std::pow(1.005f, event.delta.y), 0.5f, 2.0f);
        setZoomAround(m_zoom * factor, ctx.pointerPosition());
        return true;
    }

    bool onPointerDown(ui::Context& ctx, const ui::PointerEvent& event) override
    {
        ctx.setFocus(this);
        if (Input* input = m_editor.gameInput()) {
            input->setPointer(toWorld(event.position));
            if (const auto button = toGameButton(event.button)) {
                input->press(*button);
            }
            return true;
        }
        m_pressScreen = event.position;
        const bool panButton = event.button == ui::MouseButton::Right || event.button == ui::MouseButton::Middle
                               || (event.button == ui::MouseButton::Left && event.modifiers.alt);
        if (panButton) {
            m_drag = Drag::Pan;
            m_panStartCenter = m_center;
            return true;
        }
        if (event.button != ui::MouseButton::Left) {
            return false;
        }
        // Tile tools paint on the selected tilemap instead of selecting.
        if (isTileTool(m_panel.tool()) && selectedMap()) {
            beginPaint(event.position);
            return true;
        }
        const Scene& scene = m_editor.document().scene();
        // A handle of the selected object's tool?
        const Handle handle = handleAt(event.position);
        if (handle != Handle::None) {
            beginTransformDrag(handle, event.position);
            return true;
        }
        // Otherwise select what was clicked (or nothing), and drag it to move it.
        const EntityId hit = scene.hitTest(toWorld(event.position));
        m_editor.select(hit);
        if (!hit.isNull()) {
            beginTransformDrag(Handle::Body, event.position);
        }
        return true;
    }

    void onPointerMove(ui::Context&, const ui::PointerEvent& event) override
    {
        if (Input* input = m_editor.gameInput()) {
            input->setPointer(toWorld(event.position));
            return;
        }
        if (m_drag == Drag::Pan) {
            m_center = {m_panStartCenter.x - (event.position.x - m_pressScreen.x) / m_zoom,
                        m_panStartCenter.y + (event.position.y - m_pressScreen.y) / m_zoom};
        } else if (m_drag == Drag::Paint) {
            paintTo(event.position);
        } else if (m_drag == Drag::Transform) {
            // A click on an object should select it, not nudge it: wait for a real drag.
            if (!m_moved && length({event.position.x - m_pressScreen.x, event.position.y - m_pressScreen.y}) < 3.0f) {
                return;
            }
            m_moved = true;
            dragTo(event.position, event.modifiers);
        }
    }

    bool onPointerUp(ui::Context&, const ui::PointerEvent& event) override
    {
        if (Input* input = m_editor.gameInput()) {
            input->setPointer(toWorld(event.position));
            if (const auto button = toGameButton(event.button)) {
                input->release(*button);
            }
            return true;
        }
        if ((m_drag == Drag::Transform && m_moved) || m_drag == Drag::Paint) {
            m_editor.endEdit();
        }
        m_drag = Drag::None;
        m_moved = false;
        return true;
    }

    bool onKeyDown(ui::Context& ctx, const ui::KeyEvent& event) override
    {
        const ui::Modifiers& m = event.modifiers;
        if (Input* input = m_editor.gameInput()) {
            // The game gets the keyboard, except editor shortcuts (⌘/Ctrl + key).
            const bool shortcut = (m.ctrl || m.super) && event.key != ui::Key::LeftControl
                                  && event.key != ui::Key::RightControl && event.key != ui::Key::LeftSuper
                                  && event.key != ui::Key::RightSuper;
            const Key key = toGameKey(event.key);
            if (shortcut || key == Key::Unknown) {
                return false;
            }
            input->press(key);
            return true;
        }
        if (m.ctrl || m.super || m.alt) {
            return false;
        }
        switch (event.key) {
        case ui::Key::W: m_panel.setTool(SceneTool::Move); return true;
        case ui::Key::E: m_panel.setTool(SceneTool::Rotate); return true;
        case ui::Key::R: m_panel.setTool(SceneTool::Scale); return true;
        case ui::Key::T: m_panel.setTool(SceneTool::Rect); return true;
        case ui::Key::B:
        case ui::Key::X:
        case ui::Key::G:
        case ui::Key::I:
            if (selectedMap()) {
                m_panel.setTool(event.key == ui::Key::B   ? SceneTool::Brush
                                : event.key == ui::Key::X ? SceneTool::Eraser
                                : event.key == ui::Key::G ? SceneTool::Fill
                                                          : SceneTool::Picker);
                return true;
            }
            return false;
        case ui::Key::F: frameSelection(); return true;
        case ui::Key::Delete:
        case ui::Key::Backspace: m_editor.deleteSelection(); return true;
        case ui::Key::Left: nudge({m.shift ? -10.0f : -1.0f, 0.0f}); return true;
        case ui::Key::Right: nudge({m.shift ? 10.0f : 1.0f, 0.0f}); return true;
        case ui::Key::Up: nudge({0.0f, m.shift ? 10.0f : 1.0f}); return true;
        case ui::Key::Down: nudge({0.0f, m.shift ? -10.0f : -1.0f}); return true;
        default: break;
        }
        static_cast<void>(ctx);
        return false;
    }

    bool onKeyUp(ui::Context&, const ui::KeyEvent& event) override
    {
        if (Input* input = m_editor.gameInput()) {
            const Key key = toGameKey(event.key);
            if (key != Key::Unknown) {
                input->release(key);
                return true;
            }
        }
        return false;
    }

    void onFocusChanged(ui::Context&, bool focused) override
    {
        // Keys let go while another panel has focus would otherwise stay down.
        if (!focused) {
            if (Input* input = m_editor.gameInput()) {
                input->releaseAll();
            }
        }
    }

    ui::CursorShape cursor(ui::Vec2 point) const override
    {
        if (m_editor.isPlaying()) {
            return ui::CursorShape::Arrow;
        }
        if (m_drag == Drag::Pan) {
            return ui::CursorShape::Hand;
        }
        return handleAt(point) != Handle::None ? ui::CursorShape::Hand : ui::CursorShape::Arrow;
    }

    std::string tooltip() const override
    {
        return {}; // the tools explain themselves in the toolbar
    }

    /// The selected tilemap's tileset and image, for overlays and the brush preview.
    void setTileImage(ui::TextureId texture, ui::Size imagePixels, std::optional<Tileset> tileset)
    {
        m_tileTexture = texture;
        m_tileImageSize = imagePixels;
        m_tileset = std::move(tileset);
    }

    struct MapRef {
        EntityId id;
        const Tilemap* map = nullptr;
        Affine2 world;
    };
    std::optional<MapRef> selectedMap() const
    {
        const Scene& scene = m_editor.document().scene();
        const Entity* e = scene.find(m_editor.selection());
        const Tilemap* map = e != nullptr ? e->get<Tilemap>() : nullptr;
        if (map == nullptr) {
            return std::nullopt;
        }
        return MapRef{e->id(), map, scene.worldTransform(e->id())};
    }

private:
    enum class Drag { None, Pan, Transform, Paint };

    // --- Tile painting -------------------------------------------------------------------------
    std::optional<TileCell> cellAtScreen(const MapRef& m, ui::Vec2 screen) const
    {
        const auto inverse = m.world.inverse();
        if (!inverse) {
            return std::nullopt;
        }
        return m.map->cellAt(inverse->apply(toWorld(screen)));
    }

    int paintLayer(const Tilemap& map) const
    {
        const int layer = m_editor.tileLayer();
        if (layer == PaintTiles::kCollisionLayer) {
            return layer;
        }
        return std::clamp(layer, 0, static_cast<int>(map.layers.size()) - 1);
    }

    static int readValue(const Tilemap& map, int layer, TileCell cell)
    {
        return layer == PaintTiles::kCollisionLayer ? (map.solid.count(cell) > 0 ? 1 : 0)
                                                    : map.tileAt(static_cast<std::size_t>(layer), cell);
    }

    void applyCells(const std::vector<TileCell>& cells, bool erase, const char* verb = nullptr)
    {
        const auto m = selectedMap();
        if (!m || cells.empty()) {
            return;
        }
        const int layer = paintLayer(*m->map);
        const bool collision = layer == PaintTiles::kCollisionLayer;
        const int value = erase ? (collision ? 0 : -1) : (collision ? 1 : m_editor.tileBrush());
        std::vector<PaintTiles::Cell> paint;
        paint.reserve(cells.size());
        for (const TileCell& c : cells) {
            paint.push_back({c, value});
        }
        const std::string label = std::string(verb != nullptr ? verb : (erase ? "Erase" : "Paint"))
                                  + (collision ? " Collision" : " Tiles");
        m_editor.execute(std::make_unique<PaintTiles>(m->id, layer, std::move(paint), label));
    }

    void beginPaint(ui::Vec2 screen)
    {
        const auto m = selectedMap();
        const auto cell = m ? cellAtScreen(*m, screen) : std::nullopt;
        if (!cell) {
            return;
        }
        m_editor.endEdit(); // a new stroke is a new undo step
        switch (m_panel.tool()) {
        case SceneTool::Picker: {
            int layer = paintLayer(*m->map);
            int tile = layer >= 0 ? m->map->tileAt(static_cast<std::size_t>(layer), *cell) : -1;
            // On the collision layer (or an empty cell), take the top visible tile.
            for (int i = static_cast<int>(m->map->layers.size()) - 1; tile < 0 && i >= 0; --i) {
                if (m->map->layers[static_cast<std::size_t>(i)].visible) {
                    tile = m->map->tileAt(static_cast<std::size_t>(i), *cell);
                }
            }
            if (tile >= 0) {
                m_editor.setTileBrush(tile);
                m_panel.setTool(SceneTool::Brush);
            }
            return;
        }
        case SceneTool::Fill:
            fill(*m, *cell);
            m_editor.endEdit();
            return;
        default:
            m_drag = Drag::Paint;
            m_lastCell = *cell;
            applyCells({*cell}, m_panel.tool() == SceneTool::Eraser);
            return;
        }
    }

    void paintTo(ui::Vec2 screen)
    {
        const auto m = selectedMap();
        const auto cell = m ? cellAtScreen(*m, screen) : std::nullopt;
        if (!cell || *cell == m_lastCell) {
            return;
        }
        // Every cell on the line from the last one, so fast strokes leave no gaps.
        std::vector<TileCell> cells;
        int x = m_lastCell.x;
        int y = m_lastCell.y;
        const int dx = std::abs(cell->x - x);
        const int dy = -std::abs(cell->y - y);
        const int sx = x < cell->x ? 1 : -1;
        const int sy = y < cell->y ? 1 : -1;
        int err = dx + dy;
        while (!(x == cell->x && y == cell->y) && cells.size() < 4096) {
            const int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y += sy;
            }
            cells.push_back({x, y});
        }
        m_lastCell = *cell;
        applyCells(cells, m_panel.tool() == SceneTool::Eraser);
    }

    // Cells whose centres are inside the view (at most 256 x 256 around `around`).
    std::pair<TileCell, TileCell> visibleCells(const MapRef& m, TileCell around) const
    {
        const ui::Rect b = bounds();
        TileCell lo{INT32_MAX, INT32_MAX};
        TileCell hi{INT32_MIN, INT32_MIN};
        for (const ui::Vec2 corner : {ui::Vec2{b.x, b.y}, ui::Vec2{b.right(), b.y}, ui::Vec2{b.x, b.bottom()},
                                      ui::Vec2{b.right(), b.bottom()}}) {
            if (const auto c = cellAtScreen(m, corner)) {
                lo = {std::min(lo.x, c->x), std::min(lo.y, c->y)};
                hi = {std::max(hi.x, c->x), std::max(hi.y, c->y)};
            }
        }
        lo = {std::max(lo.x, around.x - 128), std::max(lo.y, around.y - 128)};
        hi = {std::min(hi.x, around.x + 128), std::min(hi.y, around.y + 128)};
        return {lo, hi};
    }

    // Fill: the connected cells holding the same value as the clicked one,
    // up to the edges of the view.
    void fill(const MapRef& m, TileCell start)
    {
        const int layer = paintLayer(*m.map);
        const int target = readValue(*m.map, layer, start);
        const bool collision = layer == PaintTiles::kCollisionLayer;
        const int replacement = collision ? 1 : m_editor.tileBrush();
        if (target == replacement) {
            return;
        }
        const auto [lo, hi] = visibleCells(m, start);
        std::vector<TileCell> found;
        std::vector<TileCell> todo{start};
        std::unordered_set<TileCell, TileCellHash> seen{start};
        while (!todo.empty() && found.size() < 65536) {
            const TileCell c = todo.back();
            todo.pop_back();
            found.push_back(c);
            for (const TileCell n : {TileCell{c.x + 1, c.y}, TileCell{c.x - 1, c.y}, TileCell{c.x, c.y + 1},
                                     TileCell{c.x, c.y - 1}}) {
                if (n.x < lo.x || n.y < lo.y || n.x > hi.x || n.y > hi.y || seen.count(n) > 0) {
                    continue;
                }
                seen.insert(n);
                if (readValue(*m.map, layer, n) == target) {
                    todo.push_back(n);
                }
            }
        }
        applyCells(found, false, "Fill");
    }

    // --- Tile overlays ------------------------------------------------------------------------
    std::array<ui::Vec2, 4> cellQuad(const MapRef& m, TileCell c) const
    {
        const float x0 = static_cast<float>(c.x) * m.map->cellSize.x;
        const float y0 = static_cast<float>(c.y) * m.map->cellSize.y;
        const float x1 = x0 + m.map->cellSize.x;
        const float y1 = y0 + m.map->cellSize.y;
        // Bottom-left, top-left, top-right, bottom-right in the world: clockwise on screen.
        return {toScreen(m.world.apply({x0, y0})), toScreen(m.world.apply({x0, y1})), toScreen(m.world.apply({x1, y1})),
                toScreen(m.world.apply({x1, y0}))};
    }

    static void fillQuad(ui::DrawList& draw, std::array<ui::Vec2, 4> q, ui::Color color)
    {
        const float cross = (q[1].x - q[0].x) * (q[2].y - q[0].y) - (q[1].y - q[0].y) * (q[2].x - q[0].x);
        if (cross < 0.0f) {
            std::swap(q[1], q[3]); // mirrored (negative scale): reverse the order
        }
        draw.fillConvexPolygon(q, color);
    }

    void paintTilemap(ui::Context& ctx, ui::DrawList& draw) const
    {
        const auto m = selectedMap();
        if (!m) {
            return;
        }
        const ui::Theme& t = ctx.theme();
        const float cellPixels = std::min(m->map->cellSize.x, m->map->cellSize.y) * m_zoom;
        const auto centre = cellAtScreen(*m, bounds().center()).value_or(TileCell{});
        const auto [lo, hi] = visibleCells(*m, centre);
        const bool painting = isTileTool(m_panel.tool());

        // The tile grid, when cells are big enough to see.
        if (cellPixels >= 6.0f && hi.x >= lo.x && hi.y >= lo.y) {
            const ui::Color line{255, 255, 255, static_cast<std::uint8_t>(painting ? 34 : 18)};
            const float cw = m->map->cellSize.x;
            const float ch = m->map->cellSize.y;
            for (int x = lo.x; x <= hi.x + 1; ++x) {
                draw.line(toScreen(m->world.apply({static_cast<float>(x) * cw, static_cast<float>(lo.y) * ch})),
                          toScreen(m->world.apply({static_cast<float>(x) * cw, static_cast<float>(hi.y + 1) * ch})), line);
            }
            for (int y = lo.y; y <= hi.y + 1; ++y) {
                draw.line(toScreen(m->world.apply({static_cast<float>(lo.x) * cw, static_cast<float>(y) * ch})),
                          toScreen(m->world.apply({static_cast<float>(hi.x + 1) * cw, static_cast<float>(y) * ch})), line);
            }
        }

        // Collision: painted cells strong red, cells blocked by solid tiles lighter.
        const bool collisionActive = m_editor.tileLayer() == PaintTiles::kCollisionLayer;
        const std::uint8_t strong = collisionActive ? 120 : 70;
        const std::uint8_t light = collisionActive ? 70 : 40;
        const long area = static_cast<long>(hi.x - lo.x + 1) * static_cast<long>(hi.y - lo.y + 1);
        if (area > 0 && area <= 65536) {
            const Tileset* tileset = m_tileset ? &*m_tileset : nullptr;
            for (int y = lo.y; y <= hi.y; ++y) {
                for (int x = lo.x; x <= hi.x; ++x) {
                    const TileCell c{x, y};
                    if (m->map->solid.count(c) > 0) {
                        fillQuad(draw, cellQuad(*m, c), ui::Color{235, 64, 64, strong});
                    } else if (isTileSolid(*m->map, tileset, c)) {
                        fillQuad(draw, cellQuad(*m, c), ui::Color{235, 64, 64, light});
                    }
                }
            }
        }

        // The cell under the mouse, with a preview of the brush tile.
        if (painting && isHovered() && m_drag != Drag::Pan) {
            if (const auto cell = cellAtScreen(*m, ctx.pointerPosition())) {
                const auto q = cellQuad(*m, *cell);
                const bool upright = std::abs(q[0].x - q[1].x) < 0.5f && std::abs(q[1].y - q[2].y) < 0.5f;
                const bool brush = m_panel.tool() == SceneTool::Brush || m_panel.tool() == SceneTool::Fill;
                if (brush && !collisionActive && upright && m_tileTexture != 0 && m_tileset
                    && m_tileImageSize.width > 0.0f) {
                    const TileRect r = m_tileset->tileRect(m_editor.tileBrush(), static_cast<int>(m_tileImageSize.width));
                    const ui::Rect rect{std::min(q[0].x, q[2].x), std::min(q[0].y, q[2].y), std::abs(q[2].x - q[0].x),
                                        std::abs(q[0].y - q[2].y)};
                    draw.image(m_tileTexture, rect,
                               {static_cast<float>(r.x) / m_tileImageSize.width, static_cast<float>(r.y) / m_tileImageSize.height},
                               {static_cast<float>(r.x + r.width) / m_tileImageSize.width,
                                static_cast<float>(r.y + r.height) / m_tileImageSize.height},
                               ui::Color{255, 255, 255, 170});
                }
                const ui::Color outline = m_panel.tool() == SceneTool::Eraser ? ui::Color::hex(0xF26B65) : t.accent;
                draw.polyline(q, outline, 1.5f, true);
            }
        }
    }
    enum class Handle {
        None,
        Body,            // the object itself: move freely
        MoveX, MoveY, MoveXY,
        Rotate,
        ScaleX, ScaleY, ScaleXY,
        // Rect tool: corners and edges, in worldCorners() order where possible.
        RectBL, RectBR, RectTR, RectTL, RectB, RectR, RectT, RectL,
    };

    struct DragStart {
        EntityId id;
        Handle handle = Handle::None;
        Vec2 pointerWorld;
        Vec2 position;
        float rotation = 0.0f;
        Vec2 scale;
        Vec2 size;
        const char* sizeComponent = nullptr;
        Affine2 world;           // the entity's world transform
        Affine2 parentInverse;   // world -> parent space
        Vec2 worldOrigin;
        ui::Vec2 screenOrigin;
        float startAngle = 0.0f; // Rotate: screen angle of the pointer
    };

    // --- Gizmo geometry (screen space) --------------------------------------------------------
    struct Gizmo {
        ui::Vec2 origin;
        ui::Vec2 axisX;   // unit directions on screen
        ui::Vec2 axisY;
    };

    std::optional<Gizmo> gizmo() const
    {
        if (isTileTool(m_panel.tool())) {
            return std::nullopt; // painting, not transforming
        }
        const Scene& scene = m_editor.document().scene();
        const EntityId id = m_editor.selection();
        if (id.isNull() || !scene.contains(id)) {
            return std::nullopt;
        }
        const Affine2 world = scene.worldTransform(id);
        Gizmo g;
        g.origin = toScreen(world.apply({0.0f, 0.0f}));
        if (m_panel.tool() == SceneTool::Scale) {
            // Scale works along the object's own axes: its local x axis is
            // (a, b) in the world, its y axis (c, d); screen y points down.
            g.axisX = normalized({world.a, -world.b});
            g.axisY = normalized({world.c, -world.d});
        } else {
            g.axisX = {1.0f, 0.0f};
            g.axisY = {0.0f, -1.0f}; // world up = screen up
        }
        return g;
    }

    std::vector<std::pair<Handle, ui::Vec2>> rectHandles() const
    {
        const Scene& scene = m_editor.document().scene();
        const Entity* e = scene.find(m_editor.selection());
        const auto corners = scene.worldCorners(m_editor.selection());
        if (!corners || e == nullptr || sizeComponentOf(*e) == nullptr) {
            return {}; // only shapes and sprites have a size to drag
        }
        std::array<ui::Vec2, 4> s{};
        for (std::size_t i = 0; i < 4; ++i) {
            s[i] = toScreen((*corners)[i]);
        }
        const auto mid = [](ui::Vec2 a, ui::Vec2 b) { return ui::Vec2{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}; };
        return {{Handle::RectBL, s[0]},           {Handle::RectBR, s[1]},           {Handle::RectTR, s[2]},
                {Handle::RectTL, s[3]},           {Handle::RectB, mid(s[0], s[1])}, {Handle::RectR, mid(s[1], s[2])},
                {Handle::RectT, mid(s[2], s[3])}, {Handle::RectL, mid(s[3], s[0])}};
    }

    Handle handleAt(ui::Vec2 p) const
    {
        const auto g = gizmo();
        if (!g) {
            return Handle::None;
        }
        const ui::Vec2 o = g->origin;
        const auto end = [&](ui::Vec2 axis) { return ui::Vec2{o.x + axis.x * kGizmoLength, o.y + axis.y * kGizmoLength}; };
        const auto near = [&](ui::Vec2 q, float r) { return length({p.x - q.x, p.y - q.y}) <= r; };
        switch (m_panel.tool()) {
        case SceneTool::Move:
            if (near({o.x + 12.0f, o.y - 12.0f}, 9.0f)) {
                return Handle::MoveXY;
            }
            if (distanceToSegment(p, {o.x + 14.0f, o.y}, end(g->axisX)) <= 6.0f) {
                return Handle::MoveX;
            }
            if (distanceToSegment(p, {o.x, o.y - 14.0f}, end(g->axisY)) <= 6.0f) {
                return Handle::MoveY;
            }
            break;
        case SceneTool::Rotate: {
            const float d = length({p.x - o.x, p.y - o.y});
            if (std::abs(d - kGizmoLength) <= 7.0f) {
                return Handle::Rotate;
            }
            break;
        }
        case SceneTool::Scale:
            if (near(o, 9.0f)) {
                return Handle::ScaleXY;
            }
            if (near(end(g->axisX), 9.0f) || distanceToSegment(p, o, end(g->axisX)) <= 5.0f) {
                return Handle::ScaleX;
            }
            if (near(end(g->axisY), 9.0f) || distanceToSegment(p, o, end(g->axisY)) <= 5.0f) {
                return Handle::ScaleY;
            }
            break;
        case SceneTool::Rect:
            for (const auto& [h, pos] : rectHandles()) {
                if (near(pos, kHandleSize)) {
                    return h;
                }
            }
            break;
        default:
            break;
        }
        return Handle::None;
    }

    // --- Dragging -----------------------------------------------------------------------------
    void beginTransformDrag(Handle handle, ui::Vec2 screen)
    {
        const Scene& scene = m_editor.document().scene();
        const EntityId id = m_editor.selection();
        const Entity* e = scene.find(id);
        if (e == nullptr) {
            return;
        }
        m_editor.endEdit(); // a new drag is a new undo step
        DragStart d;
        d.id = id;
        d.handle = handle;
        d.pointerWorld = toWorld(screen);
        const Transform& t = e->transform();
        d.position = t.position;
        d.rotation = t.rotation;
        d.scale = t.scale;
        d.world = scene.worldTransform(id);
        const Affine2 parentWorld = e->parent().isNull() ? Affine2{} : scene.worldTransform(e->parent());
        d.parentInverse = parentWorld.inverse().value_or(Affine2{});
        d.worldOrigin = d.world.apply({0.0f, 0.0f});
        d.screenOrigin = toScreen(d.worldOrigin);
        d.startAngle = std::atan2(screen.y - d.screenOrigin.y, screen.x - d.screenOrigin.x);
        d.sizeComponent = sizeComponentOf(*e);
        if (const auto bounds = localBounds(*e)) {
            d.size = bounds->size;
        }
        m_start = d;
        m_drag = Drag::Transform;
        m_moved = false;
    }

    void dragTo(ui::Vec2 screen, const ui::Modifiers& mods)
    {
        const DragStart& d = m_start;
        const Entity* e = m_editor.document().scene().find(d.id);
        if (e == nullptr) {
            m_drag = Drag::None;
            return;
        }
        const std::string what = e->name().empty() ? std::string("Object") : e->name();
        const Vec2 world = toWorld(screen);
        Vec2 delta = world - d.pointerWorld;
        using C = SetProperties::Change;

        switch (d.handle) {
        case Handle::Body:
        case Handle::MoveXY:
        case Handle::MoveX:
        case Handle::MoveY: {
            if (d.handle == Handle::MoveX) {
                delta.y = 0.0f;
            } else if (d.handle == Handle::MoveY) {
                delta.x = 0.0f;
            }
            Vec2 origin = d.worldOrigin + delta;
            // Whole points; with Shift, the grid.
            const float step = mods.shift ? gridStep(m_zoom) : 1.0f;
            origin = {roundTo(origin.x, step), roundTo(origin.y, step)};
            const Vec2 position = d.parentInverse.apply(origin);
            m_editor.execute(std::make_unique<SetProperties>(
                d.id, std::vector<C>{{"rolo.Transform", "position", position}}, "Move " + what));
            break;
        }
        case Handle::Rotate: {
            const float angle = std::atan2(screen.y - d.screenOrigin.y, screen.x - d.screenOrigin.x);
            // Screen angles grow clockwise (y points down), like Rolo's rotation.
            float degrees = d.rotation + (angle - d.startAngle) * 180.0f / kPi;
            degrees = std::remainder(degrees, 360.0f);
            degrees = mods.shift ? roundTo(degrees, 15.0f) : roundTo(degrees, 0.1f);
            m_editor.execute(std::make_unique<SetProperties>(
                d.id, std::vector<C>{{"rolo.Transform", "rotation", degrees}}, "Rotate " + what));
            break;
        }
        case Handle::ScaleX:
        case Handle::ScaleY:
        case Handle::ScaleXY: {
            const auto g = gizmo();
            if (!g) {
                break;
            }
            const ui::Vec2 fromOrigin{screen.x - d.screenOrigin.x, screen.y - d.screenOrigin.y};
            const ui::Vec2 startFromOrigin{m_pressScreen.x - d.screenOrigin.x, m_pressScreen.y - d.screenOrigin.y};
            Vec2 scale = d.scale;
            const auto along = [](ui::Vec2 v, ui::Vec2 axis) { return v.x * axis.x + v.y * axis.y; };
            if (d.handle == Handle::ScaleXY) {
                const float factor = std::max(0.01f, 1.0f + (screen.x - m_pressScreen.x) / 100.0f);
                scale = {d.scale.x * factor, d.scale.y * factor};
            } else {
                const ui::Vec2 axis = d.handle == Handle::ScaleX ? g->axisX : g->axisY;
                const float start = along(startFromOrigin, axis);
                const float now = along(fromOrigin, axis);
                const float factor = std::abs(start) > 1.0f ? now / start : 1.0f;
                (d.handle == Handle::ScaleX ? scale.x : scale.y) =
                    (d.handle == Handle::ScaleX ? d.scale.x : d.scale.y) * factor;
            }
            const float step = mods.shift ? 0.1f : 0.001f;
            scale = {roundTo(scale.x, step), roundTo(scale.y, step)};
            m_editor.execute(std::make_unique<SetProperties>(
                d.id, std::vector<C>{{"rolo.Transform", "scale", scale}}, "Scale " + what));
            break;
        }
        default: {
            // Rect: move the dragged corner or edge, keep the opposite one in place.
            if (d.sizeComponent == nullptr) {
                break;
            }
            const auto inverse = d.world.inverse();
            if (!inverse) {
                break;
            }
            const Vec2 local = inverse->apply(world);
            float left = -d.size.x * 0.5f;
            float right = d.size.x * 0.5f;
            float bottom = -d.size.y * 0.5f;
            float top = d.size.y * 0.5f;
            const Handle h = d.handle;
            const bool l = h == Handle::RectBL || h == Handle::RectTL || h == Handle::RectL;
            const bool r = h == Handle::RectBR || h == Handle::RectTR || h == Handle::RectR;
            const bool b = h == Handle::RectBL || h == Handle::RectBR || h == Handle::RectB;
            const bool t = h == Handle::RectTL || h == Handle::RectTR || h == Handle::RectT;
            if (l) {
                left = std::min(std::round(local.x), right - 1.0f);
            }
            if (r) {
                right = std::max(std::round(local.x), left + 1.0f);
            }
            if (b) {
                bottom = std::min(std::round(local.y), top - 1.0f);
            }
            if (t) {
                top = std::max(std::round(local.y), bottom + 1.0f);
            }
            Vec2 size{right - left, top - bottom};
            // Shift on a corner keeps the shape's proportions.
            if (mods.shift && (l || r) && (b || t) && d.size.x > 0.0f && d.size.y > 0.0f) {
                const float k = std::max(size.x / d.size.x, size.y / d.size.y);
                size = {std::round(d.size.x * k), std::round(d.size.y * k)};
                if (l) {
                    left = right - size.x;
                } else {
                    right = left + size.x;
                }
                if (b) {
                    bottom = top - size.y;
                } else {
                    top = bottom + size.y;
                }
            }
            const Vec2 centerLocal{(left + right) * 0.5f, (bottom + top) * 0.5f};
            const Vec2 position = d.parentInverse.apply(d.world.apply(centerLocal));
            m_editor.execute(std::make_unique<SetProperties>(
                d.id,
                std::vector<C>{{d.sizeComponent, "size", size}, {"rolo.Transform", "position", position}},
                "Resize " + what));
            break;
        }
        }
    }

    void nudge(Vec2 delta)
    {
        const Scene& scene = m_editor.document().scene();
        const Entity* e = scene.find(m_editor.selection());
        if (e == nullptr) {
            return;
        }
        // In world directions, so arrows move the object on screen as expected.
        const Affine2 parentWorld = e->parent().isNull() ? Affine2{} : scene.worldTransform(e->parent());
        const Affine2 parentInverse = parentWorld.inverse().value_or(Affine2{});
        const Vec2 origin = scene.worldTransform(e->id()).apply({0.0f, 0.0f}) + delta;
        m_editor.execute(std::make_unique<SetProperties>(
            e->id(), std::vector<SetProperties::Change>{{"rolo.Transform", "position", parentInverse.apply(origin)}},
            "Move " + (e->name().empty() ? std::string("Object") : e->name())));
    }

    // --- Painting -----------------------------------------------------------------------------
    void paintGrid(ui::DrawList& draw) const
    {
        const ui::Rect b = bounds();
        const float step = gridStep(m_zoom);
        const Vec2 lo = toWorld({b.x, b.bottom()});
        const Vec2 hi = toWorld({b.right(), b.y});
        const ui::Color minor{255, 255, 255, 10};
        const ui::Color major{255, 255, 255, 22};
        for (float x = std::floor(lo.x / step) * step; x <= hi.x; x += step) {
            const float sx = std::round(toScreen({x, 0.0f}).x) + 0.5f;
            const bool isMajor = std::fmod(std::abs(x), step * 4.0f) < 0.5f;
            draw.line({sx, b.y}, {sx, b.bottom()}, x == 0.0f ? ui::Color{235, 87, 87, 70} : (isMajor ? major : minor));
        }
        for (float y = std::floor(lo.y / step) * step; y <= hi.y; y += step) {
            const float sy = std::round(toScreen({0.0f, y}).y) + 0.5f;
            const bool isMajor = std::fmod(std::abs(y), step * 4.0f) < 0.5f;
            draw.line({b.x, sy}, {b.right(), sy}, y == 0.0f ? ui::Color{110, 200, 90, 70} : (isMajor ? major : minor));
        }
    }

    // Colliders as outlines: green blocks movement, blue is an area (a door,
    // a pick-up) that gameplay code notices.
    // Each collider is drawn as its shape (box, circle or capsule), turned with
    // its object; objects with a Rigid Body are drawn in orange.
    void paintColliders(ui::DrawList& draw) const
    {
        const Scene& scene = m_editor.document().scene();
        scene.forEach([&](const Entity& entity, int) {
            const Collider* collider = entity.get<Collider>();
            if (collider == nullptr || !colliderBox(scene, entity.id())) {
                return;   // none, or switched off
            }
            const Affine2 world = scene.worldTransform(entity.id());
            const std::vector<Vec2> outline = colliderOutline(*collider);
            std::vector<ui::Vec2> points;
            points.reserve(outline.size());
            for (const Vec2 p : outline) {
                points.push_back(toScreen(world.apply(p)));
            }
            const ui::Color color = entity.get<RigidBody>() != nullptr ? ui::Color{242, 169, 59, 220}
                                    : collider->solid                  ? ui::Color{111, 207, 79, 200}
                                                                       : ui::Color{86, 168, 245, 200};
            draw.fillConvexPolygon(points, color.fade(0.12f));
            draw.polyline(points, color, 1.0f, true);
        });
    }

    // A collider's outline in its object's own coordinates (see ColliderShape).
    static std::vector<Vec2> colliderOutline(const Collider& c)
    {
        const float hw = c.size.x * 0.5f;
        const float hh = c.size.y * 0.5f;
        const Vec2 o = c.offset;
        std::vector<Vec2> out;
        if (c.shape == ColliderShape::Box) {
            return {{o.x - hw, o.y - hh}, {o.x + hw, o.y - hh}, {o.x + hw, o.y + hh}, {o.x - hw, o.y + hh}};
        }
        const float r = std::min(hw, hh);
        // A capsule's two end circles sit along its long side; a circle's coincide.
        const Vec2 reach = c.shape == ColliderShape::Circle ? Vec2{} : hh >= hw ? Vec2{0.0f, hh - r} : Vec2{hw - r, 0.0f};
        constexpr int kSteps = 16;   // per half circle
        for (int end = 0; end < 2; ++end) {
            const Vec2 centre = end == 0 ? o + reach : o - reach;
            // Each end draws the half circle facing away from the other.
            const float start = std::atan2(reach.y, reach.x) - 1.5707963f + (end == 0 ? 0.0f : 3.1415927f);
            const float startAngle = (reach.x == 0.0f && reach.y == 0.0f) ? (end == 0 ? 0.0f : 3.1415927f) : start;
            for (int i = 0; i <= kSteps; ++i) {
                const float a = startAngle + 3.1415927f * static_cast<float>(i) / kSteps;
                out.push_back({centre.x + std::cos(a) * r, centre.y + std::sin(a) * r});
            }
        }
        return out;
    }

    void paintGameFrame(ui::Context& ctx, ui::DrawList& draw) const
    {
        const auto [min, size] = gameArea();
        const ui::Vec2 a = toScreen(min);
        const ui::Vec2 c = toScreen(min + size);
        const ui::Rect r{std::round(a.x) + 0.5f, std::round(c.y) + 0.5f, std::round(c.x - a.x), std::round(a.y - c.y)};
        draw.strokeRoundedRect(r, 0.0f, ui::Color{255, 255, 255, 110}, 1.0f);
        const ui::Theme& t = ctx.theme();
        draw.text(t.font, t.smallFontSize, {r.x + 4.0f, r.y - 16.0f}, ui::Color{255, 255, 255, 120},
                  m_gameCamera ? "Game view (Camera)" : "Game view");
    }

    void paintSelection(ui::Context& ctx, ui::DrawList& draw) const
    {
        const Scene& scene = m_editor.document().scene();
        const EntityId selected = m_editor.selection();
        if (selected.isNull() || !scene.contains(selected)) {
            return;
        }
        const ui::Theme& t = ctx.theme();
        if (const auto corners = scene.worldCorners(selected)) {
            std::array<ui::Vec2, 4> points{};
            for (std::size_t i = 0; i < 4; ++i) {
                points[i] = toScreen((*corners)[i]);
            }
            draw.polyline(points, ui::Color{0, 0, 0, 120}, 3.5f, true);
            draw.polyline(points, t.accent, 1.5f, true);
        }

        const auto g = gizmo();
        if (!g) {
            return;
        }
        const Handle hot = m_drag == Drag::Transform ? m_start.handle : handleAt(ctx.pointerPosition());
        const ui::Vec2 o = g->origin;
        const ui::Color red = ui::Color::hex(0xEB5757);
        const ui::Color green = ui::Color::hex(0x6FCF4F);
        const ui::Color blue = ui::Color::hex(0x56A8F5);
        const auto tint = [&](ui::Color c, bool active) { return active ? ui::Color::hex(0xFFE08A) : c; };
        const auto end = [&](ui::Vec2 axis) { return ui::Vec2{o.x + axis.x * kGizmoLength, o.y + axis.y * kGizmoLength}; };
        const auto square = [&](ui::Vec2 p, ui::Color c) {
            draw.fillRect({p.x - kHandleSize * 0.5f - 1.0f, p.y - kHandleSize * 0.5f - 1.0f, kHandleSize + 2.0f,
                           kHandleSize + 2.0f},
                          ui::Color{0, 0, 0, 140});
            draw.fillRect({p.x - kHandleSize * 0.5f, p.y - kHandleSize * 0.5f, kHandleSize, kHandleSize}, c);
        };

        switch (m_panel.tool()) {
        case SceneTool::Move: {
            const auto arrow = [&](ui::Vec2 axis, ui::Color c) {
                const ui::Vec2 e = end(axis);
                const ui::Vec2 n{-axis.y, axis.x};
                draw.line(o, e, c, 2.0f);
                const std::array<ui::Vec2, 3> head{
                    ui::Vec2{e.x + axis.x * 12.0f, e.y + axis.y * 12.0f},
                    ui::Vec2{e.x + n.x * 5.0f, e.y + n.y * 5.0f}, ui::Vec2{e.x - n.x * 5.0f, e.y - n.y * 5.0f}};
                // Clockwise on screen for the fill.
                const float cross = (head[1].x - head[0].x) * (head[2].y - head[0].y)
                                    - (head[1].y - head[0].y) * (head[2].x - head[0].x);
                if (cross > 0.0f) {
                    draw.fillConvexPolygon(head, c);
                } else {
                    const std::array<ui::Vec2, 3> rev{head[0], head[2], head[1]};
                    draw.fillConvexPolygon(rev, c);
                }
            };
            arrow(g->axisX, tint(red, hot == Handle::MoveX));
            arrow(g->axisY, tint(green, hot == Handle::MoveY));
            const ui::Color c = tint(blue, hot == Handle::MoveXY || hot == Handle::Body);
            draw.fillRect({o.x + 5.0f, o.y - 19.0f, 14.0f, 14.0f}, c.fade(0.45f));
            draw.strokeRoundedRect({o.x + 5.0f, o.y - 19.0f, 14.0f, 14.0f}, 0.0f, c, 1.5f);
            break;
        }
        case SceneTool::Rotate: {
            std::array<ui::Vec2, 64> ring{};
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(ring.size());
                ring[i] = {o.x + std::cos(a) * kGizmoLength, o.y + std::sin(a) * kGizmoLength};
            }
            draw.polyline(ring, ui::Color{0, 0, 0, 120}, 4.0f, true);
            draw.polyline(ring, tint(blue, hot == Handle::Rotate), 2.0f, true);
            const float rot = scene.find(selected)->transform().rotation * kPi / 180.0f;
            draw.line(o, {o.x + std::cos(rot) * kGizmoLength, o.y + std::sin(rot) * kGizmoLength},
                      tint(blue, hot == Handle::Rotate).fade(0.7f), 1.5f);
            break;
        }
        case SceneTool::Scale:
            draw.line(o, end(g->axisX), tint(red, hot == Handle::ScaleX), 2.0f);
            draw.line(o, end(g->axisY), tint(green, hot == Handle::ScaleY), 2.0f);
            square(end(g->axisX), tint(red, hot == Handle::ScaleX));
            square(end(g->axisY), tint(green, hot == Handle::ScaleY));
            square(o, tint(ui::Color::hex(0xDDDDDD), hot == Handle::ScaleXY));
            break;
        case SceneTool::Rect:
            for (const auto& [h, pos] : rectHandles()) {
                square(pos, tint(t.accent, hot == h));
            }
            break;
        default:
            break;
        }
        if (m_panel.tool() != SceneTool::Scale) {
            draw.fillCircle(o, 3.5f, ui::Color{0, 0, 0, 160});
            draw.fillCircle(o, 2.5f, t.accent);
        }
    }

    EditorServices& m_editor;
    ViewportPanel& m_panel;
    WorldImage m_image;
    float m_worldWidth = 1280.0f;
    float m_worldHeight = 720.0f;
    std::optional<GameCameraView> m_gameCamera;

    Vec2 m_center{640.0f, 360.0f};
    float m_zoom = 0.5f;
    bool m_pendingFit = true;

    Drag m_drag = Drag::None;
    bool m_moved = false;
    ui::Vec2 m_pressScreen;
    Vec2 m_panStartCenter;
    DragStart m_start;
    TileCell m_lastCell;
    ui::TextureId m_tileTexture = 0;
    ui::Size m_tileImageSize;
    std::optional<Tileset> m_tileset;
};

// ---------------------------------------------------------------------------
// ViewportPanel
// ---------------------------------------------------------------------------
ViewportPanel::ViewportPanel(EditorServices& editor)
    : ui::Column(0.0f)
    , m_editor(editor)
{
    auto& toolbar = makePanelToolbar(*this);
    m_toolbar = &toolbar;
    const std::array<std::pair<ui::Icon, const char*>, 4> tools{{
        {ui::Icon::Move, "Move tool (W): drag the arrows, the square or the object itself. Shift snaps to the grid."},
        {ui::Icon::Rotate, "Rotate tool (E): drag the circle. Shift turns in 15° steps."},
        {ui::Icon::Scale, "Scale tool (R): drag the squares on the axes, or the middle one for both."},
        {ui::Icon::RectTool, "Rect tool (T): drag a corner or edge to resize shapes and sprites. Shift keeps the "
                             "proportions."},
    }};
    for (std::size_t i = 0; i < tools.size(); ++i) {
        const auto tool = static_cast<SceneTool>(i);
        m_toolButtons[i] = &addToolButton(toolbar, tools[i].first, tools[i].second, [this, tool] { setTool(tool); });
    }
    // Tile tools: shown while a tilemap is selected.
    auto& divider = toolbar.emplace<ui::Separator>(ui::Axis::Vertical);
    divider.setFixedSize({1.0f, 16.0f});
    m_tileToolsDivider = &divider;
    const std::array<std::pair<ui::Icon, const char*>, 4> tileTools{{
        {ui::Icon::Brush, "Brush (B): paint the tile chosen in the Tile Palette. On the Collision layer, paints "
                          "cells that block movement."},
        {ui::Icon::Eraser, "Eraser (X): remove tiles (or collision)"},
        {ui::Icon::Fill, "Fill (G): fill the connected area of the same tile, up to the edges of the view"},
        {ui::Icon::Eyedropper, "Picker (I): click a tile in the map to paint with it"},
    }};
    for (std::size_t i = 0; i < tileTools.size(); ++i) {
        const auto tool = static_cast<SceneTool>(4 + i);
        m_toolButtons[4 + i] = &addToolButton(toolbar, tileTools[i].first, tileTools[i].second, [this, tool] {
            setTool(tool);
            m_editor.showPanel(kTilePalettePanel);
        });
    }
    toolbar.emplace<ui::Separator>(ui::Axis::Vertical).setFixedSize({1.0f, 16.0f});
    addToolButton(toolbar, ui::Icon::Scene, "Show the whole game view", [this] { fitGameView(); });
    toolbar.emplace<ui::Spacer>();
    auto& hint = toolbar.emplace<ui::Label>("", ui::Label::Style::Faint);
    m_hint = &hint;
    hint.setTooltip("Zoom: scroll wheel or two fingers on a trackpad, around the mouse.\n"
                    "Pan: drag with the right or middle mouse button, or hold ⌥ (Alt) and drag.\n"
                    "F: frame the selected object.");
    m_zoom = &toolbar.emplace<ui::Button>("100%", [this] { openZoomMenu(); });
    m_zoom->setStyle(ui::Button::Style::Subtle);
    m_zoom->setTooltip("Zoom. Click for presets.");

    m_view = &addChild(std::make_unique<SceneView>(editor, *this));
    setTool(SceneTool::Move);
}

void ViewportPanel::setTool(SceneTool tool)
{
    m_tool = tool;
    for (std::size_t i = 0; i < m_toolButtons.size(); ++i) {
        m_toolButtons[i]->setToggled(static_cast<SceneTool>(i) == tool);
    }
}

void ViewportPanel::focusView()
{
    m_editor.ui().setFocus(m_view);
}

void ViewportPanel::frameSelection()
{
    m_view->frameSelection();
}

void ViewportPanel::fitGameView()
{
    m_view->fitGameView();
}

void ViewportPanel::openZoomMenu()
{
    std::vector<ui::MenuItem> items;
    const auto add = [&](std::string label, std::string shortcut, std::function<void()> action) {
        ui::MenuItem item;
        item.label = std::move(label);
        item.shortcut = std::move(shortcut);
        item.action = std::move(action);
        items.push_back(std::move(item));
    };
    add("Show the Game View", "", [this] { fitGameView(); });
    add("Frame Selection", "F", [this] { frameSelection(); });
    items.push_back(ui::MenuItem::divider());
    for (float z : {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f}) {
        add(percentText(z), "", [this, z] { m_view->setZoomAround(z, m_view->bounds().center()); });
    }
    openMenuAt(m_editor.ui(), m_zoom->bounds(), std::move(items));
}

std::optional<Vec2> ViewportPanel::worldAt(ui::Vec2 windowPoint) const
{
    if (!isVisible() || !m_view->bounds().contains(windowPoint)) {
        return std::nullopt;
    }
    return m_view->toWorld(windowPoint);
}

void ViewportPanel::update(UiFrame& frame)
{
    // Play mode: the view follows the game's camera and the tools rest; the
    // editor's own view comes back when the game stops.
    const bool playing = m_editor.isPlaying();
    for (const auto& child : m_toolbar->children()) {
        if (child.get() != m_hint) {
            child->setEnabled(!playing);
        }
    }
    m_hint->setText(playing ? "Playing · the game has the keyboard while this view is focused"
                            : "Scroll to zoom · right-drag or ⌥-drag to pan");
    if (playing && !m_editorCamera) {
        m_editorCamera = std::pair{m_view->center(), m_view->zoom()};
    } else if (!playing && m_editorCamera) {
        m_view->setCamera(m_editorCamera->first, m_editorCamera->second);
        m_editorCamera.reset();
    }

    // Tile tools only make sense with a tilemap selected.
    const auto map = m_view->selectedMap();
    for (std::size_t i = 4; i < m_toolButtons.size(); ++i) {
        m_toolButtons[i]->setVisible(map.has_value());
    }
    m_tileToolsDivider->setVisible(map.has_value());
    if (!map && isTileTool(m_tool)) {
        setTool(SceneTool::Move);
    }
    // The tileset's image, for the brush preview and collision display.
    ui::TextureId texture = 0;
    ui::Size imageSize;
    std::optional<Tileset> tileset;
    if (const AssetDatabase* db = m_editor.assets(); db != nullptr && map) {
        const AssetRecord* ts = db->find(map->map->tileset);
        if (ts != nullptr && ts->tileset) {
            tileset = *ts->tileset;
            if (const AssetRecord* img = db->find(ts->tileset->image); img != nullptr && img->isUsable()) {
                if (const auto loaded = frame.loadImage(db->absolutePath(*img), img->filter == TextureFilter::Pixel)) {
                    texture = loaded->texture;
                    imageSize = loaded->size;
                }
            }
        }
    }
    m_view->setTileImage(texture, imageSize, std::move(tileset));

    m_view->setFrameData(frame.worldImage(), static_cast<float>(frame.worldWidth()),
                         static_cast<float>(frame.worldHeight()), frame.gameCamera());
    if (playing) {
        const auto [min, size] = m_view->gameArea();
        const ui::Rect b = m_view->bounds();
        if (size.x > 0.0f && size.y > 0.0f && b.width > 0.0f && b.height > 0.0f) {
            m_view->setCamera(min + size * 0.5f, std::min(b.width / size.x, b.height / size.y));
        }
    }

    // Render the world at this panel's size in real pixels, so it stays sharp.
    const ui::Rect b = m_view->bounds();
    const float pixelScale = frame.ui().atlas().pixelScale();
    if (b.width >= 1.0f && b.height >= 1.0f) {
        frame.setWorldView(WorldView{m_view->center(), m_view->zoom() * pixelScale,
                                     static_cast<int>(std::lround(b.width * pixelScale)),
                                     static_cast<int>(std::lround(b.height * pixelScale))});
    }
    m_zoom->setText(percentText(m_view->zoom()));
}

} // namespace rolo::editor
