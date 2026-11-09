# Physics

Rolo has 2D rigid-body physics: things fall, land, slide, tumble, bounce
and push each other. It is built on [Box2D](https://box2d.org) (MIT), the
library behind a great many 2D games.

The quickest way to see it: **File > New Project… > Platformer Starter**,
then press Play. Run and jump with the arrow keys and Space, push the
crates, knock the balls about, ride the lift.

## Three pieces

| Piece | What it does |
|---|---|
| **Collider** (component) | the object's shape: a box, a circle or a capsule |
| **Rigid Body** (component) | makes physics move the object |
| **Gravity** (a scene setting) | which way, and how hard, things fall |

### Rigid Body

Add **Add Component › Physics › Rigid Body** to an object that has a
**Collider**.

| Property | Meaning |
|---|---|
| Type | **Dynamic**: physics moves it (crates, balls, a platformer's hero). **Kinematic**: moves only at the velocity code gives it, and pushes dynamic bodies (lifts, moving platforms). **Static**: never moves (a wall that needs a bounciness) |
| Mass | in kilograms; heavy things push light things |
| Gravity Scale | 1 the scene's gravity, 0 floats, 2 falls twice as fast, negative falls up |
| Friction | 0 slides like ice, 1 grips like rubber |
| Bounciness | 0 lands with a thud, 1 bounces back as high as it fell from |
| Drag | slows it down as it moves, like air or water |
| Lock Rotation | never turns over: for characters |
| Fast Moving | for small, fast things such as bullets: checked continuously so they never pass through walls |

### Collider shapes

A Collider's **Shape** fits inside its box (**Size**):

- **Box**: the box itself.
- **Circle**: the largest circle inside the box. Balls, wheels, boulders.
- **Capsule**: the box with fully rounded ends, upright if the box is taller
  than it is wide. **Use it for characters**: it slides over the seams
  between tiles and up small steps, where a box would catch.

The Scene view draws each collider as its shape: green for solid, blue for
areas that are not solid, orange for objects with a Rigid Body.

### Gravity

Select the scene (its row at the top of the Hierarchy). The Inspector shows
**Physics › Gravity**, in points per second per second:

- **Platformer** (the default): 0, -980, pulling down.
- **Top-Down**: 0, 0. Nothing falls; things still slide and collide, so a
  top-down hero can push crates around.

## What takes part

Physics runs only in scenes with at least one Rigid Body. A scene without
one (like the Top-Down Starter or The Lantern Keeper) behaves exactly as it
always did. In a scene with physics:

- objects with a **Rigid Body** and a **Collider** are moved by physics;
- objects with a **solid Collider** and no Rigid Body are fixed obstacles.
  If code moves one (the Top-Down Controller, `Game::move`, a script's
  `setPosition`), it pushes dynamic bodies out of its way;
- **solid tiles** are the ground and walls (the tiles marked solid in the
  tileset, and cells painted on the Collision layer). Neighbouring tiles are
  joined into long floors and walls, so bodies slide along them smoothly;
- colliders that are not solid stay out of physics: they are areas for
  Triggers and overlap checks, as before.

Physics steps 60 times a second, whatever the frame rate, and gives the same
result every time the game is played.

## Scripts

```lua
-- A platformer hero: run with the arrows, jump from the ground.
function update(self, dt)
    local dx = input.direction()
    local vx, vy = self.object:velocity()
    self.object:setVelocity(dx * 280, vy)          -- keep falling as before
    if self.object:isOnGround() and input.wasPressed("space") then
        self.object:setVelocity(dx * 280, 640)     -- jump
    end
end

-- Called when this object starts touching another.
function collide(self, other)
    if other:name() == "Spikes" then
        self.object:setPosition(100, 200)
    end
end
```

| Function | What it does |
|---|---|
| `obj:velocity()`, `obj:setVelocity(x, y)` | speed in points per second |
| `obj:applyImpulse(x, y)` | a sudden push (a jump, a hit): a 1 kg body pushed by `0, 500` gains 500 points per second upwards |
| `obj:applyForce(x, y)` | a steady push for this frame (wind, a rocket) |
| `obj:spin()`, `obj:setSpin(degrees)` | turning speed, degrees per second, clockwise |
| `obj:isOnGround()` | true if it stands on something (below it, against gravity) |
| `obj:contacts()` | the objects it touches now |
| `game.gravity()`, `game.setGravity(x, y)` | the scene's gravity while the game runs |
| `function collide(self, other)` | called when the script's object starts touching `other` |

These need a Rigid Body on the object, and say so if it has none. Moving an
object with `setPosition` still works: physics carries on from there. The
full list is in the [scripting guide](scripting.md#physics).

## C++

```cpp
void Hero::update(rolo::Game& game, float dt)
{
    rolo::Vec2 v = game.velocity(entity());
    v.x = game.input().moveDirection().x * 280.0f;
    if (game.isOnGround(entity()) && game.input().wasPressed(rolo::Key::Space)) {
        v.y = 640.0f;
    }
    game.setVelocity(entity(), v);
}

void Hero::collide(rolo::Game& game, rolo::EntityId other) { /* ... */ }
```

`Game` also has `applyImpulse`, `applyForce`, `spin`, `setSpin`,
`touching`, `gravity` and `setGravity` ([gameplay guide](gameplay.md#physics)).

## Tips

- **Characters**: a capsule Collider, a Rigid Body with **Lock Rotation**
  and **Friction 0** (so they do not stick to walls), moved by setting
  their velocity.
- **Big and small**: physics is tuned for objects from about 6 to 600
  points across. Much larger or smaller still works, less precisely.
- **Scale**: an object's scale (and its parents') scales its collider.
- Changing a Collider or Rigid Body while the game runs remakes the body,
  keeping its speed.

## Not yet

Joints (hinges, ropes, springs), one-way platforms, ray casts, collision
layers (what collides with what) and physics in the editor's Scene view
while not playing are not there yet. See the [roadmap](roadmap.md).
