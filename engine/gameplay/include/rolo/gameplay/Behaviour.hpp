#pragma once

// Behaviour: gameplay code, written in C++, attached to an object.
//
// A behaviour is a component with code. Like any component it describes its
// properties, so the editor shows them in the Inspector and scenes save them;
// unlike other components it also gets called while the game runs:
//
//     struct Spinner : rolo::BehaviourBase<Spinner> {
//         float speed = 90.0f;   // degrees per second
//
//         void update(rolo::Game& game, float dt) override
//         {
//             game.transform(entity())->rotation += speed * dt;
//         }
//
//         static const rolo::ComponentType& componentType()
//         {
//             static const rolo::ComponentType type =
//                 rolo::ComponentTypeBuilder<Spinner>("mygame.Spinner", "Spinner")
//                     .category("Gameplay")
//                     .property("speed", "Speed", &Spinner::speed, {.unit = "°/s"})
//                     .build();
//             return type;
//         }
//     };
//
// Game modules register their behaviours (see GameModule.hpp). While the
// game runs, each active object's behaviours get start() once and then
// update() every frame, in hierarchy order (parents before children, top to
// bottom), and in the order the components are listed on an object.
//
// The game always runs on a copy of the scene: whatever a behaviour changes,
// including its own fields, is gone when the game stops.

#include "rolo/scene/Component.hpp"
#include "rolo/scene/EntityId.hpp"

#include <memory>

namespace rolo {

class Game;

class Behaviour : public Component {
public:
    /// The object this behaviour is attached to.
    EntityId entity() const noexcept { return m_entity; }

    /// Called once, in the first frame the object is active in a running game.
    virtual void start(Game& /*game*/) {}
    /// Called every frame while the game runs (not while it is paused).
    /// `dt` is the time since the previous frame, in seconds.
    virtual void update(Game& /*game*/, float /*dt*/) {}
    /// Physics (RigidBody): called when this object starts touching `other`,
    /// after the step in which it happened. Only in scenes with physics.
    virtual void collide(Game& /*game*/, EntityId /*other*/) {}

protected:
    Behaviour() = default;
    // Copying copies the properties, not the running state: a copy starts fresh.
    Behaviour(const Behaviour& other)
        : Component(other)
    {
    }
    Behaviour& operator=(const Behaviour& other)
    {
        Component::operator=(other);
        return *this;
    }

private:
    friend class Game;
    EntityId m_entity;
    bool m_started = false;
    bool m_failed = false;   // threw an error: switched off for the rest of the run
};

/// Base for concrete behaviours: provides type() and clone(), like ComponentBase.
template <class Derived>
class BehaviourBase : public Behaviour {
public:
    const ComponentType& type() const override { return Derived::componentType(); }
    std::unique_ptr<Component> clone() const override
    {
        // A copy is a new behaviour: it gets its own start() (e.g. when a
        // game copies an object while running).
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }
};

} // namespace rolo
