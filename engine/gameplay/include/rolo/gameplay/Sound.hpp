#pragma once

// Sound: what a game asks of the program's audio, and the Sound component.
//
// Gameplay code never talks to a sound card. It asks a SoundPlayer to play a
// sound asset, and the program running the game provides one: the player and
// the editor use rolo::AudioEngine (engine/audio, built on miniaudio); tests
// use a player that only records what was asked. Like rolo::Input, this keeps
// engine/gameplay free of any backend, so it is tested without a sound device.
//
// Two kinds of sound:
//   * effects: any number at once (a coin, a footstep, a door). play() returns
//     a handle to stop one early or ask if it is still playing;
//   * music: one piece at a time. Starting another fades the old one out
//     while the new one fades in; asking for the piece already playing lets
//     it carry on, so two scenes with the same music do not restart it.
//
// Three volumes multiply together: master, music and effects. A sound's own
// volume (0 silent, 1 as recorded) is multiplied by its channel's and master's.

#include "rolo/scene/Component.hpp"
#include "rolo/scene/EntityId.hpp"

#include <cstdint>

namespace rolo {

/// A playing sound effect; 0 means none (e.g. the sound could not be played).
using SoundHandle = std::uint32_t;

/// How to play one sound.
struct SoundOptions {
    float volume = 1.0f;   // 0 (silent) to 1 (as recorded); up to 2 for quiet recordings
    float pitch = 1.0f;    // 1 as recorded, 2 an octave higher and twice as fast, 0.5 lower
    float pan = 0.0f;      // -1 left, 0 centre, 1 right
    bool loop = false;     // start again at the end, until stopped
};

/// The volume channels.
enum class SoundChannel { Master, Music, Effects };

/// What a game needs from the program's audio. Implementations log problems
/// (a missing or unreadable file) and never throw.
class SoundPlayer {
public:
    virtual ~SoundPlayer() = default;

    /// Plays a sound effect. Returns 0 if it cannot be played.
    virtual SoundHandle play(AssetId sound, const SoundOptions& options = {}) = 0;
    /// Stops one effect; nothing happens if it has already finished.
    virtual void stop(SoundHandle handle) = 0;
    /// True while the effect is playing (a looping one, until stopped).
    virtual bool isPlaying(SoundHandle handle) const = 0;
    /// Changes a playing effect's volume, e.g. to fade a waterfall.
    virtual void setVolume(SoundHandle handle, float volume) = 0;

    /// Plays music, fading over `fadeSeconds` from what was playing. If this
    /// music is already playing, it carries on (its volume is updated).
    virtual void playMusic(AssetId music, float fadeSeconds = 1.0f, float volume = 1.0f) = 0;
    /// Fades the music out and stops it.
    virtual void stopMusic(float fadeSeconds = 1.0f) = 0;
    /// The music playing now (null if none, or if it is fading out).
    virtual AssetId music() const = 0;

    /// A channel's volume, 0 to 1.
    virtual void setChannelVolume(SoundChannel channel, float volume) = 0;
    virtual float channelVolume(SoundChannel channel) const = 0;

    /// Reads and decodes a sound now, so its first play() does not have to.
    /// The game calls it for every Sound component when a scene starts.
    virtual void preload(AssetId /*sound*/) {}
    /// Holds or continues every sound (the editor's Pause).
    virtual void setPaused(bool paused) = 0;
    /// Stops every effect and the music at once (the game stopped).
    virtual void stopAll() = 0;
};

/// A SoundPlayer that plays nothing: used when the program has no audio.
class SilentSoundPlayer final : public SoundPlayer {
public:
    SoundHandle play(AssetId, const SoundOptions&) override { return 0; }
    void stop(SoundHandle) override {}
    bool isPlaying(SoundHandle) const override { return false; }
    void setVolume(SoundHandle, float) override {}
    void playMusic(AssetId, float, float) override {}
    void stopMusic(float) override {}
    AssetId music() const override { return {}; }
    void setChannelVolume(SoundChannel channel, float volume) override;
    float channelVolume(SoundChannel channel) const override;
    void setPaused(bool) override {}
    void stopAll() override {}

private:
    float m_volumes[3] = {1.0f, 1.0f, 1.0f};
};

/// A sound on an object, played without code: an effect when the object
/// appears (a chest's creak, a waterfall's loop), or the scene's music.
///
/// The game plays it when the object becomes active, if Play On Start is on.
/// Gameplay code and scripts can play it again (Game::playSound(entity),
/// object:playSound()). A looping effect stops when its object is removed or
/// switched off; a short effect plays to its end (a collected coin's sound
/// outlives the coin).
struct Sound : ComponentBase<Sound> {
    AssetId sound;             // an audio asset: WAV, OGG or MP3
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool playOnStart = true;
    bool music = false;        // play as the music (one at a time; fades between pieces)
    float fade = 1.0f;         // music: seconds to fade in, and to fade out what played before

    SoundOptions options() const { return {.volume = volume, .pitch = pitch, .pan = 0.0f, .loop = loop}; }
    static const ComponentType& componentType();
};

} // namespace rolo
