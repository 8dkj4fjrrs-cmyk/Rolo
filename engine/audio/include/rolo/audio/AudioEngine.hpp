#pragma once

// AudioEngine: plays a project's sounds through the computer's speakers.
//
// It is the SoundPlayer (rolo/gameplay/Sound.hpp) of the player and the
// editor, built on miniaudio (third_party/miniaudio, ADR 0015). Sounds are
// the project's audio assets (WAV, OGG, MP3), found through the
// AssetDatabase:
//   * effects are read and decoded once, the first time they play (or when a
//     scene with them starts: preload), and kept in memory, so playing one
//     again costs nothing;
//   * music is streamed: read a little at a time while it plays, so a long
//     piece does not fill the memory. (Without a device, music is read whole:
//     see playMusic.)
//
// miniaudio mixes on its own thread, which the sound card calls when it needs
// more sound. Everything here is called from the program's main thread.
//
// No sound card (a server, a CI machine): the engine says so in the log and
// plays nothing, like SilentSoundPlayer; the game runs the same. Tests make
// an engine with `device = false` and pull the mixed sound with mix().

#include "rolo/core/Log.hpp"
#include "rolo/gameplay/Sound.hpp"

#include <cstddef>
#include <memory>
#include <span>

namespace rolo {

class AssetDatabase;

class AudioEngine final : public SoundPlayer {
public:
    struct Settings {
        /// False: no sound card; the caller mixes with mix() (tests, tools).
        bool device = true;
        /// Samples per second when there is no device (with one, the device's own).
        unsigned sampleRate = 48000;
    };

    explicit AudioEngine(const Logger& log);
    AudioEngine(const Logger& log, Settings settings);
    ~AudioEngine() override;
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /// Where sounds come from. Changing it stops everything and forgets the
    /// sounds read so far. Not owned; must outlive its use here.
    void setAssets(const AssetDatabase* assets);

    /// False if sound could not start at all: then nothing plays.
    bool isAvailable() const noexcept;
    /// Call once per frame: frees the effects that have finished.
    void update();

    /// Effects playing now (tests, the editor's status).
    std::size_t playingCount() const;
    /// Without a device: mixes the next stretch of sound into `stereo`
    /// (left, right, left, right... from -1 to 1) and returns the frames
    /// written. Does nothing with a device (it pulls the sound itself).
    std::size_t mix(std::span<float> stereo);
    unsigned sampleRate() const noexcept;

    // SoundPlayer
    SoundHandle play(AssetId sound, const SoundOptions& options = {}) override;
    void stop(SoundHandle handle) override;
    bool isPlaying(SoundHandle handle) const override;
    void setVolume(SoundHandle handle, float volume) override;
    void playMusic(AssetId music, float fadeSeconds = 1.0f, float volume = 1.0f) override;
    void stopMusic(float fadeSeconds = 1.0f) override;
    AssetId music() const override;
    void setChannelVolume(SoundChannel channel, float volume) override;
    float channelVolume(SoundChannel channel) const override;
    void preload(AssetId sound) override;
    void setPaused(bool paused) override;
    void stopAll() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace rolo
