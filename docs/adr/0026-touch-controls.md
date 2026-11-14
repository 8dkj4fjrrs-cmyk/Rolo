# ADR 0026: Touch controls

- **Status:** Accepted
- **Date:** 2026-11-14
- **Follows:** ADR 0019 (direction: desktop, web and mobile), ADR 0018 (controls: actions), ADR 0022 (web export)

## Context

Web games open on phones and tablets, which have no keyboard: until now
they showed the game and could not play it. Android and iOS (next on the
roadmap) need the same thing. Rolo's games ask for **actions** ("jump",
"interact", the four move actions), not for keys (ADR 0018), which makes a
general answer possible: give every action something to press on the
screen.

## Decision

**On-screen controls, made by Rolo from the game's actions.**

- `rolo/gameplay/TouchControls.hpp` (logic only, tested without a
  window): a **stick** for the bottom-left part of the screen (its base
  comes to wherever the thumb lands) and a **round button for each
  action** the game defines, in the order they were defined: the first is
  the large one in the bottom-right corner, the others sit around it.
  Actions called "pause" or "menu" are small buttons at the top. At most
  six buttons.
- Pressing a button **is** pressing its action
  (`Input::setTouchAction`): `input.isDown("jump")`,
  `wasActionPressed`, Rolo's own movement, dialogue and triggers all work
  unchanged. The stick sets `moveDirection()` (with values in between,
  like a gamepad stick) and holds the move actions past half way, so
  menus that ask for "move down" work.
- **Nothing to set up.** A game made without touch in mind is playable on
  a phone. The maker can: turn them off (`"touchControls": false` in
  `project.rolo`, or **Touch Controls** in the Inspector's game section)
  and handle touches in scripts, or choose which actions get buttons
  (`"touchButtons": ["jump", "pause"]`).
- The player leaves out "interact" when the scene has nobody who can
  interact and no dialogue is open.
- **Fingers that are not on a control are the game's**:
  `Input::touches()` (`input.touches()` in scripts), in world
  coordinates. The first of them also moves the pointer and counts as the
  left mouse button, so games made for clicking work with taps.
- **Shown only while touch is in use**: `Input::lastDevice()` has a
  third value, Touch (`"touch"` in scripts). On a phone or tablet (a
  coarse pointer that cannot hover) the controls show from the start; on
  a laptop with a touch screen they appear at the first touch and go when
  a key or gamepad is used.
- **Prompts follow**: `{button:jump}` draws the on-screen button
  (a dark pill saying "Jump"), the move actions draw the stick.
- **The web reads touches itself** (`emscripten_set_touch*_callback` on
  the canvas, in `Application`): Axmol's web touch code ends every finger
  when one lifts, which breaks holding the stick while pressing a button.
  Returning "handled" also stops the page scrolling, zooming, or turning
  a touch into a mouse click.
- For trying it on a computer: `rolo_player --touch` makes the left mouse
  button a finger; tests put fingers on the screen with
  `--finger X,Y@FROM-TO`.

## Alternatives considered

- **Touch buttons as objects the maker places in each scene.** More
  control, but nothing works until the maker has done it in every scene,
  and beginners' games would stay unplayable on phones. It can be added
  later for makers who want their own look; the automatic controls stay
  the default.
- **Bind touch areas like keys** (a new kind of binding). Remapping a
  screen area makes no sense to a player, and saved bindings would carry
  screen positions.

## Consequences

- Web games are playable on phones and tablets. The page keeps fingers
  from scrolling or zooming it, and moves its Fullscreen button out of
  the buttons' way.
- The templates' pause menu takes the Jump button as "pick" on touch (it
  asked for Enter). Menus written with raw keys (`input.wasPressed("enter")`)
  need the same care; menus written with actions just work.
- Remapping screens show keys and gamepad buttons only: on-screen buttons
  cannot be remapped.
- One look for the controls (translucent dark discs, Rolo's orange when
  pressed); makers cannot restyle them yet.
- The controls are laid out in the game's resolution, so on a small phone
  they are as large, relative to the picture, as on a tablet. Their size
  under a real thumb has not been tried on a device: only in a desktop
  browser acting as a phone (touch events through Chromium's input
  pipeline).
- Not done: gestures (pinch, swipe) as such (scripts can build them from
  `input.touches()`), rumble on phones, and letting the game picture use
  the whole of a screen of another shape (screen sizes, next).
