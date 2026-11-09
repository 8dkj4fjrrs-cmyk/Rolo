#include "rolo/ui/Icons.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rolo::ui {

namespace {

// Icons are designed on a 16 x 16 grid; G maps grid coordinates to the area.
struct Grid16 {
    Vec2 c;
    float u;
    Vec2 operator()(float x, float y) const { return {c.x + (x - 8.0f) * u, c.y + (y - 8.0f) * u}; }
};

void ring(DrawList& draw, Vec2 centre, float radius, Color color, float thickness, float from = 0.0f,
          float to = 6.2831853f, bool closed = true)
{
    std::array<Vec2, 28> pts{};
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const float a = from + (to - from) * static_cast<float>(i) / static_cast<float>(closed ? pts.size() : pts.size() - 1);
        pts[i] = {centre.x + std::cos(a) * radius, centre.y + std::sin(a) * radius};
    }
    draw.polyline(pts, color, thickness, closed);
}

void arrowHead(DrawList& draw, Vec2 tip, Vec2 dir, float size, Color color)
{
    const Vec2 n{-dir.y, dir.x};
    const std::array<Vec2, 3> tri{tip, Vec2{tip.x - dir.x * size + n.x * size * 0.7f, tip.y - dir.y * size + n.y * size * 0.7f},
                                  Vec2{tip.x - dir.x * size - n.x * size * 0.7f, tip.y - dir.y * size - n.y * size * 0.7f}};
    // Clockwise on screen whatever the direction.
    const float cross = (tri[1].x - tri[0].x) * (tri[2].y - tri[0].y) - (tri[1].y - tri[0].y) * (tri[2].x - tri[0].x);
    if (cross > 0.0f) {
        draw.fillConvexPolygon(tri, color);
    } else {
        const std::array<Vec2, 3> rev{tri[0], tri[2], tri[1]};
        draw.fillConvexPolygon(rev, color);
    }
}

} // namespace

void drawIcon(DrawList& draw, Icon icon, Rect area, Color color)
{
    const Vec2 c = area.center();
    const float s = std::min(area.width, area.height); // icon box size
    const float t = std::max(1.25f, s * 0.11f);         // stroke width

    switch (icon) {
    case Icon::ChevronRight: {
        const float h = s * 0.22f;
        const std::array<Vec2, 3> pts{Vec2{c.x - h * 0.5f, c.y - h}, Vec2{c.x + h * 0.5f, c.y},
                                      Vec2{c.x - h * 0.5f, c.y + h}};
        draw.polyline(pts, color, t);
        break;
    }
    case Icon::ChevronDown: {
        const float h = s * 0.22f;
        const std::array<Vec2, 3> pts{Vec2{c.x - h, c.y - h * 0.5f}, Vec2{c.x, c.y + h * 0.5f},
                                      Vec2{c.x + h, c.y - h * 0.5f}};
        draw.polyline(pts, color, t);
        break;
    }
    case Icon::Close: {
        const float h = s * 0.2f;
        draw.line({c.x - h, c.y - h}, {c.x + h, c.y + h}, color, t);
        draw.line({c.x - h, c.y + h}, {c.x + h, c.y - h}, color, t);
        break;
    }
    case Icon::Check: {
        const float h = s * 0.24f;
        const std::array<Vec2, 3> pts{Vec2{c.x - h, c.y}, Vec2{c.x - h * 0.3f, c.y + h * 0.7f},
                                      Vec2{c.x + h, c.y - h * 0.65f}};
        draw.polyline(pts, color, t * 1.15f);
        break;
    }
    case Icon::Search: {
        const float r = s * 0.2f;
        const Vec2 lens{c.x - s * 0.06f, c.y - s * 0.06f};
        std::array<Vec2, 20> ring{};
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(ring.size());
            ring[i] = {lens.x + std::cos(a) * r, lens.y + std::sin(a) * r};
        }
        draw.polyline(ring, color, t, true);
        draw.line({lens.x + r * 0.72f, lens.y + r * 0.72f}, {c.x + s * 0.28f, c.y + s * 0.28f}, color, t);
        break;
    }
    case Icon::Dot:
        draw.fillCircle(c, s * 0.16f, color);
        break;
    default:
        break;
    }

    const Grid16 g{c, s / 16.0f};
    const float u = s / 16.0f;
    const float w = std::max(1.2f, 1.3f * u); // stroke for the larger icons
    switch (icon) {
    case Icon::ChevronLeft: {
        const float h = s * 0.22f;
        const std::array<Vec2, 3> pts{Vec2{c.x + h * 0.5f, c.y - h}, Vec2{c.x - h * 0.5f, c.y},
                                      Vec2{c.x + h * 0.5f, c.y + h}};
        draw.polyline(pts, color, t);
        break;
    }
    case Icon::Plus:
        draw.line(g(8, 3.5f), g(8, 12.5f), color, w);
        draw.line(g(3.5f, 8), g(12.5f, 8), color, w);
        break;
    case Icon::Menu:
        for (float y : {4.5f, 8.0f, 11.5f}) {
            draw.line(g(3, y), g(13, y), color, w);
        }
        break;
    case Icon::More:
        for (float y : {4.0f, 8.0f, 12.0f}) {
            draw.fillCircle(g(8, y), 1.3f * u, color);
        }
        break;
    case Icon::Help:
        ring(draw, c, 6.0f * u, color, w * 0.85f);
        ring(draw, g(8, 6.6f), 1.9f * u, color, w * 0.85f, 3.4f, 7.2f, false);
        draw.line(g(8, 8.5f), g(8, 9.6f), color, w * 0.85f);
        draw.fillCircle(g(8, 11.6f), 0.8f * u, color);
        break;
    case Icon::Info:
        draw.fillCircle(c, 6.5f * u, color);
        draw.fillCircle(g(8, 5.0f), 0.95f * u, Color{0, 0, 0, 200});
        draw.line(g(8, 7.2f), g(8, 11.6f), Color{0, 0, 0, 200}, 1.6f * u);
        break;
    case Icon::Error:
        draw.fillCircle(c, 6.5f * u, color);
        draw.line(g(5.6f, 5.6f), g(10.4f, 10.4f), Color{0, 0, 0, 200}, 1.5f * u);
        draw.line(g(5.6f, 10.4f), g(10.4f, 5.6f), Color{0, 0, 0, 200}, 1.5f * u);
        break;
    case Icon::Warning: {
        const std::array<Vec2, 3> tri{g(8, 1.8f), g(14.6f, 13.6f), g(1.4f, 13.6f)};
        draw.fillConvexPolygon(tri, color);
        draw.line(g(8, 5.8f), g(8, 9.6f), Color{0, 0, 0, 200}, 1.6f * u);
        draw.fillCircle(g(8, 11.7f), 0.9f * u, Color{0, 0, 0, 200});
        break;
    }
    case Icon::Play: {
        const std::array<Vec2, 3> tri{g(4.5f, 2.8f), g(13.2f, 8), g(4.5f, 13.2f)};
        draw.fillConvexPolygon(tri, color);
        break;
    }
    case Icon::Stop:
        draw.fillRoundedRect(Rect{g(3.6f, 3.6f).x, g(3.6f, 3.6f).y, 8.8f * u, 8.8f * u}, 1.0f * u, color);
        break;
    case Icon::Pause:
        draw.fillRect(Rect{g(4.0f, 3.0f).x, g(4.0f, 3.0f).y, 2.8f * u, 10.0f * u}, color);
        draw.fillRect(Rect{g(9.2f, 3.0f).x, g(9.2f, 3.0f).y, 2.8f * u, 10.0f * u}, color);
        break;
    case Icon::Step: {
        const std::array<Vec2, 3> tri{g(3.0f, 3.0f), g(10.0f, 8.0f), g(3.0f, 13.0f)};
        draw.fillConvexPolygon(tri, color);
        draw.fillRect(Rect{g(10.8f, 3.0f).x, g(10.8f, 3.0f).y, 2.4f * u, 10.0f * u}, color);
        break;
    }
    case Icon::Refresh:
        ring(draw, c, 5.2f * u, color, w, -1.1f, 4.4f, false);
        arrowHead(draw, g(12.6f, 2.6f), Vec2{0.5f, -0.86f}, 3.2f * u, color);
        break;
    case Icon::Send: {
        const std::array<Vec2, 4> plane{g(1.5f, 7.4f), g(14.5f, 1.8f), g(9.6f, 14.2f), g(7.4f, 8.6f)};
        draw.polyline(plane, color, w * 0.9f, true);
        draw.line(g(7.4f, 8.6f), g(14.5f, 1.8f), color, w * 0.9f);
        break;
    }
    case Icon::Folder: {
        const std::array<Vec2, 5> tab{g(1.5f, 3.5f), g(6.2f, 3.5f), g(7.6f, 5.0f), g(7.6f, 6.0f), g(1.5f, 6.0f)};
        draw.fillConvexPolygon(tab, color);
        draw.fillRoundedRect(Rect{g(1.5f, 4.8f).x, g(1.5f, 4.8f).y, 13.0f * u, 8.4f * u}, 1.2f * u, color);
        break;
    }
    case Icon::Layers: {
        const std::array<Vec2, 4> top{g(8, 2), g(14, 5.2f), g(8, 8.4f), g(2, 5.2f)};
        draw.polyline(top, color, w * 0.9f, true);
        const std::array<Vec2, 3> mid{g(2, 8.2f), g(8, 11.4f), g(14, 8.2f)};
        draw.polyline(mid, color, w * 0.9f);
        const std::array<Vec2, 3> low{g(2, 11.0f), g(8, 14.2f), g(14, 11.0f)};
        draw.polyline(low, color, w * 0.9f);
        break;
    }
    case Icon::Scene: {
        draw.strokeRoundedRect(Rect{g(1.5f, 2.5f).x, g(1.5f, 2.5f).y, 13.0f * u, 11.0f * u}, 1.5f * u, color, w * 0.9f);
        const std::array<Vec2, 3> hill{g(3.2f, 11.8f), g(7.0f, 7.0f), g(10.6f, 11.8f)};
        draw.fillConvexPolygon(hill, color);
        draw.fillCircle(g(11.0f, 5.8f), 1.3f * u, color);
        break;
    }
    case Icon::Sliders:
        for (int i = 0; i < 3; ++i) {
            const float y = 4.0f + 4.0f * static_cast<float>(i);
            const float knob = i == 1 ? 10.5f : 5.5f;
            draw.line(g(2, y), g(14, y), color, w * 0.8f);
            draw.fillCircle(g(knob, y), 1.9f * u, color);
        }
        break;
    case Icon::Terminal:
        draw.strokeRoundedRect(Rect{g(1.5f, 2.5f).x, g(1.5f, 2.5f).y, 13.0f * u, 11.0f * u}, 1.5f * u, color, w * 0.9f);
        {
            const std::array<Vec2, 3> prompt{g(4.2f, 6.0f), g(6.6f, 8.0f), g(4.2f, 10.0f)};
            draw.polyline(prompt, color, w * 0.9f);
        }
        draw.line(g(8.0f, 10.4f), g(11.6f, 10.4f), color, w * 0.9f);
        break;
    case Icon::Eye: {
        std::array<Vec2, 24> lid{};
        for (std::size_t i = 0; i < lid.size(); ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(lid.size());
            lid[i] = g(8.0f + std::cos(a) * 6.6f, 8.0f + std::sin(a) * 3.9f);
        }
        draw.polyline(lid, color, w * 0.9f, true);
        draw.fillCircle(c, 2.1f * u, color);
        break;
    }
    case Icon::Rolo: {
        // Rolo, the engine's mascot: a round face with two eyes and a smile,
        // cut out of the ball in a darker shade.
        const Vec2 c0 = g(8.0f, 8.4f);
        draw.fillCircle(c0, 6.2f * u, color);
        const Color cut{0, 0, 0, 150};
        draw.fillCircle(g(5.9f, 7.2f), 1.25f * u, cut);
        draw.fillCircle(g(10.1f, 7.2f), 1.25f * u, cut);
        std::array<Vec2, 9> smile{};
        for (std::size_t i = 0; i < smile.size(); ++i) {
            const float a = 0.45f + 2.24f * static_cast<float>(i) / static_cast<float>(smile.size() - 1);
            smile[i] = {c0.x + std::cos(a) * 3.0f * u, c0.y + 0.6f * u + std::sin(a) * 2.6f * u};
        }
        draw.polyline(smile, cut, 1.1f * u);
        break;
    }
    case Icon::Move: {
        const float a = 3.0f * u;
        draw.line(g(8, 2.5f), g(8, 13.5f), color, w * 0.9f);
        draw.line(g(2.5f, 8), g(13.5f, 8), color, w * 0.9f);
        arrowHead(draw, g(8, 1.5f), {0, -1}, a, color);
        arrowHead(draw, g(8, 14.5f), {0, 1}, a, color);
        arrowHead(draw, g(1.5f, 8), {-1, 0}, a, color);
        arrowHead(draw, g(14.5f, 8), {1, 0}, a, color);
        break;
    }
    case Icon::Rotate:
        ring(draw, c, 5.4f * u, color, w * 0.9f, 0.6f, 5.4f, false);
        arrowHead(draw, g(8.0f + 5.4f * std::cos(5.6f), 8.0f + 5.4f * std::sin(5.6f)), Vec2{0.63f, 0.77f}, 3.0f * u, color);
        break;
    case Icon::Scale:
        draw.strokeRoundedRect(Rect{g(2, 6).x, g(2, 6).y, 8.0f * u, 8.0f * u}, 0.8f * u, color, w * 0.9f);
        draw.line(g(7, 9), g(13, 3), color, w * 0.9f);
        arrowHead(draw, g(14, 2), Vec2{0.707f, -0.707f}, 3.2f * u, color);
        break;
    case Icon::RectTool:
        draw.strokeRoundedRect(Rect{g(3, 3).x, g(3, 3).y, 10.0f * u, 10.0f * u}, 0.0f, color, w * 0.8f);
        for (float x : {3.0f, 13.0f}) {
            for (float y : {3.0f, 13.0f}) {
                const Vec2 p = g(x, y);
                draw.fillRect(Rect{p.x - 1.6f * u, p.y - 1.6f * u, 3.2f * u, 3.2f * u}, color);
            }
        }
        break;
    case Icon::Save: {
        const Rect body{g(2.0f, 2.0f).x, g(2.0f, 2.0f).y, 12.0f * u, 12.0f * u};
        draw.strokeRoundedRect(body, 1.5f * u, color, w * 0.9f);
        draw.fillRect(Rect{g(5.0f, 2.5f).x, g(5.0f, 2.5f).y, 6.0f * u, 3.5f * u}, color);   // shutter
        draw.strokeRoundedRect(Rect{g(4.5f, 9.0f).x, g(4.5f, 9.0f).y, 7.0f * u, 4.5f * u}, 0.5f * u, color, w * 0.8f);
        break;
    }
    case Icon::Undo:
    case Icon::Redo: {
        const bool undo = icon == Icon::Undo;
        // A half-circle hook with an arrowhead at its start.
        const float cx = 8.5f;
        const float cy = 9.0f;
        const float r = 4.2f;
        std::array<Vec2, 14> arc{};
        for (std::size_t i = 0; i < arc.size(); ++i) {
            const float a = -1.5707963f + 3.1415927f * static_cast<float>(i) / static_cast<float>(arc.size() - 1);
            const float x = cx + std::cos(a) * r;
            arc[i] = g(undo ? x : 16.0f - x, cy + std::sin(a) * r);
        }
        draw.polyline(arc, color, w * 0.9f);
        const Vec2 tip = undo ? g(cx - 1.0f, cy - r) : g(16.0f - cx + 1.0f, cy - r);
        draw.line(tip, undo ? g(cx + 0.5f, cy - r) : g(16.0f - cx - 0.5f, cy - r), color, w * 0.9f);
        arrowHead(draw, undo ? g(2.5f, cy - r) : g(13.5f, cy - r), undo ? Vec2{-1.0f, 0.0f} : Vec2{1.0f, 0.0f},
                  3.2f * u, color);
        draw.line(undo ? g(3.5f, cy - r) : g(12.5f, cy - r), tip, color, w * 0.9f);
        break;
    }
    case Icon::Code: {
        // "</>": two angle brackets and a slash between them.
        const std::array<Vec2, 3> left{g(5.0f, 4.5f), g(1.8f, 8.0f), g(5.0f, 11.5f)};
        const std::array<Vec2, 3> right{g(11.0f, 4.5f), g(14.2f, 8.0f), g(11.0f, 11.5f)};
        draw.polyline(left, color, w);
        draw.polyline(right, color, w);
        draw.line(g(9.2f, 3.5f), g(6.8f, 12.5f), color, w);
        break;
    }
    case Icon::Sound: {
        // A speaker: a small box and its cone, and two waves coming out.
        // (Two convex parts: the whole outline is not convex.)
        const std::array<Vec2, 4> box{g(1.8f, 6.0f), g(4.8f, 6.0f), g(4.8f, 10.0f), g(1.8f, 10.0f)};
        const std::array<Vec2, 4> cone{g(4.6f, 6.0f), g(8.4f, 2.8f), g(8.4f, 13.2f), g(4.6f, 10.0f)};
        draw.fillConvexPolygon(box, color);
        draw.fillConvexPolygon(cone, color);
        ring(draw, g(8.4f, 8.0f), 3.2f * u, color, w * 0.9f, -0.85f, 0.85f, false);
        ring(draw, g(8.4f, 8.0f), 5.9f * u, color, w * 0.9f, -0.9f, 0.9f, false);
        break;
    }
    case Icon::Brush: {
        // A handle going down-left into a rounded tip.
        draw.line(g(13.5f, 2.5f), g(7.2f, 8.8f), color, w * 1.2f);
        const std::array<Vec2, 4> ferrule{g(6.0f, 8.0f), g(8.0f, 10.0f), g(6.5f, 11.5f), g(4.5f, 9.5f)};
        draw.fillConvexPolygon(ferrule, color);
        const std::array<Vec2, 4> tip{g(4.5f, 9.5f), g(6.5f, 11.5f), g(3.0f, 14.0f), g(2.0f, 13.0f)};
        draw.fillConvexPolygon(tip, color);
        break;
    }
    case Icon::Eraser: {
        const std::array<Vec2, 4> body{g(9.5f, 2.5f), g(14.0f, 7.0f), g(8.0f, 13.0f), g(3.5f, 8.5f)};
        draw.polyline(body, color, w * 0.9f, true);
        const std::array<Vec2, 4> rubber{g(6.0f, 6.0f), g(10.5f, 10.5f), g(8.0f, 13.0f), g(3.5f, 8.5f)};
        draw.fillConvexPolygon(rubber, color);
        draw.line(g(8.0f, 13.5f), g(14.0f, 13.5f), color, w * 0.8f);
        break;
    }
    case Icon::Fill: {
        // A tipped bucket with a drop.
        const std::array<Vec2, 4> bucket{g(7.0f, 2.5f), g(12.5f, 8.0f), g(8.0f, 12.5f), g(2.5f, 7.0f)};
        draw.polyline(bucket, color, w * 0.9f, true);
        draw.line(g(2.5f, 7.0f), g(12.5f, 8.0f), color, w * 0.8f);
        draw.fillCircle(g(13.5f, 12.0f), 1.8f * u, color);
        break;
    }
    case Icon::Eyedropper: {
        draw.line(g(3.0f, 13.0f), g(9.5f, 6.5f), color, w * 1.1f);
        const std::array<Vec2, 4> bulb{g(9.0f, 4.0f), g(12.0f, 1.8f), g(14.2f, 4.0f), g(12.0f, 7.0f)};
        draw.fillConvexPolygon(bulb, color);
        draw.line(g(8.0f, 4.5f), g(11.5f, 8.0f), color, w * 1.1f);
        break;
    }
    case Icon::Grid:
        for (float x : {2.0f, 9.0f}) {
            for (float y : {2.0f, 9.0f}) {
                const Vec2 p = g(x, y);
                draw.strokeRoundedRect(Rect{p.x, p.y, 5.0f * u, 5.0f * u}, 1.0f * u, color, w * 0.8f);
            }
        }
        break;
    default:
        break;
    }
}

} // namespace rolo::ui
