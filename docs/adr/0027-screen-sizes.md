# ADR 0027: Screen sizes: a game size, and filling screens of other shapes

- **Status:** Accepted
- **Date:** 2026-11-14
- **Follows:** ADR 0019 (direction: desktop, web and mobile), ADR 0022 (web export), ADR 0026 (touch controls)

## Context

Every Rolo game was 1280 × 720, and a window or screen of another shape
showed it with bars beside it. Phones are much wider than 16:9 when held
sideways and much taller when held upright, so web games on phones wasted
a good part of the screen, and upright games could not be made at all.

## Decision

**A game size per project.** `project.rolo` has `width` and `height`
(1280 × 720 when left out; 160 to 4096). It is what the game is made
for: the frame in the editor's Scene view, the window's size, the size of
the web page's picture. Upright games swap them (720 × 1280).

**A choice of what other screen shapes show** (`screenFit`):

- `bars` (when left out): the whole game size, as large as fits, bars
  beside or above and below. Every player sees exactly the same.
- `expand`: the picture fills the screen. The game size is always
  visible; a wider screen shows more of the world to the sides, a taller
  one more above and below (at most three times the game size).

Both are in the Inspector's *Game (whole project)* section: **Game Size**
(a list of common sizes) and **Fill the Screen**.

**With `expand`, things fixed to the camera follow the screen's edges, by
themselves** (`rolo/scene/ScreenFit.hpp`, `pinToScreen`). Scores, prompts
and menus are children of the Camera object, placed in the game size. The
screen is taken as three columns and three rows of the game size: a child
in the left column keeps its distance from the left edge, one in the
right column from the right edge, one in the middle stays in the middle;
the same for top and bottom. A filled rectangle that covers the whole
game size along a side (a night veil, a fade to black) is stretched to
cover the whole screen. Nothing is set per object.

**Games can ask** how much is visible: `Game::viewSize()`,
`game.viewSize()` in scripts. Camera Bounds uses it, so a camera kept
inside a map stays inside on wide screens too.

**Where it is done**

- `Application` (game windows only): `fitScreen()` works out the visible
  size from the window's shape (`visibleSize`), sets it as Axmol's design
  resolution, centres the camera in it, lays the touch controls out in
  it, and tells the renderer, which pins the Camera's children. Run again
  whenever the window's size changes. The editor always shows the game at
  exactly its size.
- The web page lays the picture out (the game's shape with bars, or the
  whole window) and tells the game how many pixels to draw
  (`Module._rolo_set_window_size`): as many as the picture has on that
  screen, at most twice the page's own. So web games are now also
  **sharp on high-density screens**; before, they were always 1280 × 720
  pixels, scaled.
- The player's window is the game's size, made smaller (same shape) when
  larger than 1280 × 800.

## Alternatives considered

- **A Pin component on each HUD object** (which edge, how far). Exact,
  but every game's every HUD object would need one before `expand`
  worked, and beginners would not find out why their score is in the
  middle of a wide screen. The automatic rule covers what the templates
  and example games do; a component can be added later for the cases it
  gets wrong.
- **Stretching the picture** to the screen's shape. Never right for
  games: circles become ovals.
- **Cropping** (fill the screen, cut the game size's edges off). Hides
  scores and controls on some screens; `expand` shows more instead of less.

## Consequences

- The Platformer and Top-Down Starters use `expand`: on a phone they fill
  the screen. On a 1280 × 720 window nothing changes.
- Existing projects are unchanged (1280 × 720 with bars).
- With `expand`, **players see different amounts of the world**. A level
  must make sense with more visible than the game size (the camera bounds
  hold where the map is large enough; a map smaller than the screen is
  centred, with the background colour around it). Games where seeing
  further is an advantage, or whose scenes are exactly one screen with
  nothing around them, should keep `bars`.
- The pinning rule is a rule of thumb. A HUD object placed near the
  middle third stays there; one whose position is at its middle but whose
  text runs to an edge may sit oddly. Not pinned: objects that are not
  the Camera's own children (their children move with them), and scenes
  without a Camera.
- Changing the game size does not move anything: a HUD made for
  1280 × 720 is off-screen in a 720 × 1280 game until it is moved.
- The web page now draws up to four times as many pixels on sharp screens
  (twice each way): sharper, and more work for slow phones' graphics
  chips. Not measured on a real phone.
- Tested: unit tests for the sizes and the pinning rule; the player with a
  wide window and with an upright game; web games in headless Chromium at
  desktop size and acting as a phone (bars and fill). Not tried on a real
  phone or tablet, nor by resizing a real window by hand.
