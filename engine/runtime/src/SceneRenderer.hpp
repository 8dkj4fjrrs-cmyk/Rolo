#pragma once

// SceneRenderer: shows a rolo::Scene with Axmol nodes.
//
// Rolo scenes are plain data; Axmol draws node trees. The renderer keeps
// an Axmol node tree that mirrors the scene: one node per entity, carrying
// the entity's transform, with its visible content (a Shape's DrawNode, a
// Sprite's image) as a child drawn below the entity's own children.
//
// When the scene changes (its revision number goes up), or the project's
// assets change, the mirror is rebuilt. Rebuilding everything is simple and
// fast enough for editor-sized scenes. The common case while a game runs,
// things moving, is cheaper: if only Transforms changed (the scene's
// contentRevision() stayed the same), the existing nodes are just moved.
//
// Animated objects (an Animator with an animation set) are sprites whose
// image rectangle changes over time. animate() runs every frame after
// update(): it works out each Animator's frame from the animation clock and
// moves the rectangle only when the frame changed. Switching clips needs no
// rebuild either, because the Animator is read again every frame.
//
// Internal to rolo_runtime.

#include "rolo/assets/Animation.hpp"
#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/scene/Scene.hpp"

#include <axmol.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rolo::detail {

class TextureStore;

/// Where the scene's Camera is looking, in world coordinates.
struct CameraView {
    Vec2 center;
    float zoom = 1.0f;
};

class SceneRenderer {
public:
    /// `root` is the node the scene is drawn under (owned by the Axmol scene).
    /// `textures` loads images for sprites; may be null (sprites then show a
    /// placeholder). With `drawContent` false, entities get plain nodes only:
    /// DrawNodes and sprites need a graphics context, which unit tests lack.
    SceneRenderer(ax::Node& root, TextureStore* textures, bool drawContent = true);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    /// Shows `scene` (nothing for null). Takes effect at the next update().
    void show(std::shared_ptr<const Scene> scene);
    const std::shared_ptr<const Scene>& scene() const noexcept { return m_scene; }

    /// The project's assets, used to find sprite images (null: no project).
    /// Must stay alive while set.
    void setAssets(const AssetDatabase* assets);

    /// Rebuilds the Axmol nodes if the scene, the assets or the loaded
    /// images changed.
    void update();

    /// Shows each Animator's frame for the animation clock `clock` (seconds;
    /// see rolo::Animator). Call every frame, after update().
    void animate(double clock);

    /// The first active Camera in the scene, if there is one.
    std::optional<CameraView> camera() const;

    /// Number of times the node tree has been built (for diagnostics and tests).
    std::uint64_t buildCount() const noexcept { return m_builds; }
    /// The node mirroring an entity (for tests); null if none.
    ax::Node* nodeFor(EntityId id) const;

private:
    void rebuild();
    void moveNodes();
    ax::Node* makeSpriteNode(const Sprite& sprite);
    ax::Node* makeTilemapNode(const Tilemap& map);
    ax::Node* makeAnimatedNode(EntityId id, const Animator& animator, const Sprite* sprite);
    ax::Node* makeTextNode(const Text& text);

    ax::Node& m_root;
    TextureStore* m_textures;
    bool m_drawContent;
    const AssetDatabase* m_assets = nullptr;
    ax::Node* m_container = nullptr;   // child of m_root; holds the mirror
    std::shared_ptr<const Scene> m_scene;
    const Scene* m_builtScene = nullptr;
    std::uint64_t m_builtRevision = 0;
    std::uint64_t m_builtContentRevision = 0;
    std::unordered_map<EntityId, ax::Node*> m_nodes;   // owned by m_container's tree
    const AssetDatabase* m_builtAssets = nullptr;
    std::uint64_t m_builtAssetsRevision = 0;
    std::uint64_t m_builtTextures = 0;
    std::uint64_t m_builds = 0;

    // A sprite showing an Animator's frames. It keeps its own copy of the
    // animation set: the asset database may rebuild its records at any
    // refresh (pointers into it do not last), and a changed set rebuilds the
    // scene anyway because the database's revision goes up.
    struct AnimatedSprite {
        EntityId id;
        ax::Sprite* sprite = nullptr;   // owned by the node tree
        AnimationSet set;
        int imageWidth = 0;
        int frameCount = 0;
        int shown = -1;                 // the frame showing now
    };
    void showFrame(AnimatedSprite& animated, const Animator& animator);
    std::vector<AnimatedSprite> m_animated;
    double m_clock = 0.0;   // the last clock passed to animate()
};

} // namespace rolo::detail
