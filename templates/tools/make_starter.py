"""Builds the Top-Down Starter template (templates/topdown/project).

    rolo_editor --new-project templates/topdown/project
    python3 templates/tools/make_starter.py templates/topdown/project

A small game made only of Rolo's built-in components (no C++): a meadow
and a grove joined by a path, a hero who walks in four directions (Top-Down
Controller), a camera that stays on the map (Camera Bounds), doors that
arrive at named spots (Scene Door), a friend and a signpost to talk to, and
the on-screen dialogue box, prompt and quest log. Fireflies in the grove run
a Lua script (scripts/firefly.lua), the example of scripting. The art is The
Lantern Keeper's (original, MIT). Fixed IDs keep the output the same every run.
"""
import json
import random
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "examples" / "lantern_keeper" / "project" / "assets" / "art"

_NAMES = ["tiles.png", "hero_walk.png", "wren.png", "sign.png", "tiles.ltileset", "hero.lanim", "friend.lanim",
          "meadow.lscene", "grove.lscene", "starter.lvars", "friend.ldialogue", "sign.ldialogue",
          "hello.lquest", "firefly.lua"]
IDS = {n: f"5354415254{i + 1:06x}" for i, n in enumerate(_NAMES)}     # "START..."
TILE = {".": 0, ",": 1, "=": 2, "~": 3, "#": 4, "T": 5, "b": 11, '"': 23}
SOLID = [3, 4, 5, 6, 7, 8, 9, 11, 13, 15, 17, 18, 20, 22, 23]
PIXEL, SCALE = 16, 3
CELL = PIXEL * SCALE
VIEW = (1280, 720)


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


FIREFLY = """-- firefly.lua: a firefly that drifts in loops around where it starts.
-- This is a Lua script. Every firefly in the grove has a Script component
-- that runs this file. Change the numbers, save, and press Play again.
-- Help > Scripting Guide lists everything a script can use.

-- start runs once for each firefly. self is that firefly's own table:
-- self.object is the firefly, and anything else you put in self stays there.
function start(self)
    self.homeX, self.homeY = self.object:position()
    self.time = math.random() * 10   -- so the fireflies are not all in step
end

-- update runs every frame. dt is the time since the last frame, in seconds.
function update(self, dt)
    self.time = self.time + dt
    local x = self.homeX + math.cos(self.time * 1.3) * 40
    local y = self.homeY + math.sin(self.time * 2.1) * 20
    self.object:setPosition(x, y)
end
"""


def meta(name, kind, extra=None):
    m = {"format": "rolo.asset", "version": 1, "id": IDS[name], "type": kind}
    m.update(extra or {})
    return m


def transform(x=0.0, y=0.0, scale=(1, 1)):
    return {"type": "rolo.Transform", "position": [x, y], "rotation": 0, "scale": list(scale)}


def collider(size, offset=(0, 0), solid=True):
    return {"type": "rolo.Collider", "size": list(size), "offset": list(offset), "solid": solid}


def text(words, size=18, colour="#ffffffff", align="left", width=0, source="text"):
    return {"type": "rolo.Text", "text": words, "source": source, "font": None, "size": size, "color": colour,
            "align": align, "width": width}


def trigger(on, actions, prompt="", once=False):
    return {"type": "rolo.Trigger", "on": on, "prompt": prompt, "event": "", "delay": 1.0, "once": once,
            "data": {"condition": None, "actions": actions}}


class Scene:
    def __init__(self, name, prefix, rows):
        self.name, self.prefix, self.n, self.entities, self.rows = name, prefix, 0, [], rows
        self.w, self.h = len(rows[0]), len(rows)

    def at(self, col, row):
        return [(col + 0.5) * CELL, (self.h - 1 - row + 0.5) * CELL]

    def add(self, name, components, parent=None, active=True):
        self.n += 1
        e = {"id": f"{self.prefix}{self.n:010x}"}
        if parent:
            e["parent"] = parent
        e.update({"name": name, "active": active, "components": components})
        self.entities.append(e)
        return e["id"]

    def json(self):
        return {"format": "rolo.scene", "version": 1, "name": self.name, "entities": self.entities}


def grid(w, h, seed):
    rnd = random.Random(seed)
    g = [["," if rnd.random() < 0.07 else "." for _ in range(w)] for _ in range(h)]
    for r in range(h):
        for c in range(w):
            if r == 0 or c == 0 or r == h - 1 or c == w - 1:
                g[r][c] = "T"
    return g


def build_scene(name, prefix, g, start, door, spot, extras):
    s = Scene(name, prefix, ["".join(r) for r in g])
    rows = [[s.h - 1 - i, 0] + [TILE[c] for c in line] for i, line in enumerate(s.rows)]
    s.add("Ground", [transform(scale=(SCALE, SCALE)), {
        "type": "rolo.Tilemap", "tileset": IDS["tiles.ltileset"], "cellSize": [PIXEL, PIXEL],
        "data": {"layers": [{"name": "Ground", "visible": True, "rows": rows}], "solid": []}}])
    extras(s)
    # The way to the other scene, and where you arrive coming back from it.
    (dname, dcol, drow, target, arrive) = door
    x, y = s.at(dcol, drow)
    s.add(dname, [transform(x, y), collider((CELL, CELL * 2), solid=False),
                  {"type": "rolo.SceneDoor", "scene": IDS[target], "arriveAt": arrive}])
    (sname, scol, srow) = spot
    x, y = s.at(scol, srow)
    s.add(sname, [transform(x, y)])
    # The player, the camera and the interface.
    x, y = s.at(*start)
    hero = s.add("Player", [
        transform(x, y, (SCALE, SCALE)),
        {"type": "rolo.Sprite", "image": IDS["hero_walk.png"], "size": [16, 24], "color": "#ffffffff",
         "flipX": False, "flipY": False},
        {"type": "rolo.Animator", "animations": IDS["hero.lanim"], "clip": "idle_down", "speed": 1, "playing": True},
        collider((10, 5), (0, -9.5)),
        {"type": "rolo.Interactor", "key": "E"},
        {"type": "rolo.TopDownController", "speed": 170},
    ])
    cam = s.add("Camera", [transform(scale=(1 / SCALE, 1 / SCALE)), {"type": "rolo.Camera", "zoom": 1},
                           {"type": "rolo.CameraBounds", "min": [0, 0], "max": [s.w * CELL, s.h * CELL],
                            "view": list(VIEW)}], parent=hero)
    left, top = -VIEW[0] / 2 + 24, VIEW[1] / 2 - 20
    s.add("Quest Log", [transform(left, top), text("", 16, "#ecececff", source="questLog")], parent=cam)
    s.add("Help", [transform(-left, top), text("Arrows / WASD walk · E talk · F5 save · F9 load", 14, "#ffffffb0",
                                                align="right")], parent=cam)
    s.add("Prompt", [transform(0, -120), text("", 20, "#ffc84aff", align="center", source="interactionPrompt")],
          parent=cam)
    box = s.add("Dialogue Box", [transform(0, -VIEW[1] / 2 + 100),
                                 {"type": "rolo.Shape", "kind": "rectangle", "size": [1040, 168],
                                  "color": "#120e1aee", "filled": True, "lineWidth": 2},
                                 {"type": "rolo.DialogueView", "marker": "> "}], parent=cam, active=False)
    s.add("Speaker", [transform(-496, 68), text("", 20, source="dialogueSpeaker")], parent=box)
    s.add("Line", [transform(-496, 38), text("", 19, width=990, source="dialogueLine")], parent=box)
    s.add("Choices", [transform(-496, -14), text("", 19, "#ffd88aff", source="dialogueChoices")], parent=box)
    s.add("Continue", [transform(500, -62), text("E / Space", 12, "#ffffff80", align="right")], parent=box)
    return s


def talkable(s, name, col, row, image, size, lanim, clip, dialogue, prompt):
    x, y = s.at(col, row)
    comps = [transform(x, y, (SCALE, SCALE)),
             {"type": "rolo.Sprite", "image": IDS[image], "size": list(size), "color": "#ffffffff",
              "flipX": False, "flipY": False}]
    if lanim:
        comps.append({"type": "rolo.Animator", "animations": IDS[lanim], "clip": clip, "speed": 1, "playing": True})
    comps.append(collider((10, 4), (0, -size[1] / 2 + 3)))
    body = s.add(name, comps)
    s.add("Talk Area", [transform(0, -size[1] / 2 + 4), collider((24, 20), solid=False),
                        trigger("interact", [{"type": "startDialogue", "dialogue": IDS[dialogue]}], prompt=prompt)],
          parent=body)


def meadow():
    g = grid(30, 18, 1)
    for c in range(1, 30):
        for r in (8, 9):
            g[r][c] = "="
    for r in range(11, 16):
        for c in range(3, 10):
            if ((c - 6) / 3.3) ** 2 + ((r - 13) / 2.2) ** 2 <= 1:
                g[r][c] = "~"
    for (c, r) in [(14, 3), (22, 13), (25, 4), (12, 14)]:
        g[r][c] = "b"

    def extras(s):
        talkable(s, "Signpost", 9, 6.5, "sign.png", (16, 16), None, None, "sign.ldialogue", "Read")
        talkable(s, "Friend", 18, 6, "wren.png", (16, 24), "friend.lanim", "idle", "friend.ldialogue", "Talk")

    return build_scene("Meadow", "535401", g, (4, 8.5), ("Path to the Grove", 29.5, 8.5, "grove.lscene", "From Meadow"),
                       ("From Grove", 27, 8.5), extras)


def grove():
    g = grid(24, 16, 2)
    for c in range(0, 12):
        for r in (7, 8):
            g[r][c] = "="
    for r in range(3, 13):
        for c in range(12, 22):
            if ((c - 16.5) / 4.5) ** 2 + ((r - 7.5) / 4) ** 2 <= 1:
                g[r][c] = ","

    def extras(s):
        # Fireflies over the clearing, each moved by scripts/firefly.lua.
        for i, (col, row) in enumerate([(15, 6), (18, 8.5), (16.5, 10)]):
            x, y = s.at(col, row)
            fly = s.add(f"Firefly {i + 1}", [
                transform(x, y),
                {"type": "rolo.Shape", "kind": "ellipse", "size": [12, 12], "color": "#fff2a6ff", "filled": True,
                 "lineWidth": 2},
                {"type": "rolo.Script", "script": IDS["firefly.lua"]}])
            s.add("Glow", [transform(), {"type": "rolo.Shape", "kind": "ellipse", "size": [34, 34],
                                         "color": "#ffe68055", "filled": True, "lineWidth": 2}], parent=fly)

    return build_scene("Grove", "535402", g, (3, 7.5), ("Path to the Meadow", -0.5, 7.5, "meadow.lscene", "From Grove"),
                       ("From Meadow", 2.5, 7.5), extras)


def dialogue(speakers, nodes):
    return {"format": "rolo.dialogue", "version": 1, "speakers": speakers, "start": nodes[0]["id"], "nodes": nodes}


def main():
    root = Path(sys.argv[1])
    project_file = root / "project.rolo"
    if not project_file.exists():
        sys.exit(f"{root} is not a Rolo project. Make one first: rolo_editor --new-project \"{root}\"")
    project = json.loads(project_file.read_text())
    art = root / "assets" / "art"
    art.mkdir(parents=True, exist_ok=True)
    for name in ("tiles.png", "hero_walk.png", "wren.png", "sign.png"):
        shutil.copyfile(ART / name, art / name)
        write(art / (name + ".meta"), meta(name, "image", {"filter": "pixel"}))
    write(root / "assets/tiles.ltileset", {"format": "rolo.tileset", "version": 1, "image": IDS["tiles.png"],
                                           "tileSize": [16, 16], "margin": 0, "spacing": 0, "solid": SOLID})
    write(root / "assets/tiles.ltileset.meta", meta("tiles.ltileset", "tileset"))
    hero_clips = []
    for row, facing in enumerate(["down", "left", "right", "up"]):
        first = row * 3
        hero_clips += [{"id": f"idle_{facing}", "frames": [first], "fps": 1, "loop": True},
                       {"id": f"walk_{facing}", "frames": [first + 1, first, first + 2, first], "fps": 8, "loop": True}]
    for name, image, frame, clips in [
            ("hero.lanim", "hero_walk.png", (16, 24), hero_clips),
            ("friend.lanim", "wren.png", (16, 24), [{"id": "idle", "frames": [0] * 11 + [1], "fps": 6, "loop": True}])]:
        write(root / "assets/animations" / name, {"format": "rolo.animations", "version": 1, "image": IDS[image],
                                                  "frameSize": list(frame), "margin": 0, "spacing": 0, "clips": clips})
        write(root / "assets/animations" / (name + ".meta"), meta(name, "animation"))

    content = root / "content"
    write(content / "starter.lvars", {"format": "rolo.variables", "version": 1, "variables": [
        {"id": "said_hello", "type": "bool", "initial": False, "description": "Talked to the friend"}]})
    write(content / "starter.lvars.meta", meta("starter.lvars", "variables"))
    write(content / "hello.lquest", {
        "format": "rolo.quest", "version": 1, "name": "Say Hello",
        "description": "A first quest: it starts by itself and is done when you talk to your friend.",
        "prerequisites": [], "startWhen": {"type": "always"},
        "objectives": [{"id": "talk", "text": "Talk to your friend in the meadow",
                        "completeWhen": {"type": "compare", "variable": "said_hello", "op": "==", "value": True},
                        "optional": False}],
        "failWhen": None, "rewards": [{"type": "log", "text": "Quest done: Say Hello"}]})
    write(content / "hello.lquest.meta", meta("hello.lquest", "quest"))
    friend = {"id": "friend", "name": "Friend", "color": "#9ee0a8ff", "portrait": None}
    write(content / "friend.ldialogue", dialogue([friend], [
        {"id": "hello", "speaker": "friend", "text": "Hello! This is your game. Everything here can be changed in the "
                                                     "editor.", "actions": [
            {"type": "set", "variable": "said_hello", "value": True}], "next": "ask"},
        {"id": "ask", "speaker": "friend", "text": "What would you like to know?", "choices": [
            {"id": "move", "text": "How do I walk?", "next": "move"},
            {"id": "make", "text": "How do I make it my own?", "next": "make"},
            {"id": "code", "text": "Can I write code?", "next": "code"},
            {"id": "bye", "text": "Bye!", "next": None}]},
        {"id": "move", "speaker": "friend", "text": "The Top-Down Controller on the Player walks with the arrow keys "
                                                    "or W A S D, and plays the walk_ clips of its Animator.",
         "next": "ask"},
        {"id": "make", "speaker": "friend", "text": "Paint the Ground tilemap, import your own art, write dialogues "
                                                    "in the Game menu, and press the Build button to export.",
         "next": "ask"},
        {"id": "code", "speaker": "friend", "text": "Yes, in Lua! The fireflies in the grove run scripts/firefly.lua. "
                                                    "Select it in the Assets panel and open it in your code editor.",
         "next": "ask"},
    ]))
    write(content / "friend.ldialogue.meta", meta("friend.ldialogue", "dialogue"))
    write(content / "sign.ldialogue", dialogue([], [
        {"id": "read", "text": "East: the grove. (Walk into the path at the edge of the map.)", "next": None}]))
    write(content / "sign.ldialogue.meta", meta("sign.ldialogue", "dialogue"))

    scripts = root / "scripts"
    scripts.mkdir(parents=True, exist_ok=True)
    (scripts / "firefly.lua").write_text(FIREFLY, encoding="utf-8")
    write(scripts / "firefly.lua.meta", meta("firefly.lua", "script"))

    for old in ("scenes/main.lscene", "scenes/main.lscene.meta"):
        (root / old).unlink(missing_ok=True)
    for name, make in {"meadow.lscene": meadow, "grove.lscene": grove}.items():
        write(root / "scenes" / name, make().json())
        write(root / "scenes" / (name + ".meta"), meta(name, "scene"))
    project["name"] = "Top-Down Starter"   # the wizard renames copies after the new project
    project["startScene"] = "scenes/meadow.lscene"
    project["screenFit"] = "expand"        # fills screens of any shape (phones); the HUD follows the edges
    write(project_file, project)


if __name__ == "__main__":
    main()
