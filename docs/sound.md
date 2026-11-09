# Sound and music

Rolo plays sound effects and music in the editor's play mode and in exported
games. You can add sound three ways, from no code at all to full control:

| Way | Good for |
|---|---|
| A **Sound** component on an object | music for a scene, a waterfall's loop, a sound when something appears |
| The **playSound**, **playMusic** and **stopMusic** actions | a sound when a trigger fires, a dialogue choice is made or a quest completes |
| **Luau scripts** (`audio.play`, `object:playSound`) and **C++** (`game.playSound`) | anything else: footsteps, varying pitch, fading by distance |

## Sound files

Import **WAV**, **OGG** or **MP3** files like any other asset: drag them
onto the editor window, or use the Assets panel's **+**. Select one to see it
in the Inspector, and press **Play** there to listen to it.

- **WAV** for short effects: no decoding, exact timing.
- **OGG** for music and long sounds: small files, and it loops without a gap.
- **MP3** works too, but MP3 files have a little silence at their start, so
  they do not loop seamlessly.

Effects are read and decoded once, when the scene that uses them starts,
and kept in memory. Music is streamed: read a little at a time while it plays.

## The Sound component

Add it from the Inspector (**Add Component › Audio › Sound**), or drag a
sound file into the Scene view to make an object that has one.

| Property | Meaning |
|---|---|
| Sound | the sound file |
| Volume | 0 is silent, 1 is as recorded (up to 2) |
| Pitch | 1 as recorded; 2 is an octave higher and twice as fast, 0.5 an octave lower |
| Loop | play again from the start, until the object is removed or switched off |
| Play On Start | play when the object appears; off: only when an action, a script or code plays it |
| Music | play as the music (below) |
| Music Fade | music: seconds to fade in, while the music before it fades out |

When the object appears (the scene starts, or the object is switched on),
the sound plays. A looping sound stops when its object is removed or
switched off, and starts again if it comes back. A short sound plays to its
end even if its object is gone: a collected coin's jingle is not cut off.

### Music

Music is one piece at a time. A Sound with **Music** on plays as the music:
the piece playing before fades out while the new one fades in. If the new
scene's music is the same file, it simply carries on, so walking between
two scenes with the same music does not restart it. A scene without a music
Sound leaves the music playing; use the `stopMusic` action to stop it.

## Actions: sounds without code

Triggers, dialogue choices and quest rewards can use three actions (see
the [content format](formats/content.md)):

```
playSound door_creak         # play a sound effect
playMusic boss_theme         # change the music (fading over one second)
stopMusic                    # fade the music out
```

A sound is named like other assets: by its file name without the extension
(`door_creak`), or by its path (`"sounds/door_creak.wav"`) when two files
share a name. **Check Content** reports actions whose sound file is missing.

## Scripts

```lua
function update(self, dt)
    if input.wasPressed("Space") then
        audio.play("jump", { volume = 0.8, pitch = 0.9 + math.random() * 0.2 })
    end
end
```

The whole `audio` table, and the objects' `playSound` and `stopSound`, are in
the [scripting guide](scripting.md#audio).

## C++

```cpp
void Chest::update(rolo::Game& game, float dt)
{
    if (opened) {
        game.playSound(entity());                 // this object's Sound component
        game.playSound(creakSound, {.pitch = 0.8f}); // any sound asset
        game.playMusic(treasureTheme, 2.0f);         // fade over 2 seconds
    }
}
```

`game.sound()` gives the rest: stopping an effect, the volumes, the music
playing now. See [gameplay](gameplay.md#sound) and `rolo/gameplay/Sound.hpp`.

## Volumes

Three volume channels multiply together: **master**, **music** and
**effects**, each from 0 to 1 (a script sets them with
`audio.setVolume("music", 0.5)`). A sound's own volume is multiplied by its
channel's and by master.

## In the editor

- Play mode plays the game's sounds; **Pause** holds them, **Stop** stops them.
- Sound files are read again each time you press Play, so an edited file is
  heard as it is now.
- **Game › Mute Sound** silences the editor (remembered between sessions).

## On computers without sound

A computer without a sound output (a server, some CI machines) runs games
silently: the log says "No sound output found", and everything else works the
same. `rolo_player --mute` plays a game without sound on purpose.

## Behind the scenes

Rolo plays sound with [miniaudio](https://github.com/mackron/miniaudio)
(public domain), and reads OGG files with stb_vorbis (ADR 0015). Both are
built into Rolo; nothing needs installing, on any computer.
