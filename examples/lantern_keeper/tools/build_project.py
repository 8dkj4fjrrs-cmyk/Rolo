"""Builds The Lantern Keeper's project files.

    rolo_editor --new-project "<folder>"      # 1. a fresh, empty project
    python3 build_project.py "<folder>"          # 2. this script

Starting from the fresh project the editor makes, this adds the art (drawn
by make_art.py), the sounds and music (made by make_sounds.py), the tileset
and animations, the story content (variables,
dialogues, quests) and the three scenes, and makes the village the start
scene. It writes exactly the files the editor writes when you do the same
work by hand; docs/making-the-lantern-keeper.md walks through doing it in
the editor. Fixed IDs keep the output identical every time it runs.
"""
import json
import math
import random
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# --- IDs ------------------------------------------------------------------------------------------

_ASSETS = ["tiles.png", "hero_walk.png", "maren.png", "wren.png", "nell.png", "biscuit.png", "flame.png",
           "lantern.png", "shrine.png", "gate.png", "sign.png",
           "tiles.ltileset", "hero.lanim", "maren.lanim", "wren.lanim", "nell.lanim", "biscuit.lanim",
           "flame.lanim", "lantern.lanim", "shrine.lanim",
           "village.lscene", "woods.lscene", "mine.lscene",
           "keeper.lvars", "lantern.lquest", "biscuit.lquest",
           "intro.ldialogue", "maren.ldialogue", "wren.ldialogue", "nell.ldialogue", "biscuit_found.ldialogue",
           "biscuit_home.ldialogue", "flame.ldialogue", "lantern.ldialogue", "gate.ldialogue",
           "shrine.ldialogue", "village_sign.ldialogue", "mine_sign.ldialogue",
           "flame.wav", "rest.wav", "gate.wav", "lantern_lit.ogg", "night.ogg", "mine.ogg"]
IDS = {name: f"4b45455000{i + 1:06x}" for i, name in enumerate(_ASSETS)}

# --- Tiles (the order of make_art.py's TILES) -------------------------------------------------------

TILE = {".": 0, ",": 1, "=": 2, "~": 3, "#": 4, "T": 5, "H": 6, "R": 7, "D": 8, "F": 9, ":": 10, "b": 11,
        "_": 12, "M": 13, "p": 14, "W": 15, "o": 16, "Y": 17, "r": 18, "m": 19, "O": 20, "|": 21, "x": 22,
        '"': 23}
SOLID_TILES = [TILE[c] for c in '~#THRDFbMWYrOx"']

PIXEL = 16                 # a tile is 16 x 16 pixels...
SCALE = 3                  # ...drawn three times as large,
CELL = PIXEL * SCALE       # so one cell of a map is 48 points.
VIEW = (1280, 720)         # the game's view

# --- Scene building helpers ----------------------------------------------------------------------------

class Ids:
    def __init__(self, prefix):
        self.prefix, self.n = prefix, 0

    def __call__(self):
        self.n += 1
        return f"{self.prefix}{self.n:010x}"


def transform(x=0.0, y=0.0, scale=(1, 1)):
    return {"type": "rolo.Transform", "position": [x, y], "rotation": 0, "scale": list(scale)}


def sprite(image, size, flip=False):
    return {"type": "rolo.Sprite", "image": IDS[image], "size": list(size), "color": "#ffffffff",
            "flipX": flip, "flipY": False}


def animator(lanim, clip):
    return {"type": "rolo.Animator", "animations": IDS[lanim], "clip": clip, "speed": 1, "playing": True}


def collider(size, offset=(0, 0), solid=True):
    return {"type": "rolo.Collider", "size": list(size), "offset": list(offset), "solid": solid}


def trigger(on, actions, condition=None, prompt="", event="", delay=1.0, once=False):
    return {"type": "rolo.Trigger", "on": on, "prompt": prompt, "event": event, "delay": delay, "once": once,
            "data": {"condition": condition, "actions": actions}}


def shape(size, colour, kind="rectangle"):
    return {"type": "rolo.Shape", "kind": kind, "size": list(size), "color": colour, "filled": True,
            "lineWidth": 2}


def text(words, size=18, colour="#ffffffff", align="left", width=0, source="text"):
    return {"type": "rolo.Text", "text": words, "source": source, "font": None, "size": size,
            "color": colour, "align": align, "width": width}


def sound(name, music=False, volume=1.0, fade=1.0):
    """A Sound component: plays when its object appears (music: one piece at a time)."""
    return {"type": "rolo.Sound", "sound": IDS[name], "volume": volume, "pitch": 1, "loop": False,
            "playOnStart": True, "music": music, "fade": fade}


# Conditions and actions (the JSON form; the editor shows them as one line each).
def var_is(name, value, op="=="):
    return {"type": "compare", "variable": name, "op": op, "value": value}


def all_of(*conds):
    return {"type": "all", "conditions": list(conds)}


def any_of(*conds):
    return {"type": "any", "conditions": list(conds)}


def quest_is(quest, state):
    return {"type": "quest", "quest": IDS[quest], "state": state}


def say(dialogue):
    return {"type": "startDialogue", "dialogue": IDS[dialogue]}


def play_sound(name):
    return {"type": "playSound", "sound": IDS[name]}


def set_var(name, value):
    return {"type": "set", "variable": name, "value": value}


class Scene:
    def __init__(self, name, prefix, rows):
        self.name, self.ids, self.entities = name, Ids(prefix), []
        self.rows = rows
        self.w, self.h = len(rows[0]), len(rows)
        assert all(len(r) == self.w for r in rows), f"{name}: rows of different lengths"

    def at(self, col, row):
        """The world point at the middle of cell (col, row), counting rows
        from the top of the map as drawn. Halves are allowed (between cells)."""
        return [(col + 0.5) * CELL, (self.h - 1 - row + 0.5) * CELL]

    def add(self, name, components, parent=None, active=True):
        e = {"id": self.ids(), "name": name, "active": active, "components": components}
        if parent:
            e = {"id": e["id"], "parent": parent, "name": name, "active": active, "components": components}
        self.entities.append(e)
        return e["id"]

    def ground(self):
        """The tilemap: rows as drawn (top first) become rows from y = 0 up."""
        rows = []
        for i, line in enumerate(self.rows):
            y = self.h - 1 - i
            rows.append([y, 0] + [TILE[c] for c in line])
        self.add("Ground", [transform(scale=(SCALE, SCALE)), {
            "type": "rolo.Tilemap", "tileset": IDS["tiles.ltileset"], "cellSize": [PIXEL, PIXEL],
            "data": {"layers": [{"name": "Ground", "visible": True, "rows": rows}], "solid": []}}])

    def person(self, name, col, row, image, lanim, dialogue, prompt="Talk", flip=False, size=(16, 24),
               active=True, feet=(12, 6), area=(26, 24)):
        """Someone (or something) to talk to: a sprite with an idle
        animation, solid feet, and a talk area around them."""
        x, y = self.at(col, row)
        body = self.add(name, [transform(x, y, (SCALE, SCALE)), sprite(image, size, flip), animator(lanim, "idle"),
                               collider(feet, (0, -size[1] / 2 + feet[1] / 2 + 1))], active=active)
        self.add("Talk Area", [transform(0, -size[1] / 2 + 4), collider(area, solid=False),
                               trigger("interact", [say(dialogue)], prompt=prompt)], parent=body)
        return body

    def scene_json(self):
        return {"format": "rolo.scene", "version": 1, "name": self.name, "entities": self.entities}


def door(scene, name, col, row, target, arrive_at):
    """A way to another scene: two cells tall, at the map's edge."""
    x, y = scene.at(col, row)
    scene.add(name, [transform(x, y), collider((CELL, CELL * 2), solid=False),
                     {"type": "keeper.SceneDoor", "scene": IDS[target], "arriveAt": arrive_at}])


def spawn(scene, name, col, row):
    x, y = scene.at(col, row)
    scene.add(name, [transform(x, y), {"type": "keeper.SpawnPoint"}])


def flame(scene, name, col, row):
    """A lost flame: walking into it picks it up."""
    x, y = scene.at(col, row)
    scene.add(name, [transform(x, y, (SCALE, SCALE)), sprite("flame.png", (16, 16)), animator("flame.lanim", "flicker"),
                     collider((12, 12), solid=False),
                     trigger("enterArea", [play_sound("flame.wav"),
                                           {"type": "add", "variable": "flames", "value": 1},
                                           {"type": "log", "text": "Found a lost flame ({flames}/3)"},
                                           say("flame.ldialogue"),
                                           {"type": "destroy", "entity": None}])])


def sign(scene, name, col, row, dialogue):
    x, y = scene.at(col, row)
    body = scene.add(name, [transform(x, y, (SCALE, SCALE)), sprite("sign.png", (16, 16)),
                            collider((10, 4), (0, -6))])
    scene.add("Read Area", [transform(0, -6), collider((22, 18), solid=False),
                            trigger("interact", [say(dialogue)], prompt="Read")], parent=body)


def player(scene, col, row, darkness, night_while=None):
    """The hero, the camera following them, and everything on screen."""
    x, y = scene.at(col, row)
    hero = scene.add("Player", [
        transform(x, y, (SCALE, SCALE)), sprite("hero_walk.png", (16, 24)), animator("hero.lanim", "idle_down"),
        collider((10, 5), (0, -9.5)),                     # only the feet collide
        {"type": "rolo.Interactor", "key": "E"},
        {"type": "keeper.PlayerController", "speed": 170},
    ])
    camera = scene.add("Camera", [
        transform(scale=(1 / SCALE, 1 / SCALE)), {"type": "rolo.Camera", "zoom": 1},
        {"type": "keeper.CameraLimits", "min": [0, 0], "max": [scene.w * CELL, scene.h * CELL], "view": list(VIEW)},
    ], parent=hero)
    ui = {}
    # The night: a dark veil over the world, under the interface. Saved
    # switched off, so the scene is easy to see while editing; it falls when
    # the game starts the scene (in the village, only while the lantern is dark).
    ui["Night"] = scene.add("Night", [transform(), shape(VIEW, darkness)], parent=camera, active=False)
    left, top = -VIEW[0] / 2 + 24, VIEW[1] / 2 - 20
    scene.add("Flame Count", [transform(left, top), text("Flames  {flames} / 3", 24, "#ffc84aff")], parent=camera)
    scene.add("Quest Log", [transform(left, top - 38), text("", 16, "#ecececff", source="questLog")], parent=camera)
    scene.add("Help", [transform(-left, top), text("Arrows / WASD walk · E talk · F5 save · F9 load", 14,
                                                     "#ffffffb0", align="right")], parent=camera)
    scene.add("Prompt", [transform(0, -120), text("", 20, "#ffc84aff", align="center", source="interactionPrompt")],
              parent=camera)
    box = scene.add("Dialogue Box", [transform(0, -VIEW[1] / 2 + 100), shape((1040, 168), "#120e1aee"),
                                     {"type": "rolo.DialogueView", "marker": "> "}], parent=camera, active=False)
    scene.add("Speaker", [transform(-496, 68), text("", 20, source="dialogueSpeaker")], parent=box)
    scene.add("Line", [transform(-496, 38), text("", 19, width=990, source="dialogueLine")], parent=box)
    scene.add("Choices", [transform(-496, -14), text("", 19, "#ffd88aff", source="dialogueChoices")], parent=box)
    scene.add("Continue", [transform(500, -62), text("E / Space", 12, "#ffffff80", align="right")], parent=box)
    scene.add("Nightfall", [transform(), trigger("sceneStart", [{"type": "setActive", "entity": ui["Night"],
                                                                 "active": True}], condition=night_while)])
    ui["camera"] = camera
    return ui


# --- Maps ----------------------------------------------------------------------------------------------

class Grid:
    """A map being drawn: rows from the top, as on screen."""

    def __init__(self, w, h, fill):
        self.w, self.h = w, h
        self.g = [[fill] * w for _ in range(h)]

    def put(self, col, row, ch):
        if 0 <= col < self.w and 0 <= row < self.h:
            self.g[row][col] = ch

    def rect(self, c0, r0, c1, r1, ch):
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                self.put(c, r, ch)

    def get(self, col, row):
        return self.g[row][col]

    def border(self, ch, thickness=1):
        for r in range(self.h):
            for c in range(self.w):
                if c < thickness or r < thickness or c >= self.w - thickness or r >= self.h - thickness:
                    self.g[r][c] = ch

    def ellipse(self, cc, cr, rc, rr, ch):
        for r in range(self.h):
            for c in range(self.w):
                if ((c - cc) / rc) ** 2 + ((r - cr) / rr) ** 2 <= 1.0:
                    self.g[r][c] = ch

    def sprinkle(self, ch, chance, only, seed):
        rnd = random.Random(seed)
        for r in range(self.h):
            for c in range(self.w):
                if self.g[r][c] in only and rnd.random() < chance:
                    self.g[r][c] = ch

    def rows(self):
        return ["".join(r) for r in self.g]


def village_map():
    m = Grid(36, 24, ".")
    m.sprinkle(",", 0.07, ".", seed=7)
    m.border("T")
    m.rect(1, 1, 1, 22, "T")
    # The lantern square, at the top.
    m.rect(13, 1, 22, 6, "o")
    # Keeper Maren's house (left) and Wren's bakery (right).
    for c0 in (2, 27):
        m.rect(c0, 2, c0 + 5, 3, "R")
        m.rect(c0, 4, c0 + 5, 5, "H")
        m.put(c0 + 1, 4, "W")
        m.put(c0 + 4, 4, "W")
    m.put(4, 5, "D")
    m.put(30, 5, "D")
    # Roads: the main road east (to Whisperwood), paths to the doors and the square.
    m.rect(3, 8, 35, 9, "=")
    m.rect(4, 6, 4, 7, "=")
    m.rect(30, 6, 30, 7, "=")
    m.rect(17, 7, 18, 7, "=")
    # The well, the pond with reeds, Nell's family's house and a garden.
    m.put(11, 11, "O")
    m.ellipse(7, 17.5, 4.2, 2.6, "~")
    for c in (5, 6, 9):
        m.put(c, 15, '"')
    m.rect(24, 12, 29, 13, "R")
    m.rect(24, 14, 29, 15, "H")
    m.put(25, 14, "W")
    m.put(28, 14, "W")
    m.put(26, 15, "D")
    m.rect(26, 16, 26, 18, "=")
    m.rect(12, 18, 26, 19, "=")
    m.rect(12, 10, 13, 17, "=")
    m.rect(15, 11, 21, 11, "F")             # a fenced garden, its gate facing the road
    m.rect(15, 16, 21, 16, "F")
    m.rect(15, 11, 15, 16, "F")
    m.rect(21, 11, 21, 16, "F")
    m.rect(16, 12, 20, 15, ",")
    m.put(18, 11, ",")
    for (c, r) in [(9, 3), (24, 9 + 2), (31, 18), (33, 14), (3, 12), (10, 21), (23, 21), (33, 3)]:
        m.put(c, r, "b")
    m.rect(34, 8, 35, 9, "=")              # the way out east
    return m.rows()


def woods_map():
    m = Grid(40, 26, ":")
    m.sprinkle("m", 0.03, ":", seed=3)
    m.sprinkle("Y", 0.2, ":", seed=5)
    m.border("Y", 2)
    # A sunny clearing to the north (where Biscuit is).
    m.ellipse(28.5, 5, 5.2, 2.6, ".")
    m.sprinkle(",", 0.25, ".", seed=9)
    # The pond to the south-west.
    m.ellipse(8.5, 21, 4, 2.2, "~")
    m.put(6, 19, '"')
    m.put(10, 19, '"')
    m.rect(10, 17, 15, 18, ":")            # a nook east of the pond (a lost flame)
    m.rect(13, 19, 15, 21, ":")
    # The cliff with the old mine's gate.
    m.rect(34, 8, 35, 25, "#")
    # Paths: from the village (west) east to the gate, north to the clearing, south to the pond.
    m.rect(0, 12, 21, 13, "=")
    m.rect(20, 7, 21, 13, "=")
    m.rect(20, 7, 26, 7, "=")
    m.rect(20, 14, 21, 17, "=")
    m.rect(20, 16, 39, 17, "=")
    m.rect(8, 14, 9, 18, "=")
    # Keep paths and the clearing free of trees on both sides.
    for r in range(m.h):
        for c in range(m.w):
            if m.get(c, r) == "Y" and 2 <= c < 38 and 2 <= r < 24:
                near_path = any(0 <= c + dc < m.w and 0 <= r + dr < m.h and m.get(c + dc, r + dr) in "=."
                                for dc in (-1, 0, 1) for dr in (-1, 0, 1))
                if near_path:
                    m.put(c, r, ":")
    m.rect(34, 8, 35, 15, "#")
    m.rect(34, 18, 35, 25, "#")
    m.rect(34, 16, 35, 17, "=")             # under the gate
    m.rect(36, 2, 39, 15, "Y")
    m.rect(36, 18, 39, 25, "Y")
    m.rect(38, 16, 39, 17, "=")             # the way into the mine
    return m.rows()


def mine_map():
    m = Grid(34, 22, "M")
    # The way in (west), the main shaft with rails, and three chambers.
    m.rect(0, 10, 16, 11, "_")
    m.rect(16, 2, 17, 19, "|")
    m.rect(4, 2, 12, 7, "_")                # north-west chamber
    m.rect(12, 4, 16, 5, "_")
    m.rect(22, 14, 30, 19, "_")             # south-east chamber
    m.rect(18, 17, 22, 18, "_")
    m.rect(18, 10, 30, 11, "_")             # east tunnel (a dead end with crates)
    m.rect(26, 5, 31, 11, "_")
    m.rect(3, 14, 9, 19, "_")               # south-west chamber
    m.rect(9, 16, 16, 17, "_")
    for (c, r) in [(5, 3), (11, 6), (12, 2), (27, 6), (30, 6), (31, 9), (23, 19), (29, 14), (3, 19), (8, 14),
                   (4, 15)]:
        m.put(c, r, "r")
    for (c, r) in [(28, 5), (29, 5), (31, 5), (26, 10), (9, 19), (25, 14)]:
        m.put(c, r, "x")
    m.rect(0, 10, 1, 11, "_")
    return m.rows()


# --- Scenes ------------------------------------------------------------------------------------------------

NIGHT = "#0a1430a0"
DUSK = "#0a143070"
MINE_DARK = "#00000080"


def village():
    s = Scene("Village", "4b4501", village_map())
    s.ground()
    # The great lantern, dark, and the same lantern lit (switched on at the end).
    x, y = s.at(17.5, 3)
    lantern = s.add("Lantern", [transform(x, y, (SCALE, SCALE)), sprite("lantern.png", (32, 64)),
                                animator("lantern.lanim", "dark"), collider((12, 4), (0, -30))])
    s.add("Look Area", [transform(0, -28), collider((26, 14), solid=False),
                        trigger("interact", [say("lantern.ldialogue")], prompt="Look")], parent=lantern)
    lit = s.add("Lantern Light", [transform(x, y, (SCALE, SCALE)), sprite("lantern.png", (32, 64)),
                                  animator("lantern.lanim", "lit")], active=False)
    s.add("Glow", [transform(0, 9), shape((64, 48), "#ffd86a30", "ellipse")], parent=lit)

    s.person("Keeper Maren", 4, 7, "maren.png", "maren.lanim", "maren.ldialogue")
    s.person("Wren", 31.5, 7, "wren.png", "wren.lanim", "wren.ldialogue", flip=True)
    s.person("Nell", 23, 17, "nell.png", "nell.lanim", "nell.ldialogue")
    # Biscuit appears at home once she has been found in the woods.
    cat = s.person("Biscuit", 28.5, 7, "biscuit.png", "biscuit.lanim", "biscuit_home.ldialogue", prompt="Pet",
                   size=(16, 16), active=False, feet=(10, 4), area=(24, 18))

    x, y = s.at(22.5, 6)
    shrine = s.add("Wayshrine", [transform(x, y, (SCALE, SCALE)), sprite("shrine.png", (16, 24)),
                                 animator("shrine.lanim", "idle"), collider((10, 4), (0, -10))])
    s.add("Rest Area", [transform(0, -8), collider((24, 18), solid=False),
                        trigger("interact", [play_sound("rest.wav"), {"type": "saveGame", "slot": "quick"},
                                             say("shrine.ldialogue")],
                                prompt="Rest (save)")], parent=shrine)
    sign(s, "Signpost", 33, 6.5, "village_sign.ldialogue")

    spawn(s, "From Woods", 32.5, 8.5)
    door(s, "Road to Whisperwood", 35.5, 8.5, "woods.lscene", "From Village")

    # The story's moments, as triggers on empty objects.
    s.add("Story Start", [transform(), trigger("sceneStart", [say("intro.ldialogue")], once=True)])
    s.add("Biscuit Comes Home", [transform(), trigger("conditionMet", [{"type": "setActive", "entity": cat, "active": True}],
                                                      condition=var_is("cat_found", True))])
    s.add("Lantern Lights Up", [transform(), trigger("conditionMet", [
        play_sound("lantern_lit.ogg"),
        {"type": "setActive", "entity": lit, "active": True},
        {"type": "setActive", "entity": "NIGHT", "active": False},
    ], condition=var_is("lantern_lit", True))])
    s.add("Ending", [transform(), trigger("conditionMet", [
        {"type": "wait", "seconds": 1.5},
        {"type": "setActive", "entity": "BANNER", "active": True},
        {"type": "wait", "seconds": 12},
        {"type": "setActive", "entity": "BANNER", "active": False},
    ], condition=var_is("lantern_lit", True), once=True)])

    ui = player(s, 7, 8.5, NIGHT, night_while=var_is("lantern_lit", False))
    banner = s.add("The End", [transform(0, -40), shape((760, 112), "#120e1ae0")], parent=ui["camera"], active=False)
    s.add("Words", [transform(0, 40), text("The lantern of Emberwick burns again.\nThank you for playing!", 28,
                                           "#ffe08aff", align="center")], parent=banner)
    # Fill in the IDs of objects made after the triggers that refer to them.
    for e in s.entities:
        for comp in e["components"]:
            if comp["type"] == "rolo.Trigger":
                for a in comp["data"]["actions"]:
                    if a.get("entity") == "NIGHT":
                        a["entity"] = ui["Night"]
                    if a.get("entity") == "BANNER":
                        a["entity"] = banner
    # Music (added last, so the objects above keep their IDs from before
    # the game had sound). Whisperwood plays the same piece: it carries on.
    s.add("Music", [transform(), sound("night.ogg", music=True, volume=0.7, fade=2.0)])
    return s


def woods():
    s = Scene("Whisperwood", "4b4502", woods_map())
    s.ground()
    cat = s.person("Biscuit", 29, 5, "biscuit.png", "biscuit.lanim", "biscuit_found.ldialogue", prompt="Pet",
                   size=(16, 16), feet=(10, 4), area=(24, 18))
    s.add("Runs Home", [transform(), trigger("conditionMet", [{"type": "destroy", "entity": cat}],
                                             condition=var_is("cat_found", True))], parent=cat)
    flame(s, "Lost Flame", 13, 20)
    # The old mine's gate: locked until Wren's key opens it (for good).
    x, y = s.at(34.5, 16.5)
    gate = s.add("Mine Gate", [transform(x, y, (SCALE, SCALE)), sprite("gate.png", (32, 32)), collider((32, 32))])
    s.add("Lock", [transform(-18, 0), collider((12, 24), solid=False),
                   trigger("interact", [say("gate.ldialogue")], prompt="Open")], parent=gate)
    s.add("Opens", [transform(), trigger("gameEvent", [play_sound("gate.wav"), {"type": "destroy", "entity": gate}],
                                         event="gate_open")],
          parent=gate)
    spawn(s, "From Village", 2.5, 12.5)
    spawn(s, "From Mine", 36.5, 16.5)
    door(s, "Road to Emberwick", -0.5, 12.5, "village.lscene", "From Woods")
    door(s, "Into the Mine", 39.5, 16.5, "mine.lscene", "From Woods")
    player(s, 2.5, 12.5, DUSK)
    s.add("Music", [transform(), sound("night.ogg", music=True, volume=0.7, fade=2.0)])
    return s


def mine():
    s = Scene("Old Mine", "4b4503", mine_map())
    s.ground()
    flame(s, "Lost Flame North", 6, 3)
    flame(s, "Lost Flame East", 28, 18)
    sign(s, "Miners' Sign", 3, 9, "mine_sign.ldialogue")
    spawn(s, "From Woods", 2.5, 10.5)
    door(s, "Way Out", -0.5, 10.5, "woods.lscene", "From Mine")
    player(s, 2.5, 10.5, MINE_DARK)
    s.add("Music", [transform(), sound("mine.ogg", music=True, volume=0.8, fade=2.0)])
    return s


# --- Content -----------------------------------------------------------------------------------------------

def variables():
    return {"format": "rolo.variables", "version": 1, "variables": [
        {"id": "flames", "type": "int", "initial": 0, "description": "Lost flames found"},
        {"id": "met_maren", "type": "bool", "initial": False, "description": "Maren asked Pip to find the flames"},
        {"id": "met_wren", "type": "bool", "initial": False, "description": "Wren asked about Biscuit"},
        {"id": "cat_found", "type": "bool", "initial": False, "description": "Biscuit was found in the woods"},
        {"id": "has_key", "type": "bool", "initial": False, "description": "Pip has the old mine's key"},
        {"id": "lantern_lit", "type": "bool", "initial": False, "description": "The great lantern burns again"},
        {"id": "arrival", "type": "text", "initial": "",
         "description": "Where the player arrives in the next scene (set by Scene Doors)"},
    ]}


def lantern_quest():
    return {"format": "rolo.quest", "version": 1,
            "name": "The Dark Lantern",
            "description": "The storm blew out Emberwick's great lantern and scattered its three flames.",
            "prerequisites": [],
            "startWhen": any_of(var_is("met_maren", True), var_is("flames", 1, ">=")),
            "objectives": [
                {"id": "find", "text": "Find the lost flames ({flames}/3)",
                 "completeWhen": var_is("flames", 3, ">="), "optional": False},
                {"id": "light", "text": "Light the great lantern",
                 "completeWhen": var_is("lantern_lit", True), "optional": False},
            ],
            "failWhen": None,
            "rewards": [{"type": "log", "text": "Emberwick is safe: the great lantern burns again."}]}


def biscuit_quest():
    return {"format": "rolo.quest", "version": 1,
            "name": "Where Is Biscuit?",
            "description": "Wren's cat Biscuit ran into Whisperwood when the lantern went dark.",
            "prerequisites": [],
            "startWhen": any_of(var_is("met_wren", True), var_is("cat_found", True)),
            "objectives": [
                {"id": "find", "text": "Find Biscuit in Whisperwood", "completeWhen": var_is("cat_found", True),
                 "optional": False},
                {"id": "tell", "text": "Tell Wren", "completeWhen": None, "optional": False},
            ],
            "failWhen": None,
            "rewards": [set_var("has_key", True), {"type": "log", "text": "Wren gave Pip the old mine's key."}]}


SPEAKERS = {
    "maren": {"id": "maren", "name": "Keeper Maren", "color": "#ffd27aff", "portrait": None},
    "wren": {"id": "wren", "name": "Wren", "color": "#9ee0a8ff", "portrait": None},
    "nell": {"id": "nell", "name": "Nell", "color": "#ffe06aff", "portrait": None},
    "biscuit": {"id": "biscuit", "name": "Biscuit", "color": "#f0a060ff", "portrait": None},
}


def dialogue(speakers, nodes, start=None):
    return {"format": "rolo.dialogue", "version": 1, "speakers": [SPEAKERS[s] for s in speakers],
            "start": start or nodes[0]["id"], "nodes": nodes}


def line(node_id, words, next_node=None, speaker=None, actions=None, choices=None):
    n = {"id": node_id}
    if speaker:
        n["speaker"] = speaker
    n["text"] = words
    if actions:
        n["actions"] = actions
    if choices:
        n["choices"] = choices
    else:
        n["next"] = next_node
    return n


def branch(node_id, branches, otherwise):
    return {"id": node_id, "branches": [{"condition": c, "next": n} for c, n in branches], "next": otherwise}


def choice(choice_id, words, next_node, actions=None):
    c = {"id": choice_id, "text": words}
    if actions:
        c["actions"] = actions
    c["next"] = next_node
    return c


def intro():
    return dialogue([], [
        line("storm", "Emberwick, the night after the great storm.", "lantern"),
        line("lantern", "For a hundred years the great lantern has burned above the village square, keeping the "
                        "night-mist away.", "dark"),
        line("dark", "Tonight, for the first time, it is dark.", "help"),
        line("help", "(Walk with the arrow keys or W A S D. Press E to talk, and to read.)", "maren"),
        line("maren", "Keeper Maren will know what to do. Her house is just along the road."),
    ])


def maren():
    m = "maren"
    lantern_active = quest_is("lantern.lquest", "active")
    return dialogue([m], [
        branch("begin", [(var_is("lantern_lit", True), "after"),
                         (all_of(lantern_active, var_is("flames", 3, ">=")), "ready"),
                         (var_is("met_maren", True), "waiting")], "greet"),
        line("greet", "Pip! There you are. Did you see? The storm blew out the great lantern.", "scattered", m),
        line("scattered", "Its three flames were scattered on the wind. Without them the night-mist will roll in, "
                          "and nothing grows in the mist.", "ask", m),
        line("ask", "You're my apprentice. Will you find the lost flames and bring them home?", speaker=m, choices=[
            choice("yes", "I'll find them, Keeper.", "accepted", [set_var("met_maren", True)]),
            choice("where", "Where did they go?", "where"),
        ]),
        line("where", "I saw one fall into Whisperwood, east of the village. Two more flew on, toward the old "
                      "mine.", "ask", m),
        line("accepted", "Bless you. Whisperwood is along the east road. The old mine is locked, mind: Wren the "
                         "baker keeps its key.", "tip", m),
        line("tip", "When you have all three, set them in the lantern up in the square.", None, m),
        line("waiting", "{flames} of 3 flames so far. One fell in Whisperwood, two near the old mine. Wren keeps "
                        "the mine key.", None, m),
        line("ready", "You found all three! Quickly, Pip: up to the lantern in the square.", None, m),
        line("after", "Look at it burn. You're a true lantern keeper now, Pip.", None, m),
    ])


def wren():
    w = "wren"
    return dialogue([w], [
        branch("begin", [(quest_is("biscuit.lquest", "completed"), "after"),
                         (var_is("cat_found", True), "found"),
                         (var_is("met_wren", True), "waiting")], "greet"),
        line("greet", "Oh, Pip... Have you seen Biscuit? My cat bolted when the lantern went out.", "woods", w,
             actions=[set_var("met_wren", True)]),
        line("woods", "She ran off into Whisperwood. She's never been out alone at night.", speaker=w, choices=[
            choice("look", "I'll look for her.", "thanks"),
            choice("flames", "I'm looking for the lost flames.", "key"),
        ]),
        line("key", "The flames? Then you'll be wanting the old mine, and I have its key. Bring Biscuit home and "
                    "it's yours.", "thanks", w),
        line("thanks", "Thank you! She loves sunny clearings. Try the north of the wood.", None, w),
        line("waiting", "Any sign of Biscuit? She loves sunny clearings, north in Whisperwood.", None, w),
        line("found", "Biscuit came running home! You found her!", "gift", w),
        line("gift", "Here, take this: the key to the old mine. It was my father's.", "got", w,
             actions=[{"type": "completeObjective", "quest": IDS["biscuit.lquest"], "objective": "tell"}]),
        line("got", "(You got the Old Mine Key.)"),
        line("after", "Biscuit hasn't left the warm oven all day. Thank you, Pip.", None, w),
    ])


def nell():
    n = "nell"
    return dialogue([n], [
        branch("begin", [(var_is("lantern_lit", True), "lit"), (var_is("has_key", True), "mine"),
                         (var_is("cat_found", True), "cat")], "hello"),
        line("hello", "Is the lantern going to stay dark forever? Mum says the mist comes when it's dark.", "biscuit", n),
        line("biscuit", "And Biscuit ran into the woods! I saw her go past the well.", None, n),
        line("cat", "You found Biscuit! I knew you would.", None, n),
        line("mine", "You've got the mine key! Be careful, the tunnels go round and round.", None, n),
        line("lit", "The lantern's back! I can see my shadow again!", None, n),
    ])


def biscuit_found():
    return dialogue(["biscuit"], [
        line("see", "A ginger cat is curled up in the sunny grass. It's Biscuit!", "mrrp"),
        line("mrrp", "Mrrp?", "off", "biscuit"),
        line("off", "Biscuit stretches, mews at you, and darts off toward the village.",
             actions=[set_var("cat_found", True)]),
    ])


def biscuit_home():
    return dialogue(["biscuit"], [line("purr", "Prrrrrr.", None, "biscuit")])


def flame_found():
    return dialogue([], [
        branch("begin", [(var_is("flames", 3, ">="), "all")], "one"),
        line("one", "You found a lost flame! It's warm, like holding a summer afternoon. ({flames}/3)"),
        line("all", "That's the third flame! ({flames}/3) Time to bring them home to the great lantern."),
    ])


def lantern_talk():
    return dialogue([], [
        branch("begin", [(var_is("lantern_lit", True), "burning"), (var_is("flames", 3, ">="), "light")], "dark"),
        line("dark", "The great lantern is cold and dark. It needs its three flames. ({flames}/3)"),
        line("light", "You set the three flames inside the great lantern...", "whoosh"),
        line("whoosh", "For a moment, nothing. Then, with a soft whoosh, warm light spills across Emberwick.", "mist",
             actions=[set_var("lantern_lit", True)]),
        line("mist", "The night-mist melts away. The great lantern burns again."),
        line("burning", "The great lantern burns bright and steady."),
    ])


def gate_talk():
    return dialogue([], [
        branch("begin", [(var_is("has_key", True), "open")], "locked"),
        line("locked", "An iron gate, locked tight. A rusty sign reads: OLD MINE. KEEP OUT.", "who"),
        line("who", "Maren said Wren the baker keeps the key to the old mine."),
        line("open", "You turn Wren's old key in the lock. The gate creaks open.",
             actions=[{"type": "emit", "event": "gate_open"}]),
    ])


def shrine_talk():
    return dialogue([], [
        line("rest", "The wayshrine's rune glows softly. You rest a moment. Your journey is saved.", "how"),
        line("how", "(Press F9 at any time to come back to this moment. F5 saves too.)"),
    ])


def village_sign():
    return dialogue([], [line("read", "East: Whisperwood.   North: the lantern square.")])


def mine_sign():
    return dialogue([], [line("read", "Miners' rule: follow the rails, and they'll bring you home.")])


# --- Animations ------------------------------------------------------------------------------------------

def clip(clip_id, frames, fps, loop=True):
    return {"id": clip_id, "frames": frames, "fps": fps, "loop": loop}


def animations(image, frame, clips):
    return {"format": "rolo.animations", "version": 1, "image": IDS[image], "frameSize": list(frame),
            "margin": 0, "spacing": 0, "clips": clips}


def hero_clips():
    clips = []
    for row, facing in enumerate(["down", "left", "right", "up"]):
        first = row * 3
        clips.append(clip(f"idle_{facing}", [first], 1))
        clips.append(clip(f"walk_{facing}", [first + 1, first, first + 2, first], 8))
    return clips


ANIMATIONS = {
    "hero.lanim": animations("hero_walk.png", (16, 24), hero_clips()),
    "maren.lanim": animations("maren.png", (16, 24), [clip("idle", [0] * 14 + [1], 6)]),
    "wren.lanim": animations("wren.png", (16, 24), [clip("idle", [0] * 11 + [1], 6)]),
    "nell.lanim": animations("nell.png", (16, 24), [clip("idle", [0, 0, 0, 1, 0, 1], 4)]),
    "biscuit.lanim": animations("biscuit.png", (16, 16), [clip("idle", [0, 0, 1, 1, 0, 1], 3)]),
    "flame.lanim": animations("flame.png", (16, 16), [clip("flicker", [0, 1, 2, 3], 8)]),
    "lantern.lanim": animations("lantern.png", (32, 64), [clip("dark", [0], 1), clip("lit", [1, 2, 1, 1, 2], 6)]),
    "shrine.lanim": animations("shrine.png", (16, 24), [clip("idle", [0, 0, 1, 1], 2)]),
}

# --- Writing -----------------------------------------------------------------------------------------------

def to_json(value, indent=0):
    """JSON like the editor writes scenes: two-space indents, lists of plain
    values on one line, whole numbers without ".0"."""
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
        items = [pad + "  " + to_json(v, indent + 1) for v in value]
        return "[\n" + ",\n".join(items) + "\n" + pad + "]"
    return json.dumps(value, ensure_ascii=False)


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(to_json(data) + "\n")


def meta(name, kind, extra=None):
    m = {"format": "rolo.asset", "version": 1, "id": IDS[name], "type": kind}
    m.update(extra or {})
    return m


def main():
    root = Path(sys.argv[1])
    project_file = root / "project.rolo"
    if not project_file.exists():
        sys.exit(f"{root} is not a Rolo project. Make one first: rolo_editor --new-project \"{root}\"")
    project = json.loads(project_file.read_text())

    # Art, imported into assets/ (pixel art keeps its hard edges).
    art = root / "assets" / "art"
    subprocess.run([sys.executable, str(HERE / "make_art.py"), str(art)], check=True)
    for png in sorted(art.glob("*.png")):
        write(png.with_name(png.name + ".meta"), meta(png.name, "image", {"filter": "pixel"}))
    write(root / "assets/tiles.ltileset", {"format": "rolo.tileset", "version": 1, "image": IDS["tiles.png"],
                                           "tileSize": [PIXEL, PIXEL], "margin": 0, "spacing": 0,
                                           "solid": SOLID_TILES})
    write(root / "assets/tiles.ltileset.meta", meta("tiles.ltileset", "tileset"))
    # Sounds and music (made by make_sounds.py).
    sounds = root / "assets" / "sounds"
    subprocess.run([sys.executable, str(HERE / "make_sounds.py"), str(sounds)], check=True)
    for file in sorted(sounds.iterdir()):
        write(file.with_name(file.name + ".meta"), meta(file.name, "audio"))
    for name, data in ANIMATIONS.items():
        write(root / "assets/animations" / name, data)
        write(root / "assets/animations" / (name + ".meta"), meta(name, "animation"))

    # Story content.
    content = root / "content"
    write(content / "keeper.lvars", variables())
    write(content / "keeper.lvars.meta", meta("keeper.lvars", "variables"))
    for name, data in {"lantern.lquest": lantern_quest(), "biscuit.lquest": biscuit_quest()}.items():
        write(content / name, data)
        write(content / (name + ".meta"), meta(name, "quest"))
    dialogues = {"intro": intro, "maren": maren, "wren": wren, "nell": nell, "biscuit_found": biscuit_found,
                 "biscuit_home": biscuit_home, "flame": flame_found, "lantern": lantern_talk, "gate": gate_talk,
                 "shrine": shrine_talk, "village_sign": village_sign, "mine_sign": mine_sign}
    for stem, make in dialogues.items():
        name = stem + ".ldialogue"
        write(content / "dialogue" / name, make())
        write(content / "dialogue" / (name + ".meta"), meta(name, "dialogue"))

    # Scenes. The fresh project's empty start scene is replaced by the village.
    for old in ("scenes/main.lscene", "scenes/main.lscene.meta"):
        (root / old).unlink(missing_ok=True)
    for name, make in {"village.lscene": village, "woods.lscene": woods, "mine.lscene": mine}.items():
        write(root / "scenes" / name, make().scene_json())
        write(root / "scenes" / (name + ".meta"), meta(name, "scene"))
    project["name"] = "The Lantern Keeper"
    project["startScene"] = "scenes/village.lscene"
    write(project_file, project)


if __name__ == "__main__":
    main()
