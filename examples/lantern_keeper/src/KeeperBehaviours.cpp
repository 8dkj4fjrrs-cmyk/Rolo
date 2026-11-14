#include "KeeperBehaviours.hpp"

#include <algorithm>

namespace keeper {

using namespace rolo;

namespace {

// The object the player controls: the first with a PlayerController.
EntityId findPlayer(const Game& game)
{
    const std::vector<EntityId> players = game.findAll<PlayerController>();
    return players.empty() ? EntityId{} : players.front();
}

bool containsPlayer(const Game& game, const std::vector<EntityId>& ids)
{
    const EntityId player = findPlayer(game);
    return !player.isNull() && std::find(ids.begin(), ids.end(), player) != ids.end();
}

} // namespace

// --- PlayerController ------------------------------------------------------------------------

void PlayerController::update(Game& game, float dt)
{
    // In a dialogue the arrow keys choose answers, so the hero stands still.
    const Vec2 direction = game.isInDialogue() ? Vec2{} : game.input().moveDirection();
    if (direction.x == 0.0f && direction.y == 0.0f) {
        game.play(entity(), "idle_" + facing);   // carries on if it is already playing
        return;
    }
    // move() slides along walls and solid tiles instead of going through.
    game.move(entity(), direction * (speed * dt));

    // Face the way we walk; sideways wins on diagonals.
    if (direction.x != 0.0f) {
        facing = direction.x < 0.0f ? "left" : "right";
    } else {
        facing = direction.y > 0.0f ? "up" : "down";
    }
    game.play(entity(), "walk_" + facing);
}

const ComponentType& PlayerController::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<PlayerController>("keeper.PlayerController", "Player Controller")
            .category("Gameplay")
            .description("Walks with the arrow keys or W A S D and plays the walk_ and idle_ clips (down, left, "
                         "right, up) of the object's Animator. Give the object a Collider so walls stop it.")
            .property("speed", "Speed", &PlayerController::speed,
                      {.min = 0.0f, .step = 10.0f, .help = "How fast it walks, in points per second.", .unit = "pt/s"})
            .build();
    return type;
}

// --- SceneDoor ---------------------------------------------------------------------------------

void SceneDoor::update(Game& game, float)
{
    if (scene.isNull() || !containsPlayer(game, game.overlapping(entity()))) {
        return;
    }
    // The next scene's SpawnPoints read this when they start.
    game.content().state().setVariable(kArrivalVariable, arriveAt);
    game.changeScene(scene);
}

const ComponentType& SceneDoor::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<SceneDoor>("keeper.SceneDoor", "Scene Door")
            .category("Gameplay")
            .description("When the player walks into this object's Collider (make it not solid), the game goes to "
                         "another scene, and the player appears at the Spawn Point there named Arrive At.")
            .property("scene", "Scene", &SceneDoor::scene, {.help = "The scene to go to.", .assetType = "scene"})
            .property("arriveAt", "Arrive At", &SceneDoor::arriveAt,
                      {.help = "The name of the object with a Spawn Point in that scene."})
            .build();
    return type;
}

// --- SpawnPoint --------------------------------------------------------------------------------

void SpawnPoint::start(Game& game)
{
    const auto arrival = game.content().state().variable(kArrivalVariable);
    const std::string* name = arrival ? std::get_if<std::string>(&*arrival) : nullptr;
    const Entity* self = game.scene().find(entity());
    if (name == nullptr || self == nullptr || *name != self->name()) {
        return;
    }
    game.content().state().setVariable(kArrivalVariable, std::string());   // used up
    const EntityId player = findPlayer(game);
    if (Transform* t = game.transform(player)) {
        // The player is a top-level object, so its position is a world position.
        t->position = game.worldPosition(entity());
    }
}

const ComponentType& SpawnPoint::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<SpawnPoint>("keeper.SpawnPoint", "Spawn Point")
            .category("Gameplay")
            .description("Where the player appears when arriving through a Scene Door whose Arrive At is this "
                         "object's name. Place it a step away from the door back, so the player does not walk "
                         "straight out again.")
            .build();
    return type;
}

// --- CameraLimits --------------------------------------------------------------------------------

void CameraLimits::update(Game& game, float)
{
    const Entity* self = game.scene().find(entity());
    if (self == nullptr) {
        return;
    }
    // Where the camera would be with no offset: on its parent (the player).
    const EntityId parent = self->parent();
    const Vec2 anchor = parent.isNull() ? Vec2{} : game.worldPosition(parent);
    const auto clampAxis = [](float value, float lo, float hi, float half) {
        if (hi - lo <= half * 2.0f) {
            return (lo + hi) * 0.5f;   // the map is smaller than the view: centre it
        }
        return std::clamp(value, lo + half, hi - half);
    };
    // The screen may show more than the game size (a project that fills other screen shapes).
    const Vec2 seen{std::max(view.x, game.viewSize().x), std::max(view.y, game.viewSize().y)};
    const Vec2 wanted{clampAxis(anchor.x, min.x, max.x, seen.x * 0.5f),
                      clampAxis(anchor.y, min.y, max.y, seen.y * 0.5f)};
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

const ComponentType& CameraLimits::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<CameraLimits>("keeper.CameraLimits", "Camera Limits")
            .category("Gameplay")
            .description("Put on the Camera (a child of the player): the view follows the player but stays "
                         "inside the area from Min to Max, so it never shows past the map's edges.")
            .property("min", "Min", &CameraLimits::min, {.help = "The bottom-left corner of the area, in world points."})
            .property("max", "Max", &CameraLimits::max, {.help = "The top-right corner of the area, in world points."})
            .property("view", "View Size", &CameraLimits::view,
                      {.min = 1.0f, .help = "The game's view in points (the project's resolution)."})
            .build();
    return type;
}

} // namespace keeper
