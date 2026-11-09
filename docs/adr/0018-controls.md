# ADR 0018: Actions, remapping, button pictures and rumble

- **Status:** Accepted
- **Date:** 2026-11-09
- **Follows:** ADR 0017 (gamepads)

## Context

With gamepads in (ADR 0017), three things were missing for controls that
feel finished: players changing their keys and buttons, prompts showing a
picture of the right button, and rumble. The dependency rule: MIT only, or
Rolo's own code, and say so.

- **Remapping** needs a layer between "the Space key" and "jump": named
  actions with bindings that players can change and that are kept.
- **Button pictures**: Rolo's Text is drawn with Axmol labels, which cannot
  hold images; the pictures must also follow the device in use, the pad's
  family, and the remapping.
- **Rumble**: GLFW has none. No MIT library covers Windows, macOS and Linux
  (SDL is zlib and a second input layer). Each system has its own built-in
  API: XInput on Windows, the Game Controller framework on macOS, evdev
  force feedback on Linux.

## Decision

**Actions** live in `rolo::Input` (no backend): a name, default
`Binding`s (key, mouse button or pad button) and, if changed, the player's.
Rolo's own controls are actions (`move left/right/up/down`, `interact`,
whose default key is the Interactor's), so `moveDirection`, dialogue and
triggers follow remapping. `startRebind` takes the next press as the binding
for that kind of device (keyboard and mouse, or gamepad), using the press
up; Escape or Backspace cancel. The player's bindings are written as simple
lines (`jump=Space,Pad A`) to the save slot `_controls` when they change and
read when a game starts; save slots starting with `_` are reserved.

**Button pictures**: `{button:name}` in text (an action, or a key or
button) is resolved by `Game` each frame to an icon token
(`{icon:pad:A:playstation}`) for the device used last and the style of that
pad (`gamepadStyleFromName`: Xbox, PlayStation, Nintendo, generic). The
runtime's `SceneRenderer` splits lines with tokens into words and pictures
drawn with shapes and Rolo's font (`ButtonIconNodes.cpp`): no image files,
no console makers' artwork. The interaction prompt is
`{button:interact} Talk`; `Game::interactionPrompt()` keeps the readable
"[E] Talk" / "[Cross] Talk".

**Rumble**: games ask through `Input::rumble` (queued, no backend);
`GamepadReader` passes requests to `RumbleTimer`, which tells the system
when a pad's motors change and stops them on time, when paused, stopped or
unplugged. The system part is **Rolo's own code**, one file each:
- Windows: XInput, loaded at run time; GLFW's XInput joysticks are matched
  to XInput slots by their order;
- macOS: `GCController` haptics through Core Haptics (macOS 11+), matched by
  order; one strength (the stronger motor);
- Linux: evdev `FF_RUMBLE`, the device found by rebuilding GLFW's GUID from
  each `/dev/input/event*`.

Also fixed on the way: `obj:setText` from Lua was undone the next frame
(Game refilled every Text from the text written in the editor). Texts are
now changed through `Game::setText`, which changes what is refilled.

## Consequences

- Games without code are remappable and show the right button pictures.
- The rumble code is matched to pads by order on Windows and macOS, which
  is right unless pads are unplugged and plugged back in a different order.
- It was written and built on all three systems by CI, but **not tried with
  a real gamepad**: no test machine has one. Tests cover the timing
  (`RumbleTimer`) with a fake device.
- Lines with pictures are not wrapped.
