#include "SceneRenderer.hpp"

#include "ButtonIconNodes.hpp"

#include "TextSprites.hpp"
#include "TextureStore.hpp"


#include <filesystem>
#include <unordered_map>

namespace rolo::detail {

namespace {

ax::Color4F toAxmol(Color c)
{
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

// Draws a Shape centred on the node's origin.
void drawShape(ax::DrawNode& node, const Shape& shape)
{
    const float hw = shape.size.x * 0.5f;
    const float hh = shape.size.y * 0.5f;
    if (hw <= 0.0f || hh <= 0.0f || shape.color.a == 0) {
        return;
    }
    const ax::Color4F color = toAxmol(shape.color);
    const float thickness = std::max(0.5f, shape.lineWidth);
    if (shape.kind == ShapeKind::Rectangle) {
        if (shape.filled) {
            node.drawSolidRect(ax::Vec2(-hw, -hh), ax::Vec2(hw, hh), color);
        } else {
            node.drawRect(ax::Vec2(-hw, -hh), ax::Vec2(hw, hh), color, thickness);
        }
        return;
    }
    // Ellipses: a circle of radius hw, stretched vertically to hh.
    const unsigned int segments = std::clamp(static_cast<unsigned int>(std::max(hw, hh) * 0.6f), 24u, 128u);
    if (shape.filled) {
        node.drawSolidCircle(ax::Vec2::ZERO, hw, 0.0f, segments, 1.0f, hh / hw, color);
    } else {
        node.drawCircle(ax::Vec2::ZERO, hw, 0.0f, segments, false, 1.0f, hh / hw, color, thickness);
    }
}

// What a sprite without a usable image looks like: a pink box with a cross,
// impossible to mistake for real art.
ax::Node* makePlaceholder(Vec2 size)
{
    auto* node = ax::DrawNode::create();
    const float hw = std::max(4.0f, size.x * 0.5f);
    const float hh = std::max(4.0f, size.y * 0.5f);
    const ax::Color4F pink(1.0f, 0.2f, 0.75f, 1.0f);
    node->drawSolidRect(ax::Vec2(-hw, -hh), ax::Vec2(hw, hh), ax::Color4F(1.0f, 0.2f, 0.75f, 0.25f));
    node->drawRect(ax::Vec2(-hw, -hh), ax::Vec2(hw, hh), pink, 2.0f);
    node->drawLine(ax::Vec2(-hw, -hh), ax::Vec2(hw, hh), pink, 2.0f);
    node->drawLine(ax::Vec2(-hw, hh), ax::Vec2(hw, -hh), pink, 2.0f);
    return node;
}

// Images cut into frames (tiles, animation frames) are drawn from a hair
// inside each frame's edges. At some zoom levels a screen pixel falls exactly
// on the border between two frames of the image; without the inset it may
// pick up the neighbouring frame's colour, which shows as thin lines
// ("bleeding"). In image pixels: invisible, but enough.
constexpr float kInset = 0.02f;

ax::Rect insetRect(const TileRect& r)
{
    return ax::Rect(static_cast<float>(r.x) + kInset, static_cast<float>(r.y) + kInset,
                    static_cast<float>(r.width) - 2.0f * kInset, static_cast<float>(r.height) - 2.0f * kInset);
}

} // namespace

SceneRenderer::SceneRenderer(ax::Node& root, TextureStore* textures, bool drawContent)
    : m_root(root)
    , m_textures(textures)
    , m_drawContent(drawContent)
{
}

SceneRenderer::~SceneRenderer()
{
    if (m_container != nullptr) {
        m_container->removeFromParent();
    }
}

void SceneRenderer::show(std::shared_ptr<const Scene> scene)
{
    m_scene = std::move(scene);
}

void SceneRenderer::setAssets(const AssetDatabase* assets)
{
    m_assets = assets;
}

void SceneRenderer::update()
{
    const bool sceneChanged =
        m_scene.get() != m_builtScene || (m_scene != nullptr && m_scene->revision() != m_builtRevision);
    const bool assetsChanged =
        m_assets != m_builtAssets || (m_assets != nullptr && m_assets->revision() != m_builtAssetsRevision);
    const bool texturesChanged = m_textures != nullptr && m_textures->generation() != m_builtTextures;
    if (!sceneChanged && !assetsChanged && !texturesChanged) {
        return;
    }
    struct PinAfter {
        SceneRenderer& self;
        ~PinAfter() { self.pinCameraChildren(); }   // nodes were put back at their own places
    } pinAfter{*this};
    const bool onlyMoved = !assetsChanged && !texturesChanged && m_scene.get() == m_builtScene && m_scene != nullptr
                           && m_scene->contentRevision() == m_builtContentRevision;
    if (onlyMoved) {
        moveNodes();
    } else {
        rebuild();
    }
}

void SceneRenderer::setScreen(Vec2 gameSize, Vec2 visible, ScreenInsets insets)
{
    if (gameSize == m_gameSize && visible == m_visible && insets == m_insets) {
        return;
    }
    m_gameSize = gameSize;
    m_visible = visible;
    m_insets = insets;
    pinCameraChildren();
}

// Things fixed to the camera (its children) follow the screen's edges when
// the screen shows more than the game size.
void SceneRenderer::pinCameraChildren()
{
    if (m_scene == nullptr) {
        return;
    }
    const bool wanted = m_visible.x > 0.0f && (m_visible != m_gameSize || m_insets.any());
    if (!wanted && !m_pinned) {
        return;
    }
    m_pinned = wanted;
    const auto view = camera();
    if (!view) {
        return;
    }
    const float zoom = view->zoom > 0.0f ? view->zoom : 1.0f;
    m_scene->forEach([&](const Entity& entity, int) {
        if (entity.parent() != view->entity) {
            return;
        }
        ax::Node* node = nodeFor(entity.id());
        if (node == nullptr) {
            return;
        }
        const Transform& t = entity.transform();
        ScreenPin pin;
        if (wanted) {
            Vec2 coverage;
            if (const Shape* shape = entity.get<Shape>(); shape != nullptr && shape->filled && shape->kind == ShapeKind::Rectangle) {
                coverage = {shape->size.x * std::abs(t.scale.x) * zoom, shape->size.y * std::abs(t.scale.y) * zoom};
            }
            pin = pinToScreen({t.position.x * zoom, t.position.y * zoom}, coverage, m_gameSize, m_visible, m_insets);
        }
        node->setPosition(t.position.x + pin.offset.x / zoom, t.position.y + pin.offset.y / zoom);
        node->setScale(t.scale.x * pin.scale.x, t.scale.y * pin.scale.y);
    });
}

ax::Node* SceneRenderer::nodeFor(EntityId id) const
{
    const auto it = m_nodes.find(id);
    return it != m_nodes.end() ? it->second : nullptr;
}

void SceneRenderer::moveNodes()
{
    m_builtRevision = m_scene->revision();
    for (const auto& [id, node] : m_nodes) {
        if (const Entity* entity = m_scene->find(id)) {
            const Transform& t = entity->transform();
            node->setPosition(t.position.x, t.position.y);
            node->setRotation(t.rotation);
            node->setScale(t.scale.x, t.scale.y);
        }
    }
}

std::optional<CameraView> SceneRenderer::camera() const
{
    if (m_scene == nullptr) {
        return std::nullopt;
    }
    std::optional<CameraView> found;
    m_scene->forEach([&](const Entity& entity, int) {
        if (found) {
            return;
        }
        const Camera* camera = entity.get<Camera>();
        if (camera == nullptr) {
            return;
        }
        for (const Entity* e = &entity; e != nullptr; e = m_scene->find(e->parent())) {
            if (!e->isActive()) {
                return;
            }
        }
        found = CameraView{m_scene->worldTransform(entity.id()).apply({0.0f, 0.0f}), camera->zoom, entity.id()};
    });
    return found;
}

ax::Node* SceneRenderer::makeSpriteNode(const Sprite& sprite)
{
    const AssetRecord* asset = m_assets != nullptr && !sprite.image.isNull() ? m_assets->find(sprite.image) : nullptr;
    ax::Texture2D* texture = nullptr;
    if (asset != nullptr && asset->type == AssetType::Image && asset->isUsable() && m_textures != nullptr) {
        texture = m_textures->get(m_assets->absolutePath(*asset), asset->filter == TextureFilter::Pixel);
    }
    if (texture == nullptr) {
        // No image chosen yet: draw nothing in the game; a missing or broken image: a placeholder.
        return sprite.image.isNull() ? ax::Node::create() : makePlaceholder(sprite.size);
    }
    auto* node = ax::Sprite::createWithTexture(texture);
    const ax::Size natural = node->getContentSize();
    if (natural.width > 0.0f && natural.height > 0.0f) {
        node->setScale(sprite.size.x / natural.width, sprite.size.y / natural.height);
    }
    node->setFlippedX(sprite.flipX);
    node->setFlippedY(sprite.flipY);
    node->setColor(ax::Color3B(sprite.color.r, sprite.color.g, sprite.color.b));
    node->setOpacity(sprite.color.a);
    return node;
}

ax::Node* SceneRenderer::makeTilemapNode(const Tilemap& map)
{
    auto* root = ax::Node::create();
    root->setName("Tiles");
    // The tileset, its image and the image's texture, if all are usable.
    const AssetRecord* tilesetAsset = m_assets != nullptr && !map.tileset.isNull() ? m_assets->find(map.tileset) : nullptr;
    const Tileset* tileset = tilesetAsset != nullptr && tilesetAsset->tileset ? &*tilesetAsset->tileset : nullptr;
    const AssetRecord* image = tileset != nullptr && m_assets != nullptr ? m_assets->find(tileset->image) : nullptr;
    ax::Texture2D* texture = nullptr;
    if (image != nullptr && image->isUsable() && image->image && m_textures != nullptr) {
        texture = m_textures->get(m_assets->absolutePath(*image), image->filter == TextureFilter::Pixel);
    }
    const float cw = map.cellSize.x;
    const float ch = map.cellSize.y;
    for (std::size_t i = 0; i < map.layers.size(); ++i) {
        const TileLayer& layer = map.layers[i];
        auto* layerNode = ax::Node::create();
        layerNode->setName(layer.name);
        layerNode->setVisible(layer.visible);
        root->addChild(layerNode, static_cast<int>(i));
        if (texture == nullptr) {
            if (layer.tiles.empty() || map.tileset.isNull()) {
                continue;
            }
            // Tiles whose tileset or image is missing: pink cells, impossible to miss.
            auto* draw = ax::DrawNode::create();
            for (const auto& [cell, tile] : layer.tiles) {
                const ax::Vec2 lo(static_cast<float>(cell.x) * cw, static_cast<float>(cell.y) * ch);
                draw->drawSolidRect(lo, lo + ax::Vec2(cw, ch), ax::Color4F(1.0f, 0.2f, 0.75f, 0.35f));
                draw->drawRect(lo, lo + ax::Vec2(cw, ch), ax::Color4F(1.0f, 0.2f, 0.75f, 1.0f), 1.0f);
            }
            layerNode->addChild(draw);
            continue;
        }
        const int imageWidth = image->image->width;
        const int count = tileset->columns(imageWidth) * tileset->rows(image->image->height);
        for (const auto& [cell, tile] : layer.tiles) {
            if (tile < 0 || tile >= count) {
                continue; // a tile number the tileset does not have (e.g. after changing the tile size)
            }
            const TileRect r = tileset->tileRect(tile, imageWidth);
            // Taken from a hair inside the tile's edges (see kInset).
            const ax::Rect rect = insetRect(r);
            const float w = rect.size.width;
            const float h = rect.size.height;
            auto* sprite = ax::Sprite::createWithTexture(texture, rect);
            sprite->setAnchorPoint(ax::Vec2::ZERO);
            sprite->setPosition(static_cast<float>(cell.x) * cw, static_cast<float>(cell.y) * ch);
            sprite->setScale(cw / w, ch / h);
            layerNode->addChild(sprite);
        }
    }
    return root;
}

ax::Node* SceneRenderer::makeAnimatedNode(EntityId id, const Animator& animator, const Sprite* sprite)
{
    const AssetRecord* asset = m_assets != nullptr ? m_assets->find(animator.animations) : nullptr;
    const AnimationSet* set = asset != nullptr && asset->animation ? &*asset->animation : nullptr;
    const AssetRecord* image = set != nullptr && !set->image.isNull() ? m_assets->find(set->image) : nullptr;
    ax::Texture2D* texture = nullptr;
    if (image != nullptr && image->isUsable() && image->image && m_textures != nullptr) {
        texture = m_textures->get(m_assets->absolutePath(*image), image->filter == TextureFilter::Pixel);
    }
    // Without a Sprite, a frame is drawn at its size in pixels.
    const Vec2 size = sprite != nullptr ? sprite->size
                      : set != nullptr  ? Vec2{static_cast<float>(set->frameWidth), static_cast<float>(set->frameHeight)}
                                        : Vec2{32.0f, 32.0f};
    const int frameCount = texture != nullptr ? set->columns(image->image->width) * set->rows(image->image->height) : 0;
    if (frameCount <= 0) {
        // A missing set or image, or frames larger than the image.
        return makePlaceholder(size);
    }
    AnimatedSprite animated{id, nullptr, *set, image->image->width, frameCount, -1};
    const ax::Rect first = insetRect(set->frameRect(0, animated.imageWidth));
    auto* node = ax::Sprite::createWithTexture(texture, first);
    // Every frame has the same size, so this scale suits them all.
    node->setScale(size.x / first.size.width, size.y / first.size.height);
    if (sprite != nullptr) {
        node->setFlippedX(sprite->flipX);
        node->setFlippedY(sprite->flipY);
        node->setColor(ax::Color3B(sprite->color.r, sprite->color.g, sprite->color.b));
        node->setOpacity(sprite->color.a);
    }
    animated.sprite = node;
    showFrame(animated, animator);
    m_animated.push_back(animated);
    return node;
}

void SceneRenderer::showFrame(AnimatedSprite& animated, const Animator& animator)
{
    int frame = animatorFrame(animated.set, animator, m_clock);
    if (frame < 0 || frame >= animated.frameCount) {
        frame = 0;   // a clip the set lacks, or a frame the image lacks: the sheet's first frame
    }
    if (frame != animated.shown) {
        animated.sprite->setTextureRect(insetRect(animated.set.frameRect(frame, animated.imageWidth)));
        animated.shown = frame;
    }
}

void SceneRenderer::animate(double clock)
{
    m_clock = clock;
    if (m_scene == nullptr) {
        return;
    }
    for (AnimatedSprite& animated : m_animated) {
        const Entity* entity = m_scene->find(animated.id);
        const Animator* animator = entity != nullptr ? entity->get<Animator>() : nullptr;
        if (animator != nullptr) {
            showFrame(animated, *animator);
        }
    }
}

ax::Node* SceneRenderer::makeTextNode(const Text& text)
{
    if (text.text.empty()) {
        return nullptr;
    }
    // Text is drawn by Rolo's own font renderer (TextSprites.hpp, ADR 0025).
    // The font: a font asset's file, or Rolo's built-in font (an empty name).
    std::string font;
    const AssetRecord* asset = m_assets != nullptr && !text.font.isNull() ? m_assets->find(text.font) : nullptr;
    if (asset != nullptr && asset->type == AssetType::Font && asset->isUsable()) {
        font = m_assets->absolutePath(*asset).string();
    }
    // Drawn at twice the size and scaled down, so text stays sharp when the
    // view is zoomed in a little (and on Retina screens).
    constexpr float kOversample = 2.0f;
    const float size = std::max(1.0f, text.size * kOversample);
    const float anchorX = text.align == TextAlign::Left ? 0.0f : text.align == TextAlign::Right ? 1.0f : 0.5f;
    TextSpriteOptions options;
    options.wrapWidth = std::max(0.0f, text.width) * kOversample;
    options.align = text.align == TextAlign::Left    ? ui::TextImageAlign::Left
                    : text.align == TextAlign::Right ? ui::TextImageAlign::Right
                                                     : ui::TextImageAlign::Center;
    options.anchor = {anchorX, 1.0f};

    auto* root = ax::Node::create();
    root->setScale(1.0f / kOversample);
    // Runs of plain lines are one picture each; a line with button pictures
    // ({button:...} became icon tokens) is words and pictures side by side
    // (such lines are not wrapped).
    float y = 0.0f;
    std::string plain;
    bool havePlain = false;
    const auto flushPlain = [&] {
        if (!havePlain) {
            return;
        }
        const TextSprite block = makeTextSprite(font, plain, size, text.color, options);
        if (block.sprite != nullptr) {
            block.sprite->setPositionY(y);
            root->addChild(block.sprite);
            y -= std::max(block.height, block.lineHeight);
        }
        plain.clear();
        havePlain = false;
    };
    std::size_t start = 0;
    while (start <= text.text.size()) {
        const std::size_t end = std::min(text.text.find('\n', start), text.text.size());
        const std::string line = text.text.substr(start, end - start);
        start = end + 1;
        std::vector<TextPiece> pieces;
        if (line.find("{icon:") != std::string::npos) {
            pieces = splitIcons(line);
        }
        if (std::none_of(pieces.begin(), pieces.end(), [](const TextPiece& p) { return p.icon.has_value(); })) {
            if (havePlain) {
                plain += '\n';
            }
            plain += line;
            havePlain = true;
            continue;
        }
        flushPlain();
        auto* row = ax::Node::create();
        float x = 0.0f;
        float lineHeight = size * 1.2f;
        TextSpriteOptions wordOptions;
        wordOptions.anchor = {0.0f, 1.0f};
        for (const TextPiece& piece : pieces) {
            if (!piece.icon) {
                const TextSprite words = makeTextSprite(font, piece.text, size, text.color, wordOptions);
                if (words.sprite != nullptr) {
                    words.sprite->setPosition(x, 0.0f);
                    row->addChild(words.sprite);
                    lineHeight = std::max(lineHeight, words.lineHeight);
                }
                // Measured, not the sprite's width: spaces at the end of the
                // words keep them apart from the next picture.
                x += textWidth(font, piece.text, size);
                continue;
            }
            const float iconHeight = size * 1.15f;
            ax::Node* icon = makeButtonIcon(*piece.icon, iconHeight, std::string());
            // Centred on the line, with a little room either side.
            icon->setPosition(x + size * 0.08f, -lineHeight * 0.5f - iconHeight * 0.5f);
            icon->setCascadeOpacityEnabled(true);
            icon->setOpacity(text.color.a);
            row->addChild(icon);
            x += icon->getContentSize().width + size * 0.16f;
        }
        row->setPosition(-x * anchorX, y);
        root->addChild(row);
        y -= lineHeight;
    }
    flushPlain();
    return root;
}

void SceneRenderer::rebuild()
{
    if (m_container != nullptr) {
        m_container->removeFromParent();
        m_container = nullptr;
    }
    m_nodes.clear();
    m_animated.clear();
    m_builtScene = m_scene.get();
    m_builtRevision = m_scene != nullptr ? m_scene->revision() : 0;
    m_builtContentRevision = m_scene != nullptr ? m_scene->contentRevision() : 0;
    m_builtAssets = m_assets;
    m_builtAssetsRevision = m_assets != nullptr ? m_assets->revision() : 0;
    ++m_builds;
    if (m_scene == nullptr) {
        m_builtTextures = m_textures != nullptr ? m_textures->generation() : 0;
        return;
    }

    m_container = ax::Node::create();
    m_container->setName(m_scene->name());
    m_root.addChild(m_container);

    auto& nodes = m_nodes;
    m_scene->forEach([&](const Entity& entity, int) {
        auto* node = ax::Node::create();
        node->setName(entity.name());
        const Transform& t = entity.transform();
        node->setPosition(t.position.x, t.position.y);
        node->setRotation(t.rotation); // both Axmol and Rolo: degrees, clockwise
        node->setScale(t.scale.x, t.scale.y);
        node->setVisible(entity.isActive());

        // The entity's own content, drawn below its children (local z -1).
        if (m_drawContent) {
            if (const Shape* shape = entity.get<Shape>()) {
                auto* draw = ax::DrawNode::create();
                drawShape(*draw, *shape);
                node->addChild(draw, -1);
            }
            // An Animator with an animation set draws the Sprite itself (its
            // frames replace the Sprite's image).
            const Animator* animator = entity.get<Animator>();
            const Sprite* sprite = entity.get<Sprite>();
            if (animator != nullptr && !animator->animations.isNull()) {
                node->addChild(makeAnimatedNode(entity.id(), *animator, sprite), -1);
            } else if (sprite != nullptr) {
                node->addChild(makeSpriteNode(*sprite), -1);
            }
            if (const Tilemap* map = entity.get<Tilemap>()) {
                node->addChild(makeTilemapNode(*map), -1);
            }
            if (const Text* text = entity.get<Text>()) {
                if (ax::Node* label = makeTextNode(*text)) {
                    node->addChild(label, -1);
                }
            }
        }

        ax::Node* parent = m_container;
        if (const auto it = nodes.find(entity.parent()); it != nodes.end()) {
            parent = it->second;
        }
        // Sibling order is drawing order.
        parent->addChild(node, static_cast<int>(m_scene->indexOf(entity.id()).value_or(0)));
        nodes.emplace(entity.id(), node);
    });
    // Loading textures above may itself have bumped the generation.
    m_builtTextures = m_textures != nullptr ? m_textures->generation() : 0;
}

} // namespace rolo::detail
