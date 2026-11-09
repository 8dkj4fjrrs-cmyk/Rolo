# ADR 0016: 2D physics with Box2D

- **Status:** Accepted
- **Date:** 2026-11-09

## Context

Rolo's collision was boxes and code: `Game::move` slides a box along walls
and overlap queries find areas. That is enough for top-down RPGs, and most
of what Rolo had was shaped by one. Rolo is meant for all kinds of 2D games,
and platformers, physics puzzles and anything that falls, bounces or is
pushed need rigid-body physics: gravity, mass, friction, restitution,
rotation, stacking and continuous collision.

The project owner's rule for dependencies: MIT licences only; if there is no
MIT library, write Rolo's own, and say so.

Options:
- **Box2D 3.x**: the standard 2D physics engine, MIT since 3.0 (2.4 was
  MIT too, 2.3 zlib). C17, no dependencies, deterministic across machines
  when built without fused multiply-add, and tuned over many years.
- **Chipmunk2D**: MIT, but no longer maintained.
- **Our own**: rigid bodies with stable stacking, friction and continuous
  collision are years of specialist work; a home-made solver would be
  worse at everything a game notices (jitter, tunnelling, sinking stacks).
- **Axmol's physics**: Axmol 2 wraps Box2D (an older 2.x) and Chipmunk, tied
  to its node tree; Rolo keeps scenes as its own data (ADR 0005).

Box2D is MIT, so the rule needs nothing of our own.

## Decision

Use **Box2D 3.2.0**, kept unmodified in `third_party/box2d/` (library only;
like Lua and miniaudio), built as `rolo_box2d` with Box2D's own settings
(C17, `-ffp-contract=off`, the AVX2 solver chosen at run time).

The parts:
- `rolo::PhysicsWorld` (`engine/physics`): Box2D in Rolo's units: points
  (64 per metre), y up, degrees clockwise like `Transform::rotation`,
  points per second, kilograms. No Box2D type appears in its header.
  Uses exact sine and cosine (Box2D's quick approximation is about 0.1° off,
  and an object placed at 10° must come back at 10°).
- Scene data (`engine/scene`):
  - the **RigidBody** component: dynamic, kinematic or static; mass (the
    whole body's, turned into a density for Box2D), gravity scale,
    friction, bounciness, drag, lock rotation, fast (bullet);
  - **Collider** gets a `shape`: box, circle (the largest in the box) or
    capsule (the box with round ends, for characters). Overlap queries and
    `Game::move` keep using the box around it;
  - scenes get a **gravity**, written only when it is not the default
    (0, -980), so existing scene files do not change.

  Both are added to version 1 of the scene format: files without them mean
  exactly what they meant before, and older Rolo ignores them with a warning.
- `PhysicsSync` (`engine/gameplay`) keeps a world in step with the scene a
  game plays:
  - physics starts only when the scene has a RigidBody, so existing games
    are untouched;
  - solid colliders without a RigidBody are static bodies; when code moves
    one, it becomes kinematic and is steered to its Transform each step, so
    it pushes crates. (Kinematic from the start, they made boxes resting on
    them slip.);
  - solid tiles become one static body of boxes merged into rows and blocks
    (no seams to catch on);
  - the world steps at a fixed 60 Hz (at most 4 steps a frame), and dynamic
    and kinematic RigidBodies write their positions back into their
    Transforms. A Transform moved by code teleports its body;
  - changing a Collider or RigidBody while playing remakes the body, keeping
    its velocity.
- `Game` API (`velocity`, `setVelocity`, `applyImpulse`, `applyForce`,
  `spin`, `setSpin`, `isOnGround`, `touching`, `gravity`, `setGravity`),
  `Behaviour::collide` and the Lua equivalents (`obj:velocity()`...,
  `collide(self, other)`).
- The editor: Collider shapes drawn as they are, the scene's **Gravity** in
  the Inspector (with Platformer and Top-Down presets), undoable.
- The **Platformer Starter** template, made by
  `templates/tools/make_platformer.py`, with original art and sounds.

A guard added on the way: component property keys cannot be `type` or
`data`, which scene files use for the component itself (RigidBody's
`type` property was first saved over its component type; the test caught it).

## Consequences

- Platformers and physics games without C++; top-down games can have
  pushable things with zero gravity.
- Physics is deterministic on one machine (tests check it); Box2D's build
  settings aim for the same results across machines too.
- Not yet: joints, one-way platforms, ray casts, collision filtering,
  physics previews in the Scene view, and tilemaps changed while playing
  (their bodies are built when the scene starts).
- `licenses/box2d.txt` goes with every game.
