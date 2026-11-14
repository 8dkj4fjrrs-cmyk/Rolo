#!/usr/bin/env python3
"""Makes the stress-test project (examples/stress/project): scenes that load
Rolo the way busy games do, to measure frame times before and after changes
to scripting (ADR 0019).

    python3 examples/stress/make_stress.py examples/stress/project

Every scene is the same each time (no randomness), so runs can be compared.
The scripts were written to run unchanged on Lua 5.4 and on Luau (all but
vectors.lua, which uses Luau's vectors).

Scenes:
  scripts   2000 objects, each with its own script moving it every frame
  garbage   1000 objects whose scripts make new tables and strings every frame
  tidy      the same as garbage, with the same maths, but making no garbage
  vectors   the same again, written with Luau vectors: points as values,
            like garbage's tables, but no garbage
  bullets   one spawner firing 1200 bullets a second from a pool of 2000,
            each bullet with its own script
  physics   800 bodies (circles and boxes) falling into a box and piling up
  sprites   5000 still sprites and no scripts (drawing, and the engine's
            cost per object)
  busy      a bit of everything at once: 800 movers, 400 garbage makers,
            bullets from a pool of 1000, and 300 bodies
"""

import json
import math
import sys
from pathlib import Path

from PIL import Image

VIEW_W, VIEW_H = 1280, 720
CX, CY = VIEW_W / 2, VIEW_H / 2

_NAMES = ["dot.png", "orbit.lua", "garbage.lua", "spawner.lua", "bullet.lua",
          "scripts.lscene", "garbage.lscene", "bullets.lscene", "physics.lscene", "sprites.lscene",
          "busy.lscene", "tidy.lua", "tidy.lscene", "vectors.lua", "vectors.lscene"]
IDS = {n: f"53545253{i + 1:08x}" for i, n in enumerate(_NAMES)}   # "STRS..."

# --- Scripts (Lua 5.4 and Luau alike) ---------------------------------------------------------------------

ORBIT = """-- orbit.lua: circles the point where the object starts, turning as it goes.
-- The kind of small script a game has hundreds of.

local RADIUS = 40

function start(self)
    self.x, self.y = self.object:position()
    self.phase = (self.x * 0.013 + self.y * 0.007) % 6.283
    self.speed = 1 + (self.x % 7) * 0.2
end

function update(self, dt)
    self.phase = self.phase + self.speed * dt
    self.object:setPosition(self.x + math.cos(self.phase) * RADIUS, self.y + math.sin(self.phase) * RADIUS)
    self.object:setRotation(self.phase * 57.3)
end
"""

GARBAGE = """-- garbage.lua: moves like orbit.lua, but the way a lot of real code is
-- written: a new table for every point and a new string every frame. All of
-- it is garbage a moment later, which the collector must clear up.

local function offset(t)
    return {x = math.cos(t) * 30, y = math.sin(t) * 30}
end

function start(self)
    self.x, self.y = self.object:position()
    self.t = (self.x + self.y) * 0.01
end

function update(self, dt)
    self.t = self.t + dt
    local trail = {}
    for i = 1, 8 do
        trail[i] = offset(self.t - i * 0.1)
    end
    self.label = "at " .. math.floor(trail[1].x) .. ", " .. math.floor(trail[1].y)
    self.object:setPosition(self.x + trail[1].x, self.y + trail[1].y)
end
"""

TIDY = """-- tidy.lua: does the same maths as garbage.lua and moves the same way,
-- but keeps numbers in local variables instead of making new tables and
-- strings, so it leaves no garbage. Compare the two scenes.

function start(self)
    self.x, self.y = self.object:position()
    self.t = (self.x + self.y) * 0.01
end

function update(self, dt)
    self.t = self.t + dt
    local firstX, firstY = 0, 0
    for i = 1, 8 do
        local t = self.t - i * 0.1
        local x, y = math.cos(t) * 30, math.sin(t) * 30
        if i == 1 then firstX, firstY = x, y end
    end
    self.labelX, self.labelY = math.floor(firstX), math.floor(firstY)
    self.object:setPosition(self.x + firstX, self.y + firstY)
end
"""

VECTORS = """-- vectors.lua: the same maths as garbage.lua, with each point a Luau vector
-- instead of a table. A vector is a value, like a number: making one leaves
-- no garbage, and vectors add and multiply (home + offset). Rolo's functions
-- take a vector wherever they take x, y.

local function offset(t)
    return vector.create(math.cos(t) * 30, math.sin(t) * 30, 0)
end

function start(self)
    local x, y = self.object:position()
    self.home = vector.create(x, y, 0)
    self.t = (x + y) * 0.01
end

function update(self, dt)
    self.t = self.t + dt
    local first = offset(self.t - 0.1)
    for i = 2, 8 do
        local point = offset(self.t - i * 0.1)
    end
    self.object:setPosition(self.home + first)
end
"""

SPAWNER = """-- spawner.lua: fires bullets from a pool of objects made in the editor
-- ("Bullet 1", "Bullet 2", ...): a resting bullet is put here and switched
-- on; bullet.lua flies it and switches it off again.

local PER_SECOND = 1200

function start(self)
    self.x, self.y = self.object:position()
    self.pool = {}
    local i = 1
    while true do
        local bullet = game.find("Bullet " .. i)
        if bullet == nil then break end
        self.pool[#self.pool + 1] = bullet
        i = i + 1
    end
    self.next = 1
    self.owed = 0
end

function update(self, dt)
    self.owed = self.owed + PER_SECOND * dt
    while self.owed >= 1 do
        self.owed = self.owed - 1
        local bullet = self.pool[self.next]
        self.next = self.next % #self.pool + 1
        if not bullet:isActive() then
            bullet:setPosition(self.x, self.y)
            bullet:setActive(true)
        end
    end
end
"""

BULLET = """-- bullet.lua: flies straight out from where the spawner put it, and rests
-- (switched off) once it is off the screen.

local SPEED = 500
local fired = 0   -- shared by every bullet (one file, one set of globals)

local function aim(self)
    fired = fired + 1
    local angle = fired * 0.37
    self.vx, self.vy = math.cos(angle) * SPEED, math.sin(angle) * SPEED
    self.fromX, self.fromY = self.object:position()
end

function update(self, dt)
    if not self.flying then   -- just fired (or fired again)
        aim(self)
        self.flying = true
    end
    local x, y = self.object:position()
    x, y = x + self.vx * dt, y + self.vy * dt
    self.object:setPosition(x, y)
    local dx, dy = x - self.fromX, y - self.fromY
    if dx * dx + dy * dy > 700 * 700 then
        self.flying = false
        self.object:setActive(false)
    end
end
"""

# --- Project files -----------------------------------------------------------------------------------------


def write(path, data, compact=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    if compact:   # big scenes: one object per line, to keep the files small
        if "entities" in data:
            head = {k: v for k, v in data.items() if k != "entities"}
            lines = ",\n".join(json.dumps(e, separators=(",", ":")) for e in data["entities"])
            text = json.dumps(head)[:-1] + ', "entities": [\n' + lines + "\n]}"
            path.write_text(text + "\n")
            return
    path.write_text(json.dumps(data, indent=2) + "\n")


def meta(name, kind, extra=None):
    m = {"format": "rolo.asset", "version": 1, "id": IDS[name], "type": kind}
    m.update(extra or {})
    return m


def transform(x=0.0, y=0.0, rotation=0.0, scale=(1, 1)):
    return {"type": "rolo.Transform", "position": [round(x, 3), round(y, 3)], "rotation": rotation,
            "scale": list(scale)}


def sprite(size, colour):
    return {"type": "rolo.Sprite", "image": IDS["dot.png"], "size": list(size), "color": colour,
            "flipX": False, "flipY": False}


def script(name):
    return {"type": "rolo.Script", "script": IDS[name]}


def collider(size, shape="box"):
    return {"type": "rolo.Collider", "size": list(size), "offset": [0, 0], "solid": True, "shape": shape}


def body():
    return {"type": "rolo.RigidBody", "bodyType": "dynamic", "mass": 1, "gravityScale": 1, "friction": 0.4,
            "bounciness": 0.1, "drag": 0, "lockRotation": False, "fast": False}


class Scene:
    def __init__(self, name, prefix):
        self.name, self.prefix, self.n, self.entities = name, prefix, 0, []
        self.add("Camera", [transform(CX, CY), {"type": "rolo.Camera", "zoom": 1}])

    def add(self, name, components, active=True):
        self.n += 1
        self.entities.append({"id": f"{self.prefix}{self.n:010x}", "name": name, "active": active,
                              "components": components})

    def data(self):
        return {"format": "rolo.scene", "version": 1, "name": self.name, "entities": self.entities}


def colour(i):
    """A cheerful colour that changes along the list."""
    r = int(150 + 100 * math.sin(i * 0.11))
    g = int(150 + 100 * math.sin(i * 0.17 + 2))
    b = int(150 + 100 * math.sin(i * 0.23 + 4))
    return f"#{r:02x}{g:02x}{b:02x}ff"


def grid(count, x0=40, y0=40, x1=VIEW_W - 40, y1=VIEW_H - 40):
    """`count` points spread evenly over the rectangle."""
    columns = max(1, round(math.sqrt(count * (x1 - x0) / (y1 - y0))))
    rows = math.ceil(count / columns)
    for i in range(count):
        c, r = i % columns, i // columns
        yield (x0 + (c + 0.5) * (x1 - x0) / columns, y0 + (r + 0.5) * (y1 - y0) / rows)


def add_movers(s, count, file, label, size=10):
    for i, (x, y) in enumerate(grid(count)):
        s.add(f"{label} {i + 1}", [transform(x, y), sprite((size, size), colour(i)), script(file)])


def add_bullets(s, pool):
    s.add("Spawner", [transform(CX, CY), sprite((24, 24), "#ffffffff"), script("spawner.lua")])
    for i in range(pool):
        s.add(f"Bullet {i + 1}", [transform(CX, CY), sprite((6, 6), "#ffd84aff"), script("bullet.lua")],
              active=False)


def add_physics(s, count):
    # A box to fall into: a floor and two walls (colliders without bodies stay still).
    s.add("Floor", [transform(CX, 10), sprite((VIEW_W, 20), "#40485aff"), collider((VIEW_W, 20))])
    s.add("Left Wall", [transform(10, CY), sprite((20, VIEW_H), "#40485aff"), collider((20, VIEW_H))])
    s.add("Right Wall", [transform(VIEW_W - 10, CY), sprite((20, VIEW_H), "#40485aff"), collider((20, VIEW_H))])
    for i, (x, y) in enumerate(grid(count, 60, 200, VIEW_W - 60, VIEW_H * 2)):
        x += (i % 3) * 2   # not quite stacked, so they tumble
        if i % 2 == 0:
            s.add(f"Ball {i + 1}", [transform(x, y), sprite((14, 14), colour(i)), collider((14, 14), "circle"),
                                    body()])
        else:
            s.add(f"Crate {i + 1}", [transform(x, y), sprite((14, 14), colour(i)), collider((14, 14)), body()])


def scenes():
    out = {}
    s = Scene("Scripts", "a10000")
    add_movers(s, 2000, "orbit.lua", "Mover")
    out["scripts.lscene"] = s
    s = Scene("Garbage", "a20000")
    add_movers(s, 1000, "garbage.lua", "Litterer")
    out["garbage.lscene"] = s
    s = Scene("Tidy", "a70000")
    add_movers(s, 1000, "tidy.lua", "Tidy")
    out["tidy.lscene"] = s
    s = Scene("Vectors", "a80000")
    add_movers(s, 1000, "vectors.lua", "Vector")
    out["vectors.lscene"] = s
    s = Scene("Bullets", "a30000")
    add_bullets(s, 2000)
    out["bullets.lscene"] = s
    s = Scene("Physics", "a40000")
    add_physics(s, 800)
    out["physics.lscene"] = s
    s = Scene("Sprites", "a50000")
    for i, (x, y) in enumerate(grid(5000, 10, 10, VIEW_W - 10, VIEW_H - 10)):
        s.add(f"Dot {i + 1}", [transform(x, y), sprite((8, 8), colour(i))])
    out["sprites.lscene"] = s
    s = Scene("Busy", "a60000")
    add_movers(s, 800, "orbit.lua", "Mover")
    add_movers(s, 400, "garbage.lua", "Litterer", size=6)
    add_physics(s, 300)
    add_bullets(s, 1000)
    out["busy.lscene"] = s
    return out


def main():
    root = Path(sys.argv[1])
    root.mkdir(parents=True, exist_ok=True)
    write(root / "project.rolo", {"format": "rolo.project", "version": 1, "name": "Rolo Stress Test",
                                  "startScene": "scenes/busy.lscene"})
    art = root / "assets"
    art.mkdir(parents=True, exist_ok=True)
    Image.new("RGBA", (4, 4), (255, 255, 255, 255)).save(art / "dot.png")
    write(art / "dot.png.meta", meta("dot.png", "image", {"filter": "pixel"}))
    for name, code in (("orbit.lua", ORBIT), ("garbage.lua", GARBAGE), ("tidy.lua", TIDY), ("vectors.lua", VECTORS),
                       ("spawner.lua", SPAWNER), ("bullet.lua", BULLET)):
        (root / "scripts").mkdir(exist_ok=True)
        (root / "scripts" / name).write_text(code, encoding="utf-8")
        write(root / "scripts" / (name + ".meta"), meta(name, "script"))
    for name, scene in scenes().items():
        write(root / "scenes" / name, scene.data(), compact=True)
        write(root / "scenes" / (name + ".meta"), meta(name, "scene"))


if __name__ == "__main__":
    main()
