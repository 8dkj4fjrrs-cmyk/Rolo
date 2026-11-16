# ADR 0031: Waiting in scripts (tasks)

- **Status:** Accepted; built (2026-11-16)
- **Follows:** ADR 0013 (Lua scripting), ADR 0019 (direction: for people who come from Roblox), ADR 0020 (Luau)

## The need

"Wait two seconds, then open the door." In Rolo that was a number counted
down in `update`, with a flag to remember that it was counting. It works,
but it turns one thought into three places in the file, and it is the
habit people from Roblox have to drop first: there, a script runs from
top to bottom and writes `task.wait(2)`.

## Decision

**A script may wait**, with Roblox's own names:

```lua
function start(self)
    task.wait(2)                 -- two seconds of game time
    self.object:play("open")
    while true do                -- a loop for ever is fine, if it waits
        task.wait(0.5)
        blink(self)
    end
end
```

| | |
|---|---|
| `task.wait(seconds)` | stops here until that much game time has passed; with nothing, until the next frame. Gives back how long it waited |
| `task.spawn(f, ...)` | begins `f(...)` now as code of its own, which may wait; the caller goes on when it first waits or ends |
| `task.delay(seconds, f, ...)` | begins `f(...)` after that much game time |

- **`start(self)` is itself such code** (a "task"): it may wait.
- **`update` and `collide` may not**: they must finish within their
  frame. `task.wait` there is an error that says what to do instead
  (count the time, or begin a task with `task.spawn`).
- A task **belongs to the object** whose script began it. It goes on,
  when its time has come, just before that object's `update`. It does not
  run while the object is switched off, and is dropped when the object is
  destroyed or the scene changes.

### Why these names

Rolo's own lists are `game`, `input` and `audio`, and a function there
(`game.wait`) would have been in house style. But these three are the
ones a Roblox scripter types without thinking, they mean the same here,
and ADR 0019 says who Rolo is for. Nothing else of Roblox's `task` is
here (`task.defer`, `task.cancel`, threads as values).

### Why only `start` and tasks, not `update`

Waiting needs a coroutine: code with its own stack that can be put aside.
Making one for every `update` of every object every frame would be
garbage by the thousand, for the sake of code that should not wait
anyway. `start` runs once per object, so one coroutine each is cheap.

## How it is built

- Luau coroutines. `ScriptHost::start` puts `start(self)` on a new one
  and runs it; `task.spawn` and `task.delay` do the same for a function.
  A script's waiting tasks are kept with its Script component
  (`Script::m_tasks`: the coroutine, when it goes on, when it began to
  wait).
- `task.wait` notes the time and yields. `ScriptHost::update` first lets
  the object's due tasks go on (`runTasks`), then calls `update`.
  "Until the next frame" is exact: a task never goes on in the frame it
  began to wait in.
- The limits scripts already have hold for tasks: the time limit counts
  each stretch of running (so `while true do end` without a wait is still
  stopped after ten seconds), and memory is the scripts' one budget.
  A script may have 1000 tasks waiting; the next is an error (one begun
  every frame and never ending is the usual cause).
- An error in a task is reported like any script error, with the file and
  line, marked "in a task (code that waited)", and switches that object's
  script off, as an error in `update` does.
- `task.wait` in a coroutine the script made itself
  (`coroutine.create`) is refused: Rolo could not let it go on again.

## Cost

Measured with the stress test (debug build, Linux, three runs each):
2000 scripted objects cost 2.62 ms a frame before and 2.80 ms after
(about 6 % more: one check per object for waiting tasks, and noting whose
code runs). The bullets and busy scenes did not move beyond their noise.
Not measured in a release build.

## Tested

`tests/gameplay/test_scripts.cpp`: a `start` that waits half a second
and then three frames while `update` goes on; `task.spawn` with a loop
for ever, `task.delay` with arguments, two objects with their own
timings, a destroyed object whose tasks never run; `task.wait` in
`update` and in a script's own coroutine; an error after a wait (line
named, script switched off); `task.spawn` at the top of a file; the
limit of 1000.

## Not done

- `task.cancel`, `task.defer`, and stopping a task from outside.
- Events to wait for (`event:Wait()`): only time.
- Tasks in saved games: after loading, `start` runs again from its top.
