# ADR 0032: Events in scripts (connect, once, wait)

- **Status:** Accepted; built (2026-11-16)
- **Follows:** ADR 0013 (Lua scripting), ADR 0019 (direction: for people who come from Roblox), ADR 0031 (waiting in scripts)

## The need

"When the gate opens, play a sound." "Wait until the player presses E."
In Rolo both were questions asked every frame in `update`
(`game.wasEmitted`, `input.wasPressed`), with a flag where something had
to happen only once. It works, but people from Roblox write
`event:Connect(function() ... end)` and `event:Wait()`, and since
ADR 0031 a script can wait for time but not for anything else.

## Decision

**A script can listen to events.** An event is a value; it has three
methods:

```lua
function start(self)
    game.event("gate_open"):connect(function()      -- every time it is sent
        self.object:play("open")
    end)
    input.pressed("e"):wait()                        -- start stops here until E goes down
    self.object:collided():once(function(other)      -- the first time only
        print("hit " .. other:name())
    end)
end
```

| | |
|---|---|
| `event:connect(f)` | begins `f` every time the event happens. Gives back a connection |
| `event:once(f)` | the same, the first time only |
| `event:wait()` | stops the task here until it happens; gives back what the event brings |
| `connection:disconnect()` | stops listening. `connection:isConnected()` asks |

The events of this first step:

| | happens when | brings |
|---|---|---|
| `game.event("name")` | that game event was sent (`game.emit`, a Trigger's emit action); heard in the next frame, as `game.wasEmitted` | nothing |
| `input.pressed("jump")`, `input.released("jump")` | an action, key or button goes down or up (named as for `input.wasPressed`) | nothing |
| `obj:collided()` | physics makes that object begin to touch another: what `collide(self, other)` is told, for any object | the other object |

- **A connected function is a task** (ADR 0031): it may wait.
- **A connection belongs to the object** whose script made it, as tasks
  do: it rests while the object is switched off (what happens meanwhile is
  missed, not kept), and is gone when the object is destroyed or the
  scene changes. Nothing has to be disconnected to tidy up.
- `wait` is for tasks: in `update` and `collide` it is an error that says
  to use `connect`.

### The names

The method names are Roblox's with a small first letter, like every name
in Rolo (`connect`, not `Connect`). Writing `:Connect` is an error that
says so, where "attempt to call a nil value" would leave a newcomer
guessing. (The Roblox names here are from memory, not checked against
Roblox's documentation.)

### Why events are made by functions

Roblox has them as fields (`part.Touched`). Rolo's objects have methods
only, and a script could not tell a missing field from a misspelt one;
`obj:collided()`, `game.event(name)` and `input.pressed(name)` check
their arguments like every other function.

## How it is built

- An event is a small value (`ScriptSignal`: what kind, and the name, key
  or object). A script's connections are kept with its Script component
  (`Script::m_connections`), beside its tasks.
- **Asking, not telling.** Once a frame, just before an object's
  `update`, the script host looks whether anything that object's script
  listens to has happened (`ScriptHost::runConnections`): the frame's
  game events, the frame's key presses, the last physics step's new
  touches. Then it begins the connected functions as tasks, and lets
  tasks that waited go on. So:
  - a script that listens to nothing pays one check that its list is
    empty;
  - connections rest and go with their object without any bookkeeping;
  - a function connected to a collision runs in the frame *after* the
    touch began (`collide(self, other)` still runs in the same frame);
  - an event that happens twice in a frame is heard once (collisions:
    once for each other object).
- The limits of tasks hold: a script may have 1000 connections (the next
  is an error that asks whether `connect` is called every frame), and a
  connected function is one of its script's at most 1000 tasks while it
  waits.
- An error in a connected function is reported with file and line,
  marked "in a function connected to an event", and switches that
  object's script off, as an error in `update` does.

## Cost

Stress test (debug build, Linux, three runs each, 2000 scripted objects
that listen to nothing): 2.80 to 2.97 ms a frame (median) before, 2.76 to
2.92 after. No difference beyond the noise. Scenes with many connections
were not measured.

## Tested

`tests/gameplay/test_scripts.cpp` (`[event]`): `connect`, `once`,
`disconnect` (twice too) and `wait` on a game event, with a connected
function that waits; keys, actions and the mouse, pressed and released,
and a `start` that waits for a key; `obj:collided()` of another object,
connected and waited for, with physics; connections of an object that is
switched off (rest) and destroyed (gone); `:Connect`, `wait` in `update`,
`connect` at the top of a file and every frame, bad arguments; an error
in a connected function. The script tests also pass under the address
sanitizer.

## Added 2026-11-18: three more events

| | happens when | brings |
|---|---|---|
| `obj:separated()` | physics makes that object stop touching another | the other object |
| `obj:animationFinished()` | its clip, one that does not loop, has played to its end | the clip's name |
| `obj:destroyed()` | that object is gone from the game | nothing |

Each is one more `ScriptSignal` kind looked at in
`ScriptHost::runConnections`, as the first three are:

- **Separated** reads the pairs whose touch ended in the last physics
  step (`PhysicsWorld::parts`, from Box2D's end-touch events). A touch
  that ends because one of the two was destroyed is not reported: Box2D
  can no longer say whose shape it was. `destroyed()` is for that.
- **AnimationFinished** has no list to read: "finished" is a state
  (`Game::isAnimationFinished`). The connection remembers whether it was
  finished when last looked at (`Connection::was`) and fires on becoming
  so. A clip that is finished stays finished until it is played again
  from its start (`obj:play(clip, true)`).
- **Destroyed** fires when the object is no longer in the scene, once;
  the connection is then dropped, having nothing more to hear. An object
  cannot hear its own destruction, because its connections go with it.

Tested (`[more]`): touching ending, connected and waited for, with
physics; an object destroyed, connected and waited for, heard once; a
clip finishing, not heard for a looping clip, heard again when played
again.

## Not done

- Events of one's own with values (`BindableEvent`): game events have a
  name only.
- More events: a variable changing, the scene changing, an overlap (not a
  physics touch) beginning or ending.
- `wait` with a time limit.
- Events kept for an object that was switched off.
