# ADR 0015: Sound with miniaudio

- **Status:** Accepted
- **Date:** 2026-11-09
- **Revisits:** the roadmap's plan to use Axmol's audio (ADR 0011's known limitations)

## Context

Rolo had no sound. Sound files could be imported, but nothing played them:
Axmol's audio was switched off (`AX_ENABLE_AUDIO OFF`). The roadmap planned
to switch it on, with its OpenAL Soft library linked dynamically.

Axmol's audio needs OpenAL Soft on Windows, Linux and (since Apple deprecated
its own OpenAL) macOS. OpenAL Soft is LGPL. An LGPL library inside a game
is allowed, but it brings duties that every game made with Rolo would
inherit: link it dynamically, let players replace it, ship its source or an
offer of it, and its license text. "Everything inside a game is MIT or
similarly permissive" is one of Rolo's promises (README, ADR 0012), and
these duties fall on beginners who did not choose them.

Other ways to play sound:
- **SDL_mixer** or **SoLoud**: permissive, but each is a large library or
  brings SDL, a second windowing and input layer next to Axmol's.
- **The system's own audio** (Core Audio, WASAPI, PulseAudio/ALSA) written
  by hand: three backends to write and keep working.
- **miniaudio**: one C file, public domain or MIT No Attribution, used
  widely (Raylib's audio is built on it). It talks to every desktop
  system's audio itself, loading the system libraries at run time, so
  nothing is linked or installed. It decodes WAV, MP3 and FLAC, and OGG with
  stb_vorbis (public domain or MIT). It has a mixer with groups, volume
  fades, pitch, panning, streaming, and a mode without a device that a
  program can pull mixed sound from.

## Decision

Play sound with **miniaudio 0.11.26** and **stb_vorbis 1.22**, kept in
`third_party/miniaudio/` (unmodified, like Lua). Axmol's audio stays off.
(0.11.25 was tried first: AddressSanitizer caught it reading freed memory
when a sound file could not be decoded, which 0.11.26 fixes. Rolo's test
"A sound that cannot be read is reported once" guards it.)

The parts:
- `rolo::SoundPlayer` (`engine/gameplay`, `Sound.hpp`): what a game asks of
  audio: play an effect (a handle back), stop it, music with fades, three
  volume channels (master, music, effects), pause, stop all. An interface,
  like `rolo::Input`: `engine/gameplay` keeps no backend, and its tests use
  a recording player. `SilentSoundPlayer` is the default: a program without
  audio runs games silently, unchanged.
- `rolo::AudioEngine` (`engine/audio`): the SoundPlayer of the player and
  the editor, on miniaudio. Effects are decoded once (when a scene that uses
  them starts) and shared by every play; music is streamed. It has no Axmol
  and no window, so it is built and tested in every preset: tests make one
  without a device and measure the samples it mixes.
- The **Sound** component (`rolo.Sound`): plays when its object appears,
  loops until the object goes, or is the scene's music. Music is one piece
  at a time: a new piece fades over the old, and the same piece carries on
  across scenes.
- Content actions `playSound`, `playMusic`, `stopMusic`, so triggers,
  dialogue choices and quests play sound without code. They are added to
  content format version 1 without bumping it: no existing file changes
  meaning; Rolo 0.9.1 reports them as unknown actions.
- Lua's `audio` table and `object:playSound()`/`stopSound()`; C++'s
  `game.playSound()`, `playMusic()` and `game.sound()`.
- The editor plays a game's sound in play mode (Pause holds it), previews
  sound files from the Inspector, and has **Game › Mute Sound**.
- `rolo_player --mute`.

## Consequences

- Games make sound on macOS, Windows and Linux with nothing to install, and
  their licenses stay permissive: `licenses/miniaudio.txt` and
  `licenses/stb-vorbis.txt` go with every game (neither license requires it).
- A computer without a sound output runs games silently. On Linux,
  miniaudio then uses its "null" device; ALSA may print messages of its own
  to the terminal, which do not affect the game.
- Streamed music reads ahead on a helper thread in step with the sound
  card. Without a device (tests), music is decoded whole instead, since
  mixing runs faster than real time.
- Sounds have no position: no 3D, distance or Doppler effects (miniaudio
  has them; switched off with `MA_SOUND_FLAG_NO_SPATIALIZATION`). Positional
  sound, a players' volume menu and gamepads are next on the roadmap.
- The Lantern Keeper has original music and effects, synthesised by
  `examples/lantern_keeper/tools/make_sounds.py`; its test plays them through
  a real AudioEngine without a device, so every sound file is decoded on
  every test run.
