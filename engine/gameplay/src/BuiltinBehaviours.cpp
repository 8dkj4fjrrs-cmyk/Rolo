#include "rolo/gameplay/BuiltinBehaviours.hpp"

#include "rolo/assets/Animation.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/Script.hpp"

#include <algorithm>

namespace rolo {

namespace {

// True if the object's Animator has a clip with this id.
bool hasClip(const Game& game, EntityId id, const std::string& clip)
{
    const Entity* e = game.scene().find(id);
    const Animator* animator = e != nullptr ? e->get<Animator>() : nullptr;
    if (animator == nullptr || game.assets() == nullptr) {
        return false;
    }
    const AssetRecord* set = game.assets()->find(animator->animations);
    return set != nullptr && set->animation && set->animation->findClip(clip) != nullptr;
}

} // namespace

// --- TopDownController ---------------------------------------------------------------------------

void TopDownController::update(Game& game, float dt)
{
    // In a dialogue the arrow keys choose answers, so it stands still.
    const Vec2 direction = game.isInDialogue() ? Vec2{} : game.input().moveDirection();
    const bool moving = direction.x != 0.0f || direction.y != 0.0f;
    if (moving) {
        game.move(entity(), direction * (speed * dt));   // stops at anything solid, sliding along walls
        if (direction.x != 0.0f) {
            facing = direction.x < 0.0f ? "left" : "right";   // sideways wins on diagonals
        } else {
            facing = direction.y > 0.0f ? "up" : "down";
        }
    }
    const std::string clip = (moving ? "walk_" : "idle_") + facing;
    if (hasClip(game, entity(), clip)) {
        game.play(entity(), clip);   // carries on if it is already playing
        return;
    }
    // No such clip: face left or right by flipping the Sprite (only when it
    // changes: changing a sprite redraws it, moving does not).
    const Entity* self = game.scene().find(entity());
    const Sprite* sprite = self != nullptr ? self->get<Sprite>() : nullptr;
    const bool faceLeft = facing == "left";
    if (sprite != nullptr && (facing == "left" || facing == "right") && sprite->flipX != faceLeft) {
        game.get<Sprite>(entity())->flipX = faceLeft;
    }
}

const ComponentType& TopDownController::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<TopDownController>("rolo.TopDownController", "Top-Down Controller")
            .category("Gameplay")
            .description("Walks the object with the arrow keys or W A S D, stopped by anything solid (give it a "
                         "Collider). With an Animator it plays walk_down / walk_left / walk_right / walk_up and "
                         "idle_... clips; without them it flips the Sprite to face left or right.")
            .property("speed", "Speed", &TopDownController::speed,
                      {.min = 0.0f, .step = 10.0f, .help = "How fast it walks, in points per second.", .unit = "pt/s"})
            .build();
    return type;
}

// --- SceneDoor -------------------------------------------------------------------------------------

void SceneDoor::update(Game& game, float)
{
    if (scene.isNull()) {
        return;
    }
    for (const EntityId other : game.overlapping(entity())) {
        const Entity* e = game.scene().find(other);
        if (e != nullptr && e->get<Interactor>() != nullptr) {
            game.changeScene(scene, arriveAt);
            return;
        }
    }
}

const ComponentType& SceneDoor::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<SceneDoor>("rolo.SceneDoor", "Scene Door")
            .category("Gameplay")
            .description("When the player (the object with the Interactor) walks into this object's Collider (make "
                         "it not solid), the game goes to another scene. The player appears at the object named "
                         "Arrive At there: put an empty object a step inside the way back, so they do not walk "
                         "straight out again.")
            .property("scene", "Scene", &SceneDoor::scene, {.help = "The scene to go to.", .assetType = "scene"})
            .property("arriveAt", "Arrive At", &SceneDoor::arriveAt,
                      {.help = "The name of an object in that scene. Empty: where that scene puts the player."})
            .build();
    return type;
}

// --- CameraBounds -----------------------------------------------------------------------------------

void CameraBounds::update(Game& game, float)
{
    const Entity* self = game.scene().find(entity());
    if (self == nullptr) {
        return;
    }
    const Camera* camera = self->get<Camera>();
    const float zoom = camera != nullptr && camera->zoom > 0.0f ? camera->zoom : 1.0f;
    const EntityId parent = self->parent();
    // Where the camera would be without bounds: on its parent, or (a camera
    // at the top of the scene) where it was placed.
    const Vec2 anchor = parent.isNull() ? self->transform().position : game.worldPosition(parent);
    const auto clampAxis = [](float value, float lo, float hi, float half) {
        if (hi - lo <= half * 2.0f) {
            return (lo + hi) * 0.5f;   // the area is smaller than the view: centre it
        }
        return std::clamp(value, lo + half, hi - half);
    };
    // The screen may show more than the game size (ScreenFit::Expand).
    const Vec2 seen{std::max(view.x, game.viewSize().x), std::max(view.y, game.viewSize().y)};
    const Vec2 wanted{clampAxis(anchor.x, min.x, max.x, seen.x * 0.5f / zoom),
                      clampAxis(anchor.y, min.y, max.y, seen.y * 0.5f / zoom)};
    // Back into the parent's own coordinates (it may be scaled).
    Vec2 local = wanted;
    if (!parent.isNull()) {
        if (const auto inverse = game.scene().worldTransform(parent).inverse()) {
            local = inverse->apply(wanted);
        }
    }
    Transform* t = game.transform(entity());
    if (t != nullptr && (t->position.x != local.x || t->position.y != local.y)) {
        t->position = local;
    }
}

const ComponentType& CameraBounds::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<CameraBounds>("rolo.CameraBounds", "Camera Bounds")
            .category("Gameplay")
            .description("Put on the Camera (a child of the player): the view follows the player but stays inside "
                         "the area from Min to Max, so it never shows past the map's edges.")
            .property("min", "Min", &CameraBounds::min, {.help = "The area's bottom-left corner, in world points."})
            .property("max", "Max", &CameraBounds::max, {.help = "The area's top-right corner, in world points."})
            .property("view", "View Size", &CameraBounds::view,
                      {.min = 1.0f, .help = "The game's size in points (1280 x 720 by default). If the screen shows more "
                                            "(a project that fills screens of other shapes), that is used."})
            .build();
    return type;
}

void registerBuiltinBehaviours(ComponentRegistry& registry)
{
    static_cast<void>(registry.add(TopDownController::componentType()));
    static_cast<void>(registry.add(SceneDoor::componentType()));
    static_cast<void>(registry.add(CameraBounds::componentType()));
    static_cast<void>(registry.add(Script::componentType()));
}

} // namespace rolo
