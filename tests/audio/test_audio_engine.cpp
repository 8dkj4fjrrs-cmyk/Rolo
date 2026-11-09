// AudioEngine (engine/audio): the sound it actually mixes.
//
// The engine runs without a sound card here (Settings::device = false): the
// test pulls the mixed samples with mix() and measures them, so "it plays"
// means sound came out, not just that a function was called.

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/audio/AudioEngine.hpp"
#include "rolo/core/Log.hpp"
#include "support/TempFolder.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace rolo;
namespace fs = std::filesystem;

namespace {

constexpr unsigned kRate = 48000;

// A project with the test sounds (tests/data/audio) and a silent engine.
struct SoundProject {
    fs::path folder = test::uniqueTempFolder("audio");
    AssetDatabase assets{folder};
    Logger log;
    std::vector<std::string> problems;
    AudioEngine audio{log, {.device = false, .sampleRate = kRate}};

    SoundProject()
    {
        fs::create_directories(folder / "sounds");
        for (const char* name : {"tone.wav", "tone.ogg", "tone.mp3", "broken.wav"}) {
            fs::copy_file(fs::path(ROLO_TEST_DATA_DIR) / "audio" / name, folder / "sounds" / name);
        }
        assets.refresh();
        audio.setAssets(&assets);
        log.addSink([this](const LogMessage& m) {
            if (m.level >= LogLevel::Warning) {
                problems.push_back(m.text);
            }
        });
    }
    ~SoundProject()
    {
        std::error_code ignored;
        fs::remove_all(folder, ignored);
    }

    AssetId id(const std::string& path) const
    {
        const AssetRecord* record = assets.findByPath(path);
        REQUIRE(record != nullptr);
        return record->id;
    }

    // Mixes `seconds` of sound; returns how loud it was (root mean square).
    float listen(float seconds)
    {
        std::vector<float> buffer(static_cast<std::size_t>(seconds * kRate) * 2);
        const std::size_t frames = audio.mix(buffer);
        REQUIRE(frames == buffer.size() / 2);
        double sum = 0.0;
        for (const float sample : buffer) {
            sum += static_cast<double>(sample) * sample;
        }
        audio.update();
        return static_cast<float>(std::sqrt(sum / static_cast<double>(buffer.size())));
    }

    // A stopped sound fades out over a few milliseconds instead of being cut,
    // which would click: let that pass before listening for silence.
    void settle() { listen(0.02f); }
};

// The test tone is a sine at half volume: about 0.35 when heard in full.
constexpr float kHeard = 0.05f;
constexpr float kSilent = 0.001f;

} // namespace

TEST_CASE("WAV, OGG and MP3 sounds play, and finish", "[audio]")
{
    SoundProject p;
    REQUIRE(p.audio.isAvailable());
    CHECK(p.audio.sampleRate() == kRate);
    const std::string file = GENERATE("sounds/tone.wav", "sounds/tone.ogg", "sounds/tone.mp3");
    INFO(file);

    CHECK(p.listen(0.05f) < kSilent);   // nothing yet
    const SoundHandle handle = p.audio.play(p.id(file));
    REQUIRE(handle != 0);
    CHECK(p.audio.isPlaying(handle));
    CHECK(p.listen(0.1f) > kHeard);

    // The tone lasts a quarter of a second, then the effect is freed.
    p.listen(0.3f);
    CHECK_FALSE(p.audio.isPlaying(handle));
    CHECK(p.audio.playingCount() == 0);
    CHECK(p.listen(0.05f) < kSilent);
    CHECK(p.problems.empty());
}

TEST_CASE("Several effects play at once, each with its own volume", "[audio]")
{
    SoundProject p;
    const AssetId tone = p.id("sounds/tone.wav");
    const SoundHandle loud = p.audio.play(tone);
    const SoundHandle quiet = p.audio.play(tone, {.volume = 0.0f});
    CHECK(loud != quiet);
    CHECK(p.audio.playingCount() == 2);
    const float both = p.listen(0.05f);
    p.audio.stop(loud);
    CHECK_FALSE(p.audio.isPlaying(loud));
    CHECK(both > kHeard);
    p.settle();
    CHECK(p.listen(0.05f) < kSilent);   // the other one is silent
    CHECK(p.audio.isPlaying(quiet));
}

TEST_CASE("A looping effect plays until it is stopped", "[audio]")
{
    SoundProject p;
    const SoundHandle handle = p.audio.play(p.id("sounds/tone.wav"), {.loop = true});
    CHECK(p.listen(1.0f) > kHeard);   // four times as long as the sound
    CHECK(p.audio.isPlaying(handle));
    CHECK(p.listen(0.1f) > kHeard);
    p.audio.stop(handle);
    p.settle();
    CHECK(p.listen(0.05f) < kSilent);
}

TEST_CASE("Pitch 2 plays twice as fast", "[audio]")
{
    SoundProject p;
    const SoundHandle handle = p.audio.play(p.id("sounds/tone.wav"), {.pitch = 2.0f});
    p.listen(0.1f);
    CHECK(p.audio.isPlaying(handle));
    p.listen(0.05f);   // 0.15 s: past the end of a 0.25 s sound played at double speed
    CHECK_FALSE(p.audio.isPlaying(handle));
}

TEST_CASE("Channel volumes multiply: master, music and effects", "[audio]")
{
    SoundProject p;
    const AssetId tone = p.id("sounds/tone.wav");
    p.audio.setChannelVolume(SoundChannel::Effects, 0.0f);
    CHECK(p.audio.channelVolume(SoundChannel::Effects) == 0.0f);
    p.audio.play(tone, {.loop = true});
    CHECK(p.listen(0.05f) < kSilent);
    p.audio.setChannelVolume(SoundChannel::Effects, 1.0f);
    const float full = p.listen(0.05f);
    p.audio.setChannelVolume(SoundChannel::Master, 0.5f);
    const float half = p.listen(0.05f);
    CHECK(full > kHeard);
    CHECK(half == Catch::Approx(full * 0.5f).epsilon(0.1));
    p.audio.setChannelVolume(SoundChannel::Master, 2.0f);   // clamped to 1
    CHECK(p.audio.channelVolume(SoundChannel::Master) == 1.0f);
}

TEST_CASE("Music: one piece at a time, carried on when asked for again", "[audio]")
{
    SoundProject p;
    const AssetId wav = p.id("sounds/tone.wav");
    const AssetId ogg = p.id("sounds/tone.ogg");
    p.audio.playMusic(wav, 0.0f);
    CHECK(p.audio.music() == wav);
    CHECK(p.listen(1.0f) > kHeard);   // music loops
    p.audio.playMusic(wav, 0.0f);     // the same: carries on
    CHECK(p.audio.music() == wav);

    p.audio.playMusic(ogg, 0.5f);   // fades from one to the other
    CHECK(p.audio.music() == ogg);
    CHECK(p.listen(0.25f) > kHeard);
    p.audio.setChannelVolume(SoundChannel::Music, 0.0f);
    CHECK(p.listen(0.05f) < kSilent);
    p.audio.setChannelVolume(SoundChannel::Music, 1.0f);

    p.audio.stopMusic(0.25f);
    CHECK(p.audio.music().isNull());
    p.listen(0.3f);   // the fade
    CHECK(p.listen(0.05f) < kSilent);
}

TEST_CASE("Music fades in", "[audio]")
{
    SoundProject p;
    p.audio.playMusic(p.id("sounds/tone.wav"), 1.0f);
    const float start = p.listen(0.1f);
    p.listen(0.8f);
    const float later = p.listen(0.1f);
    CHECK(start < later * 0.5f);
}

TEST_CASE("Pausing holds every sound, and resuming continues it", "[audio]")
{
    SoundProject p;
    const SoundHandle handle = p.audio.play(p.id("sounds/tone.wav"));
    p.audio.playMusic(p.id("sounds/tone.ogg"), 0.0f);
    p.listen(0.1f);
    p.audio.setPaused(true);
    p.settle();
    CHECK(p.listen(0.5f) < kSilent);
    CHECK(p.audio.isPlaying(handle));   // held, not finished
    p.audio.setPaused(false);
    CHECK(p.listen(0.05f) > kHeard);

    p.audio.stopAll();
    CHECK_FALSE(p.audio.isPlaying(handle));
    CHECK(p.audio.music().isNull());
    p.settle();
    CHECK(p.listen(0.05f) < kSilent);
}

TEST_CASE("A sound that cannot be read is reported once, and the rest keeps playing", "[audio]")
{
    SoundProject p;
    const AssetId broken = p.id("sounds/broken.wav");
    CHECK(p.audio.play(broken) == 0);
    CHECK(p.audio.play(broken) == 0);
    REQUIRE(p.problems.size() == 1);
    CHECK_THAT(p.problems[0], Catch::Matchers::ContainsSubstring("sounds/broken.wav"));

    CHECK(p.audio.play(AssetId{0x1234}) == 0);   // not in the project
    CHECK(p.problems.size() == 2);
    p.audio.playMusic(AssetId{0x1234});
    CHECK(p.audio.music().isNull());
    CHECK(p.problems.size() == 2);   // the same sound: not reported again

    CHECK(p.audio.play(p.id("sounds/tone.wav")) != 0);
    CHECK(p.listen(0.05f) > kHeard);
}

TEST_CASE("Without assets, or with a null sound, nothing plays", "[audio]")
{
    Logger log;
    AudioEngine audio(log, {.device = false});
    CHECK(audio.play(AssetId{}) == 0);
    audio.preload(AssetId{});
    CHECK_FALSE(audio.isPlaying(0));
    audio.stop(42);   // unknown handles are ignored
    audio.setVolume(42, 0.5f);
    SUCCEED();
}
