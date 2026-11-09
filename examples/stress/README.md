# The stress test

Scenes that load Rolo the way busy games do, to measure frame times before
and after changes to scripting and the engine ([ADR 0019](../../docs/adr/0019-direction.md)).
Nothing here is kept unless the numbers say it helps.

| Scene | What happens |
|---|---|
| `scripts` | 2000 objects, each with its own script (`orbit.lua`) moving and turning it every frame |
| `garbage` | 1000 objects whose script (`garbage.lua`) makes new tables and a new string every frame, as a lot of real code does |
| `tidy` | the same 1000 objects and the same maths (`tidy.lua`), making no garbage: compare it with `garbage` |
| `bullets` | one spawner firing 1200 bullets a second from a pool of 2000, each bullet with its own script |
| `physics` | 800 bodies, circles and boxes, falling into a box and piling up |
| `sprites` | 5000 still sprites and no scripts: drawing, and what the engine costs per object |
| `busy` | a bit of everything at once (the start scene) |

The project is made by `make_stress.py` (`python3 examples/stress/make_stress.py
examples/stress/project`); every run of it gives the same scenes. The
scripts run unchanged on Lua 5.4 and on Luau, so the two can be compared
(Rolo uses Luau since ADR 0020).

## Measuring gameplay: `rolo_stress`

`rolo_stress` plays each scene without a window, with fixed 1/60 s steps,
and reports how long gameplay took per frame. **Build it optimised**:
times from a Debug build mean little.

```sh
cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DROLO_RUNTIME_BACKEND=none \
      -DROLO_BUILD_EDITOR=OFF -DROLO_BUILD_TESTS=OFF -DROLO_BUILD_SAMPLE_GAME=OFF
cmake --build build/bench --target rolo_stress
./build/bench/bin/rolo_stress                    # every scene, 600 frames each
./build/bench/bin/rolo_stress --scene garbage --frames 1200 --csv garbage.csv
```

| Column | Meaning |
|---|---|
| mean, median | a typical frame's gameplay time, in milliseconds |
| p99, worst | the slow frames: 1 in 100 is slower than p99. **Stutters show here**: a worst far above the median |
| >8ms, >16ms | frames over half a 60 fps frame, and over a whole one |
| scripts, physics, other | where the time went (means): behaviours and Lua scripts; physics; the rest (dialogue, triggers, texts) |
| Lua MB | the most memory the scripts held at the end of a frame |
| KB/frame | memory the scripts asked for each frame: nearly all of it garbage, which the collector must clear |

A frame at 60 fps has 16.7 ms for everything, drawing included, so
gameplay should stay well below that.

## Measuring everything: the player

To include drawing, play a scene in the player with `--frame-times`; when
the window closes it reports frame lengths and the gameplay part:

```sh
./build/release/bin/rolo_player/rolo_player --project examples/stress/project --frame-times
./build/release/bin/rolo_player/rolo_player --scene examples/stress/project/scenes/garbage.lscene --frame-times
```

(macOS: the program is inside `rolo_player.app/Contents/MacOS/`.) Frame
lengths there include waiting for the screen, so a fast computer shows
16.7 ms (60 fps) when it has time to spare.

## Baseline: Lua 5.4.9, before any optimisation

(Rolo moved to Luau the same day; see below.)

Measured on 2026-11-09 with `rolo_stress --frames 1200` (after 120 warm-up
frames), Release build, GCC 13.3, on a shared cloud Linux machine (2 cores
of an Intel Xeon at 2.1 GHz). Each number is the median of three runs.
Runs on that machine vary: means by about 10%, worst frames by up to 3×,
since other work shares the computer. Compare like with like: measure
before and after a change on the same computer.

| scene | objects | mean | median | p99 | worst | >8ms | >16ms | scripts | physics | other | Lua MB | KB/frame |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| scripts | 2001 | 2.21 | 2.19 | 3.45 | 4.42 | 0 | 0 | 1.25 | 0.16 | 0.80 | 0.6 | 0 |
| garbage | 1001 | 4.47 | 4.48 | 7.48 | 9.38 | 2 | 0 | 3.94 | 0.09 | 0.44 | 0.6 | 997 |
| tidy | 1001 | 2.06 | 1.90 | 3.42 | 5.08 | 0 | 0 | 1.58 | 0.08 | 0.39 | 0.3 | 0 |
| bullets | 2002 | 1.93 | 1.73 | 3.62 | 6.75 | 0 | 0 | 0.94 | 0.17 | 0.83 | 0.7 | 0 |
| physics | 804 | 1.47 | 1.33 | 2.18 | 3.88 | 0 | 0 | 0.07 | 1.03 | 0.38 | 0.0 | 0 |
| sprites | 5001 | 1.96 | 1.91 | 3.65 | 5.23 | 0 | 0 | 0.27 | 0.29 | 1.40 | 0.0 | 0 |
| busy | 2505 | 4.89 | 4.60 | 10.56 | 13.87 | 28 | 0 | 2.72 | 1.12 | 1.05 | 1.5 | 398 |

What it shows:

- **Garbage is the costliest thing a script does.** `garbage` and `tidy`
  do the same maths for the same 1000 objects; the only difference is
  that `garbage` makes about 1 MB of new tables and strings a frame. Its
  scripts take about 2.5× as long (3.9 ms against 1.6), and its slow
  frames are about twice its typical ones: the stutter pattern. The
  `tidy` row was measured later the same day (median of three runs).
  Garbage-free vectors and APIs (ADR 0019) aim straight at this.
- **Objects cost time even when they do nothing.** `sprites` has no
  scripts and no physics, yet takes about 1.9 ms a frame for 5000
  objects: the engine looks at every object several times a frame
  (behaviours, physics, texts). This is engine-side work, independent of
  the language.
- **The engine's own part ("other") grows with every object**, about
  0.8 ms for 2000. Together with the point above, this is the first
  target alongside Luau.
- **Physics is fine at this size**: 800 tumbling bodies take about 1 ms.
- **Busy scenes add up**: `busy` reaches 10–14 ms in its worst frames
  for gameplay alone, before drawing.

## Luau against Lua 5.4

Measured on 2026-11-09 on the same machine, Lua 5.4.9 (Rolo at `00f0d14`)
and Luau 0.744 runs taking turns, three of each, `--frames 1200`; medians.
Only the columns that matter here (milliseconds per frame):

| scene | scripts, Lua 5.4 | scripts, Luau | | frame mean, Lua 5.4 | frame mean, Luau | p99, Lua 5.4 | p99, Luau |
|---|---|---|---|---|---|---|---|
| scripts | 1.48 | 1.27 | 1.2× | 2.63 | 2.60 | 4.49 | 3.93 |
| garbage | 4.09 | 1.74 | 2.4× | 4.63 | 2.44 | 8.46 | 4.22 |
| tidy | 1.76 | 0.76 | 2.3× | 2.28 | 1.32 | 3.81 | 3.37 |
| bullets | 0.96 | 0.91 | 1.1× | 1.98 | 2.10 | 3.85 | 4.64 |
| busy | 2.92 | 1.63 | 1.8× | 5.22 | 4.33 | 9.78 | 7.63 |

What it shows:

- **Code that runs in the language itself is about 2.3× faster** with
  Luau (`garbage`, `tidy`), and `garbage`'s slow frames (p99) are half as
  slow: Luau's garbage collector clears the same garbage with shorter
  pauses.
- **Scripts that mostly call Rolo hardly change** (`scripts`, `bullets`):
  there the time goes into Rolo's functions (`setPosition`,
  `setRotation`, `isActive`), not the language. That is the target of the
  next step, the performance layer.
- **Scenes without scripts** (`physics`, `sprites`) do not use Luau at
  all; their differences between the two runs (up to 30%) are the noise of
  the machine. Worst frames vary even more, so they are left out above.
- `KB/frame` counts differently now: Luau reports every object it makes,
  where Lua 5.4 counted blocks from the system. Garbage is about the same
  (1048 against 997 KB a frame in `garbage`).
- Moving to Luau first made garbage of its own: every script call made a
  48-byte object (Rolo pushed its error handler anew each call, which is
  free in Lua 5.4 but not in Luau). The stress test showed it (94 KB a
  frame in `scripts`); it is fixed, and scripts that make no garbage now
  show 0.

Drawing was not measured here: the cloud machine draws without a graphics
card, which says nothing about a real computer. Measure it on yours with
the player and `--frame-times`.

Not yet possible in the stress test: **making and removing objects while
playing**. Scripts cannot create objects yet (no `game.spawn` or clone),
so `bullets` reuses a pool. Spawning is on the list for the scripting work.
