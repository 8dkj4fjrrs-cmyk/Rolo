# ADR 0017: Gamepads through GLFW

- **Status:** Accepted
- **Date:** 2026-11-09

## Context

Rolo games were keyboard (and mouse) only. Platformers and action games
want gamepads. The project owner's rule for dependencies: MIT licences only;
if there is none, write Rolo's own, and say so.

Reading gamepads well is mostly about knowing each controller's layout: the
same "bottom face button" is a different raw button number on an Xbox, a
PlayStation, a Switch Pro and every third-party pad, on each operating
system. The standard answer is SDL's GameControllerDB, a community list of
layouts for thousands of controllers (zlib licence).

Options:
- **SDL** (zlib): a second windowing and input library next to Axmol's.
- **Our own**: XInput and DirectInput on Windows, IOKit on macOS, evdev on
  Linux, plus a layout database. Large, and the database would be SDL's
  anyway (or a much smaller one of our own).
- **GLFW's gamepad API**: GLFW 3.4 is already compiled into Rolo (Axmol's
  window layer) and ships in every game since 0.8; its gamepad support
  (`glfwGetGamepadState`) uses SDL_GameControllerDB's layouts, built in.

GLFW and the layout list are zlib, not MIT. Neither is new: both have been
inside every Rolo game since the first release, and the zlib licence is as
permissive as MIT (it does not even ask for a notice in programs).

## Decision

Read gamepads with GLFW. Nothing new is added to Rolo; the project owner is
told that this uses GLFW's zlib code rather than MIT code.

- `rolo::Input` (`engine/gameplay`, no backend) gains gamepads: up to four,
  `GamepadButton` named by position on an Xbox pad (A bottom, B right, X
  left, Y top, bumpers, Back, Start, Guide, stick clicks, d-pad) and
  `GamepadAxis` (sticks, y up; triggers 0 to 1). Without a pad number a
  question means "any gamepad". Sticks get a radial dead zone of 0.2,
  rescaled so slow walking is possible, and are never longer than 1.
  `wasNudged` turns a stick push into one menu step.
- `moveDirection()` uses the keys, else the d-pad, else the left stick, so
  the Top-Down Controller, game modules and `input.direction()` work with
  gamepads unchanged. Dialogue goes on with A and picks choices with the
  d-pad or stick; interaction triggers fire on A, and their prompt says
  "[A]" while a gamepad is plugged in.
- `rolo::GamepadReader` (`engine/runtime`) reads GLFW once a frame into an
  Input: pads keep their number while plugged in, plugging in and out is
  logged, and controllers without a known layout are mentioned once and
  ignored. The player reads them while the game runs, the editor while it
  plays (whatever has keyboard focus).
- Lua: `"pad a"`-style names in `input.isDown`/`wasPressed`/`wasReleased`
  (with an optional pad number 1–4), `input.stick`, `input.trigger`,
  `input.gamepad`.
- `licenses/glfw.txt` now includes SDL_GameControllerDB's notice too.

## Consequences

- Games play with most gamepads on macOS, Windows and Linux, with nothing to
  install.
- No rumble (GLFW has none), no player remapping, no button pictures.
- Tests cover Input, the dialogue and Lua with simulated pads, and the GLFW
  conversion; reading a real pad needs a person with a controller (CI has
  none).
