"""Builds the Platformer Starter template (templates/platformer/project).

    rolo_editor --new-project templates/platformer/project --frames 1
    python3 templates/tools/make_platformer.py templates/platformer/project

A side-view level that shows Rolo's physics: a hero who runs and jumps
(scripts/player.lua: a Rigid Body moved by setting its velocity), crates to
push and stack, bouncy balls, a lift (a kinematic body, scripts/lift.lua),
coins to collect (Triggers, no code) and a flag at the end. Solid tiles are
the ground and walls. The art and sounds are drawn and synthesised here:
original, MIT like the rest of Rolo. The hero is The Lantern Keeper's.
Fixed IDs keep the output the same every run.

Needs Python 3 with Pillow and NumPy.
"""
import json
import math
import shutil
import sys
import wave
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
HERO = ROOT / "examples" / "lantern_keeper" / "project" / "assets" / "art" / "hero_walk.png"

_NAMES = ["tiles.png", "hero_walk.png", "crate.png", "ball.png", "coin.png", "flag.png", "tiles.ltileset",
          "hero.lanim", "coin.lanim", "level.lscene", "platformer.lvars", "player.lua", "lift.lua",
          "jump.wav", "coin.wav", "win.wav", "controls.lua", "pause.lua"]
IDS = {n: f"504c4154{i + 1:08x}" for i, n in enumerate(_NAMES)}     # "PLAT..."
PIXEL, SCALE = 16, 3
CELL = PIXEL * SCALE      # 48 points
VIEW = (1280, 720)

# Tiles, in the order drawn into tiles.png (see draw_tiles).
TILE = {"G": 0, "D": 1, "S": 2, "P": 3, "B": 4, "b": 5, "f": 6}
SOLID = [0, 1, 2, 3, 4]

# The level: 70 columns by 16 rows, row 0 at the top. The hero jumps about
# 4 tiles high and 7 across, so every ledge is within one jump of the last.
# Tiles: G grass, D dirt, S stone, P planks, B bricks, b bush, f flowers
# (bush and flowers are decoration). Things: 1 the start, C coins, X crates,
# O balls, L the lift, F the flag.
W, H = 70, 16
GROUND = 13                                   # the top row of the ground
PITS = [(23, 25), (45, 48)]                   # columns with no ground
FEATURES = [
    ("S", 19, 21, 10), ("C", 20, 20, 9),          # a stone step...
    ("P", 25, 31, 8), ("C", 26, 26, 7), ("C", 28, 28, 7), ("C", 30, 30, 7),   # ...up to planks over the pit
    ("S", 38, 40, 10), ("C", 39, 39, 9),
    ("P", 52, 56, 7), ("C", 54, 54, 6),           # high planks, reached from the lift
    ("B", 68, 69, 9), ("B", 68, 69, 10), ("B", 68, 69, 11), ("B", 68, 69, 12),   # the wall at the end
    ("b", 5, 5, 12), ("b", 34, 34, 12), ("f", 14, 14, 12), ("f", 60, 60, 12),
    ("1", 2, 2, 12),
    ("X", 8, 8, 11), ("X", 8, 8, 12), ("X", 11, 11, 12), ("X", 33, 33, 12), ("X", 64, 64, 12),
    ("O", 16, 16, 12), ("O", 36, 36, 12),
    ("L", 50, 50, 12),
    ("C", 43, 43, 12), ("C", 59, 59, 12),
    ("F", 66, 66, 12),
]


def make_level():
    rows = [[" "] * W for _ in range(H)]
    for c in range(W):
        if any(lo <= c <= hi for lo, hi in PITS):
            continue
        rows[GROUND][c] = "G"
        for r in range(GROUND + 1, H):
            rows[r][c] = "D"
    for ch, c0, c1, r in FEATURES:
        for c in range(c0, c1 + 1):
            rows[r][c] = ch
    return ["".join(r) for r in rows]


LEVEL = make_level()

PLAYER = """-- player.lua: the hero runs and jumps. Physics does the rest.
--
-- The Player has a Rigid Body (physics moves it, gravity pulls it down) and
-- a capsule Collider (it slides over the edges of tiles). This script only
-- says how fast it should go: sideways from the arrow keys, and up when
-- you jump. Try other numbers, save, and press Play again.

local SPEED = 280     -- running speed, points per second
local JUMP = 640      -- jump speed, points per second upwards

function start(self)
    -- "jump" is an action: the keys and buttons that do it. Players can
    -- change them in the Controls screen (scripts/controls.lua).
    input.action("jump", {"space", "w", "up", "pad a"})
    self.startX, self.startY = self.object:position()
    self.facing = "right"
    self.fallSpeed = 0
end

function update(self, dt)
    local vx, vy = self.object:velocity()
    local onGround = self.object:isOnGround()

    -- Landing after a long fall: a little rumble on a gamepad.
    if onGround and self.fallSpeed < -700 then
        input.rumble(0.4, 0.15)
    end
    self.fallSpeed = vy

    -- While a menu is open (pause or controls), the hero stands still.
    if game.get("menu") ~= "" then
        self.object:setVelocity(0, vy)
        self.object:play("idle_" .. self.facing)
        return
    end

    -- Run: set the sideways speed and keep the up-and-down speed (falling).
    local dx = input.direction()          -- -1 left to 1 right (keys, d-pad or stick)
    self.object:setVelocity(dx * SPEED, vy)

    -- Jump: only from the ground.
    if onGround and input.wasPressed("jump") then
        self.object:setVelocity(dx * SPEED, JUMP)
        audio.play("jump")
    end

    -- Face where it runs, and walk or stand.
    if dx < 0 then self.facing = "left" elseif dx > 0 then self.facing = "right" end
    if dx ~= 0 and onGround then
        self.object:play("walk_" .. self.facing)
    else
        self.object:play("idle_" .. self.facing)
    end

    -- Fell into a pit: back to the start.
    local x, y = self.object:position()
    if y < -200 then
        self.object:setPosition(self.startX, self.startY)
        self.object:setVelocity(0, 0)
    end
end
"""

CONTROLS = """-- controls.lua: a Controls screen where players change their keys and buttons.
--
-- Tab (or Back on a gamepad) opens and closes it; so does Controls in the
-- pause menu (scripts/pause.lua). Up and down choose an action, Enter (or A)
-- changes it: the next key or gamepad button pressed becomes its key or
-- button. R (or Y) puts everything back as it was. Players' choices are kept
-- on their computer, beside their saved games.
-- Copy this script into your own game and list your own actions below.
--
-- Which menu is open is the text variable "menu": "" (none), "pause" or
-- "controls" (content/platformer.lvars).

local ACTIONS = {
    {"jump", "Jump"},
    {"move left", "Run left"},
    {"move right", "Run right"},
    {"interact", "Interact"},
}

function start(self)
    input.action("controls", {"tab", "pad back"})
    self.panel = game.find("Controls Panel")
    self.words = game.find("Controls Text")
    self.choice = 1
    game.set("menu", "")
end

-- The pictures of an action's keys and buttons: "{button:space} {button:pad a}".
local function pictures(action)
    local out = {}
    for _, name in ipairs(input.bindings(action)) do
        out[#out + 1] = "{button:" .. name .. "}"
    end
    return table.concat(out, " ")
end

local function show(self)
    local lines = {"CONTROLS", ""}
    for i, entry in ipairs(ACTIONS) do
        local marker = (i == self.choice) and "> " or "   "
        local keys = pictures(entry[1])
        if input.isRebinding() and i == self.choice then
            keys = "press a key or button...  (Backspace: keep)"
        end
        lines[#lines + 1] = marker .. entry[2] .. "    " .. keys
    end
    lines[#lines + 1] = ""
    lines[#lines + 1] = "{button:move up} {button:move down} choose   {button:enter} / {button:pad a} change   "
                        .. "{button:r} / {button:pad y} reset   {button:controls} close"
    self.words:setText(table.concat(lines, "\\n"))
end

function update(self, dt)
    local menu = game.get("menu")
    if input.wasPressed("controls") and not input.isRebinding() then
        if menu == "" then menu = "controls" elseif menu == "controls" then menu = "" end
        game.set("menu", menu)
    end
    local open = menu == "controls"
    self.panel:setActive(open)
    if not open then
        return
    end
    if not input.isRebinding() then
        if input.wasPressed("move down") then self.choice = self.choice % #ACTIONS + 1 end
        if input.wasPressed("move up") then self.choice = (self.choice - 2) % #ACTIONS + 1 end
        if input.wasPressed("enter") or input.wasPressed("pad a") then
            input.rebind(ACTIONS[self.choice][1])
        elseif input.wasPressed("r") or input.wasPressed("pad y") then
            input.resetControls()
        end
    end
    show(self)
end
"""

PAUSE = """-- pause.lua: a pause menu. Escape (or Start on a gamepad) opens and closes it.
--
-- Rolo games quit when Escape is pressed, unless the project turns that off.
-- This one does (the scene's Inspector, Game section: Escape Quits), so
-- Escape is free for this menu, and its Quit closes the game with
-- game.quit(). In the editor, Quit stops playing.
--
-- Up and down choose, Enter (or A) picks. The hero waits while it is open
-- (scripts/player.lua checks the "menu" variable).

local ITEMS = {"Resume", "Controls", "Quit"}

function start(self)
    input.action("pause", {"escape", "pad start"})
    self.panel = game.find("Pause Panel")
    self.words = game.find("Pause Text")
    self.choice = 1
end

local function show(self)
    local lines = {"PAUSED", ""}
    for i, item in ipairs(ITEMS) do
        lines[#lines + 1] = ((i == self.choice) and "> " or "   ") .. item
    end
    lines[#lines + 1] = ""
    lines[#lines + 1] = "{button:move up} {button:move down} choose   {button:enter} / {button:pad a} pick   "
                        .. "{button:pause} resume"
    self.words:setText(table.concat(lines, "\\n"))
end

function update(self, dt)
    local menu = game.get("menu")
    -- (The Escape that cancels changing a control is not a press, so it does not pause.)
    if input.wasPressed("pause") then
        if menu == "" then
            menu = "pause"
            self.choice = 1
        elseif menu == "pause" then
            menu = ""        -- resume
        elseif menu == "controls" then
            menu = "pause"   -- back from the Controls screen
        end
        game.set("menu", menu)
    end
    local open = menu == "pause"
    self.panel:setActive(open)
    if not open then
        return
    end
    if input.wasPressed("move down") then self.choice = self.choice % #ITEMS + 1 end
    if input.wasPressed("move up") then self.choice = (self.choice - 2) % #ITEMS + 1 end
    if input.wasPressed("enter") or input.wasPressed("pad a") then
        local item = ITEMS[self.choice]
        if item == "Resume" then
            game.set("menu", "")
        elseif item == "Controls" then
            game.set("menu", "controls")
        else
            game.quit()
        end
    end
    show(self)
end
"""

LIFT = """-- lift.lua: moves the lift up and down between two heights.
--
-- The lift has a kinematic Rigid Body: gravity does not pull it, nothing
-- pushes it, and it moves at the velocity a script gives it, carrying
-- whatever stands on it.

local SPEED = 90       -- points per second
local RISE = 200       -- how far up it goes

function start(self)
    local x, y = self.object:position()
    self.bottom = y
    self.direction = 1
end

function update(self, dt)
    local x, y = self.object:position()
    if y > self.bottom + RISE then self.direction = -1 end
    if y < self.bottom then self.direction = 1 end
    self.object:setVelocity(0, SPEED * self.direction)
end
"""

# --- Art --------------------------------------------------------------------------------------------------


def rgb(hex_colour, a=255):
    h = hex_colour.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def speckle(d, x0, colours, seed, count=10):
    rnd = np.random.default_rng(seed)
    for _ in range(count):
        x, y = rnd.integers(0, 16, size=2)
        d.point((x0 + int(x), int(y)), fill=colours[int(rnd.integers(0, len(colours)))])


def draw_tiles(path):
    img = Image.new("RGBA", (16 * 8, 16), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    dirt, dirt_dark, dirt_light = rgb("8a5a36"), rgb("6e4529"), rgb("a06c43")
    # 0 grass on dirt
    d.rectangle([0, 0, 15, 15], fill=dirt)
    speckle(d, 0, [dirt_dark, dirt_light], 1, 12)
    d.rectangle([0, 0, 15, 4], fill=rgb("5cb848"))
    for x in range(0, 16, 3):
        d.point((x, 5), fill=rgb("4a9a3a"))
    d.line([0, 0, 15, 0], fill=rgb("8ee070"))
    # 1 dirt
    d.rectangle([16, 0, 31, 15], fill=dirt)
    speckle(d, 16, [dirt_dark, dirt_light], 2, 14)
    # 2 stone
    d.rectangle([32, 0, 47, 15], fill=rgb("7d8794"))
    d.rectangle([32, 0, 47, 0], fill=rgb("a3adb8"))
    d.rectangle([32, 15, 47, 15], fill=rgb("5a626c"))
    d.line([32, 7, 47, 7], fill=rgb("5a626c"))
    d.line([39, 0, 39, 7], fill=rgb("5a626c"))
    d.line([43, 8, 43, 15], fill=rgb("5a626c"))
    # 3 planks
    d.rectangle([48, 0, 63, 15], fill=rgb("b7843f"))
    for y in (5, 10):
        d.line([48, y, 63, y], fill=rgb("8c6230"))
    d.line([48, 0, 63, 0], fill=rgb("d8a560"))
    d.point((51, 2), fill=rgb("6a4723"))
    d.point((60, 8), fill=rgb("6a4723"))
    # 4 bricks
    d.rectangle([64, 0, 79, 15], fill=rgb("b2523e"))
    for y in (3, 7, 11, 15):
        d.line([64, y, 79, y], fill=rgb("7e3527"))
    for row, y in enumerate((0, 4, 8, 12)):
        for x in range(64 + (row % 2) * 4, 80, 8):
            d.line([x, y, x, y + 2], fill=rgb("7e3527"))
    # 5 bush
    for (cx, cy, r) in [(84, 11, 5), (90, 9, 6), (86, 7, 4)]:
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=rgb("3f8f3a"))
    d.ellipse([88, 5, 91, 8], fill=rgb("63b85a"))
    # 6 flowers
    for (x, c) in [(99, "ffd84a"), (103, "ff7aa8"), (107, "ffffff"), (110, "ffd84a")]:
        d.line([x, 15, x, 10], fill=rgb("4a9a3a"))
        d.point((x, 9), fill=rgb(c))
        d.point((x - 1, 10), fill=rgb(c))
        d.point((x + 1, 10), fill=rgb(c))
    img.save(path)


def draw_crate(path):
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 15, 15], fill=rgb("a8743a"), outline=rgb("5e3b18"))
    d.rectangle([2, 2, 13, 13], outline=rgb("7c5226"))
    d.line([2, 2, 13, 13], fill=rgb("7c5226"), width=2)
    d.line([1, 1, 14, 1], fill=rgb("c99555"))
    img.save(path)


def draw_ball(path):
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([0, 0, 15, 15], fill=rgb("f08a24"), outline=rgb("8a4310"))
    d.arc([-6, 3, 21, 22], 220, 320, fill=rgb("8a4310"))   # a seam curving round it
    d.ellipse([3, 3, 6, 6], fill=rgb("ffd2a0"))
    img.save(path)


def draw_coin(path):
    """Four frames of a spinning coin, 12 x 12 each."""
    img = Image.new("RGBA", (48, 12), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for i, half_width in enumerate([5, 3, 1, 3]):
        cx = i * 12 + 6
        d.ellipse([cx - half_width, 1, cx + half_width, 10], fill=rgb("f5c242"), outline=rgb("a8761a"))
        if half_width > 2:
            d.line([cx, 3, cx, 8], fill=rgb("fff0a8"))
    img.save(path)


def draw_flag(path):
    img = Image.new("RGBA", (16, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle([2, 2, 3, 31], fill=rgb("d9d9d9"))
    d.ellipse([1, 0, 4, 3], fill=rgb("f5c242"))
    d.polygon([(4, 3), (15, 7), (4, 12)], fill=rgb("ff8c1a"), outline=rgb("b35a00"))
    img.save(path)


# --- Sounds ------------------------------------------------------------------------------------------------

RATE = 44100


def tone(freq_start, freq_end, length, decay, square=False):
    t = np.arange(int(length * RATE)) / RATE
    freq = freq_start + (freq_end - freq_start) * t / length
    phase = 2 * np.pi * np.cumsum(freq) / RATE
    wave_ = np.sign(np.sin(phase)) * 0.5 if square else np.sin(phase)
    env = np.minimum(1.0, t / 0.004) * np.exp(-t * decay) * np.minimum(1.0, (length - t) / 0.02)
    return wave_ * env


def write_wav(path, samples, peak=0.5):
    samples = samples / np.max(np.abs(samples)) * peak
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((samples * 32767).astype("<i2").tobytes())


def sounds(folder):
    folder.mkdir(parents=True, exist_ok=True)
    write_wav(folder / "jump.wav", tone(260, 620, 0.18, 9, square=True) * 0.6, 0.35)
    coin = np.concatenate([tone(988, 988, 0.07, 4), tone(1319, 1319, 0.28, 9)])
    write_wav(folder / "coin.wav", coin, 0.4)
    notes = [523.25, 659.25, 783.99, 1046.5]
    win = np.concatenate([tone(f, f, 0.12 if i < 3 else 0.6, 3 if i == 3 else 6) for i, f in enumerate(notes)])
    write_wav(folder / "win.wav", win, 0.45)


# --- Project files -----------------------------------------------------------------------------------------

def to_json(value, indent=0):
    pad = "  " * indent
    if isinstance(value, float) and value.is_integer():
        value = int(value)
    if isinstance(value, float):
        return repr(round(value, 6))
    if isinstance(value, dict):
        if not value:
            return "{}"
        items = [f'{pad}  {json.dumps(k)}: {to_json(v, indent + 1)}' for k, v in value.items()]
        return "{\n" + ",\n".join(items) + "\n" + pad + "}"
    if isinstance(value, list):
        if all(not isinstance(v, (dict, list)) for v in value):
            return "[" + ", ".join(to_json(v) for v in value) + "]"
        return "[\n" + ",\n".join(pad + "  " + to_json(v, indent + 1) for v in value) + "\n" + pad + "]"
    return json.dumps(value, ensure_ascii=False)


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(to_json(data) + "\n")


def meta(name, kind, extra=None):
    m = {"format": "rolo.asset", "version": 1, "id": IDS[name], "type": kind}
    m.update(extra or {})
    return m


def transform(x=0.0, y=0.0, scale=(1, 1)):
    return {"type": "rolo.Transform", "position": [x, y], "rotation": 0, "scale": list(scale)}


def sprite(image, size):
    return {"type": "rolo.Sprite", "image": IDS[image], "size": list(size), "color": "#ffffffff",
            "flipX": False, "flipY": False}


def collider(size, offset=(0, 0), solid=True, shape="box"):
    return {"type": "rolo.Collider", "size": list(size), "offset": list(offset), "solid": solid, "shape": shape}


def body(kind="dynamic", mass=1.0, friction=0.4, bounciness=0.0, lock=False):
    return {"type": "rolo.RigidBody", "bodyType": kind, "mass": mass, "gravityScale": 1, "friction": friction,
            "bounciness": bounciness, "drag": 0, "lockRotation": lock, "fast": False}


def text(words, size=18, colour="#ffffffff", align="left", width=0):
    return {"type": "rolo.Text", "text": words, "source": "text", "font": None, "size": size, "color": colour,
            "align": align, "width": width}


def shape(size, colour, kind="rectangle"):
    return {"type": "rolo.Shape", "kind": kind, "size": list(size), "color": colour, "filled": True, "lineWidth": 2}


def trigger(on, actions, once=False):
    return {"type": "rolo.Trigger", "on": on, "prompt": "", "event": "", "delay": 1.0, "once": once,
            "data": {"condition": None, "actions": actions}}


class Scene:
    def __init__(self, name, prefix):
        self.name, self.prefix, self.n, self.entities = name, prefix, 0, []

    def add(self, name, components, parent=None, active=True):
        self.n += 1
        e = {"id": f"{self.prefix}{self.n:010x}"}
        if parent:
            e["parent"] = parent
        e.update({"name": name, "active": active, "components": components})
        self.entities.append(e)
        return e["id"]


def at(col, row, dy=0.0):
    """The centre of a cell (column, row from the top), in points."""
    return [(col + 0.5) * CELL, (H - 1 - row + 0.5) * CELL + dy]


def level():
    s = Scene("Level 1", "504c01")
    # Sky and clouds, behind everything.
    s.add("Sky", [transform(W * CELL / 2, H * CELL / 2 + 200), shape((W * CELL + 1400, H * CELL + 1200), "#7ec8f0ff")])
    for i, (col, row, w) in enumerate([(4, 1, 160), (19, 0.5, 220), (37, 1.5, 180), (56, 0.8, 200)]):
        x, y = at(col, row)
        cloud = s.add(f"Cloud {i + 1}", [transform(x, y), shape((w, w * 0.35), "#ffffffd0", "ellipse")])
        s.add("Puff", [transform(w * 0.15, w * 0.12), shape((w * 0.55, w * 0.4), "#ffffffd0", "ellipse")], parent=cloud)

    rows = []
    for r, line in enumerate(LEVEL):
        cells = [TILE.get(ch, -1) for ch in line]
        rows.append([H - 1 - r, 0] + cells)
    s.add("Ground", [transform(scale=(SCALE, SCALE)), {
        "type": "rolo.Tilemap", "tileset": IDS["tiles.ltileset"], "cellSize": [PIXEL, PIXEL],
        "data": {"layers": [{"name": "Ground", "visible": True, "rows": rows}], "solid": []}}])

    things = {ch: [] for ch in "1CXOLF"}
    for r, line in enumerate(LEVEL):
        for c, ch in enumerate(line):
            if ch in things:
                things[ch].append((c, r))

    for i, (c, r) in enumerate(things["X"]):
        x, y = at(c, r)
        s.add(f"Crate {i + 1}", [transform(x, y, (SCALE, SCALE)), sprite("crate.png", (16, 16)),
                                 collider((16, 16)), body(mass=2.0, friction=0.6)])
    for i, (c, r) in enumerate(things["O"]):
        x, y = at(c, r)
        s.add(f"Ball {i + 1}", [transform(x, y, (SCALE, SCALE)), sprite("ball.png", (16, 16)),
                                collider((16, 16), shape="circle"), body(mass=0.5, friction=0.4, bounciness=0.7)])
    for i, (c, r) in enumerate(things["L"]):
        x, y = at(c, r)
        s.add(f"Lift {i + 1}", [transform(x, y, (SCALE, SCALE)), shape((48, 8), "#8c6230ff"),
                                collider((48, 8)), body(kind="kinematic"),
                                {"type": "rolo.Script", "script": IDS["lift.lua"]}])
    for i, (c, r) in enumerate(things["C"]):
        x, y = at(c, r)
        s.add(f"Coin {i + 1}", [transform(x, y, (SCALE, SCALE)), sprite("coin.png", (12, 12)),
                                {"type": "rolo.Animator", "animations": IDS["coin.lanim"], "clip": "spin",
                                 "speed": 1, "playing": True},
                                collider((10, 10), solid=False, shape="circle"),
                                trigger("enterArea", [{"type": "playSound", "sound": IDS["coin.wav"]},
                                                      {"type": "add", "variable": "coins", "value": 1},
                                                      {"type": "destroy", "entity": None}])])
    (fc, fr) = things["F"][0]
    x, y = at(fc, fr, dy=CELL / 2)   # two tiles tall, standing on the ground
    s.add("Flag", [transform(x, y, (SCALE, SCALE)), sprite("flag.png", (16, 32)),
                   collider((10, 32), solid=False),
                   trigger("enterArea", [{"type": "playSound", "sound": IDS["win.wav"]},
                                         {"type": "setActive", "entity": "WIN", "active": True},
                                         {"type": "log", "text": "Level complete with {coins} coins!"}], once=True)])

    # The hero, the camera following them, and the words on screen.
    (pc, pr) = things["1"][0]
    x, y = at(pc, pr)
    hero = s.add("Player", [
        transform(x, y, (SCALE, SCALE)),
        sprite("hero_walk.png", (16, 24)),
        {"type": "rolo.Animator", "animations": IDS["hero.lanim"], "clip": "idle_right", "speed": 1, "playing": True},
        collider((10, 22), shape="capsule"),
        body(mass=1.0, friction=0.0, lock=True),
        {"type": "rolo.Interactor", "key": "E"},
        {"type": "rolo.Script", "script": IDS["player.lua"]},
    ])
    cam = s.add("Camera", [transform(scale=(1 / SCALE, 1 / SCALE)), {"type": "rolo.Camera", "zoom": 1},
                           {"type": "rolo.CameraBounds", "min": [0, 0], "max": [W * CELL, H * CELL],
                            "view": list(VIEW)}], parent=hero)
    left, top = -VIEW[0] / 2 + 24, VIEW[1] / 2 - 20
    s.add("Coins", [transform(left, top), text("Coins  {coins} / " + str(len(things["C"])), 22, "#1a2a44ff")],
          parent=cam)
    s.add("Help", [transform(-left, top), text("{button:move left} {button:move right} run   {button:jump} jump   {button:pause} menu", 18, "#1a2a44e0",
                                                align="right")], parent=cam)
    win = s.add("Well Done", [transform(0, 120), shape((620, 110), "#1a2a44e0")], parent=cam, active=False)
    s.add("Words", [transform(0, 34), text("Well done!\nYou found {coins} coins.", 30, "#ffd84aff", align="center")],
          parent=win)
    # The Controls screen (scripts/controls.lua opens and fills it).
    controls = s.add("Controls", [transform(), {"type": "rolo.Script", "script": IDS["controls.lua"]}], parent=cam)
    panel = s.add("Controls Panel", [transform(0, 70), shape((860, 300), "#141c30f0")], parent=controls, active=False)
    s.add("Controls Text", [transform(-400, 120), text("", 24, "#ffffffff")], parent=panel)
    # The pause menu (scripts/pause.lua).
    pause = s.add("Pause", [transform(), {"type": "rolo.Script", "script": IDS["pause.lua"]}], parent=cam)
    panel = s.add("Pause Panel", [transform(0, 70), shape((560, 260), "#141c30f0")], parent=pause, active=False)
    s.add("Pause Text", [transform(-240, 100), text("", 26, "#ffffffff")], parent=panel)
    for e in s.entities:
        for comp in e["components"]:
            if comp["type"] == "rolo.Trigger":
                for a in comp["data"]["actions"]:
                    if a.get("entity") == "WIN":
                        a["entity"] = win
    return {"format": "rolo.scene", "version": 1, "name": s.name, "entities": s.entities}


def main():
    root = Path(sys.argv[1])
    project_file = root / "project.rolo"
    if not project_file.exists():
        sys.exit(f"{root} is not a Rolo project. Make one first: rolo_editor --new-project \"{root}\" --frames 1")
    project = json.loads(project_file.read_text())

    art = root / "assets" / "art"
    art.mkdir(parents=True, exist_ok=True)
    draw_tiles(art / "tiles.png")
    draw_crate(art / "crate.png")
    draw_ball(art / "ball.png")
    draw_coin(art / "coin.png")
    draw_flag(art / "flag.png")
    shutil.copyfile(HERO, art / "hero_walk.png")
    for name in ("tiles.png", "hero_walk.png", "crate.png", "ball.png", "coin.png", "flag.png"):
        write(art / (name + ".meta"), meta(name, "image", {"filter": "pixel"}))
    write(root / "assets/tiles.ltileset", {"format": "rolo.tileset", "version": 1, "image": IDS["tiles.png"],
                                           "tileSize": [16, 16], "margin": 0, "spacing": 0, "solid": SOLID})
    write(root / "assets/tiles.ltileset.meta", meta("tiles.ltileset", "tileset"))
    hero_clips = []
    for row, facing in [(1, "left"), (2, "right")]:
        first = row * 3
        hero_clips += [{"id": f"idle_{facing}", "frames": [first], "fps": 1, "loop": True},
                       {"id": f"walk_{facing}", "frames": [first + 1, first, first + 2, first], "fps": 10, "loop": True}]
    for name, image, frame, clips in [
            ("hero.lanim", "hero_walk.png", (16, 24), hero_clips),
            ("coin.lanim", "coin.png", (12, 12), [{"id": "spin", "frames": [0, 1, 2, 3], "fps": 8, "loop": True}])]:
        write(root / "assets/animations" / name, {"format": "rolo.animations", "version": 1, "image": IDS[image],
                                                  "frameSize": list(frame), "margin": 0, "spacing": 0, "clips": clips})
        write(root / "assets/animations" / (name + ".meta"), meta(name, "animation"))

    sound_folder = root / "assets" / "sounds"
    sounds(sound_folder)
    for name in ("jump.wav", "coin.wav", "win.wav"):
        write(sound_folder / (name + ".meta"), meta(name, "audio"))

    content = root / "content"
    write(content / "platformer.lvars", {"format": "rolo.variables", "version": 1, "variables": [
        {"id": "coins", "type": "int", "initial": 0, "description": "Coins collected"},
        {"id": "menu", "type": "text", "initial": "", "description": "The menu open: \"\", \"pause\" or \"controls\""}]})
    write(content / "platformer.lvars.meta", meta("platformer.lvars", "variables"))

    scripts = root / "scripts"
    scripts.mkdir(parents=True, exist_ok=True)
    for name, code in (("player.lua", PLAYER), ("lift.lua", LIFT), ("controls.lua", CONTROLS), ("pause.lua", PAUSE)):
        (scripts / name).write_text(code, encoding="utf-8")
        write(scripts / (name + ".meta"), meta(name, "script"))

    for old in ("scenes/main.lscene", "scenes/main.lscene.meta"):
        (root / old).unlink(missing_ok=True)
    write(root / "scenes" / "level.lscene", level())
    write(root / "scenes" / "level.lscene.meta", meta("level.lscene", "scene"))
    project["name"] = "Platformer Starter"   # the wizard renames copies after the new project
    project["startScene"] = "scenes/level.lscene"
    project["escapeQuits"] = False           # Escape opens the pause menu (scripts/pause.lua)
    write(project_file, project)


if __name__ == "__main__":
    main()
