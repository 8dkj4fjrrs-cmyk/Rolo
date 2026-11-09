#pragma once

// Components that connect scenes to RPG content, without code:
//
//   Interactor     marks the player: what walks into areas and presses the
//                  interact key.
//   Trigger        when something happens (the player interacts, walks in
//                  or out, the scene starts, time passes, a game event, a
//                  condition comes true), run actions if a condition holds.
//   DialogueView   an object shown only while a dialogue is going on (the
//                  dialogue box); Text children with a dialogue Source show
//                  its lines.
//
// Game (Game.hpp) runs them. The editor, the player and the tests add them to
// their ComponentRegistry with registerGameplayComponents().

#include "rolo/content/Logic.hpp"
#include "rolo/gameplay/Input.hpp"
#include "rolo/scene/Component.hpp"

namespace rolo {

/// The keys an Interactor can use to interact.
enum class InteractKey { E, Space, Enter, Z };
Key toKey(InteractKey key);

/// Marks the player (usually one object): Triggers react to it entering
/// their area and to its interact key. Needs a Collider.
struct Interactor : ComponentBase<Interactor> {
    InteractKey key = InteractKey::E;

    static const ComponentType& componentType();
};

enum class TriggerOn { Interact, EnterArea, LeaveArea, SceneStart, Timer, GameEvent, ConditionMet };

/// Runs actions when something happens. Interact, Enter Area and Leave Area
/// need a Collider on the same object (make it not solid): that is the area.
/// The condition and actions are kept as component data (see Reaction).
struct Trigger : ComponentBase<Trigger> {
    TriggerOn on = TriggerOn::Interact;
    std::string prompt = "Talk";   // Interact: shown as "[E] Talk"
    std::string event;             // GameEvent: the event's name
    float delay = 1.0f;            // Timer: seconds after the scene starts (and between repeats)
    bool once = false;             // runs only once per playthrough (remembered in saves)
    Reaction reaction;             // condition (optional) and actions

    std::string saveData() const override;
    std::string loadData(std::string_view json) override;
    static const ComponentType& componentType();
};

/// Shown only while a dialogue is going on. Put it on the dialogue box.
struct DialogueView : ComponentBase<DialogueView> {
    std::string marker = "> ";   // in front of the selected choice

    static const ComponentType& componentType();
};

/// Adds Interactor, Trigger, DialogueView and Sound (Sound.hpp).
void registerGameplayComponents(ComponentRegistry& registry);

} // namespace rolo
