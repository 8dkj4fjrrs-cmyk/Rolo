#include "rolo/audio/AudioEngine.hpp"

#include "rolo/assets/AssetDatabase.hpp"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rolo {

namespace {

constexpr std::string_view kCategory = "audio";

// Sounds are not placed in a 3D world: no distance or direction effects.
constexpr ma_uint32 kFlat = MA_SOUND_FLAG_NO_SPATIALIZATION;

ma_uint64 milliseconds(float seconds)
{
    return static_cast<ma_uint64>(std::lround(std::clamp(seconds, 0.0f, 30.0f) * 1000.0f));
}

// A miniaudio sound must stay where it was made (the mixing thread holds its
// address), so each lives on the heap and is uninitialised before it is freed.
struct SoundDeleter {
    void operator()(ma_sound* sound) const
    {
        ma_sound_uninit(sound);
        delete sound;
    }
};
using SoundPtr = std::unique_ptr<ma_sound, SoundDeleter>;

} // namespace

struct AudioEngine::Impl {
    const Logger& log;
    Settings settings;
    ma_engine engine{};
    ma_sound_group musicGroup{};
    ma_sound_group effectsGroup{};
    bool ready = false;
    const AssetDatabase* assets = nullptr;

    std::map<AssetId, SoundPtr> decoded;        // effects read so far, copied for each play
    std::set<AssetId> reported;                 // sounds that failed (reported once)
    std::map<SoundHandle, SoundPtr> playing;    // effects playing
    SoundHandle nextHandle = 1;
    SoundPtr music;                             // the music playing
    AssetId musicId;
    std::vector<SoundPtr> fadingOut;            // music fading out before it is freed
    std::vector<ma_sound*> paused;              // held by setPaused(true)
    float volumes[3] = {1.0f, 1.0f, 1.0f};

    Impl(const Logger& logger, Settings s)
        : log(logger)
        , settings(s)
    {
    }

    // The file of a sound asset; empty (and reported once) if there is none.
    std::filesystem::path pathOf(AssetId id)
    {
        const AssetRecord* record = assets != nullptr ? assets->find(id) : nullptr;
        if (record == nullptr || record->type != AssetType::Audio) {
            if (reported.insert(id).second) {
                log.warning(kCategory, "Sound " + id.toString() + " is not a sound file of this project (was it deleted?).");
            }
            return {};
        }
        return assets->absolutePath(*record);
    }

    // Makes a miniaudio sound from a file. Null (reported once) if it cannot.
    SoundPtr load(AssetId id, ma_uint32 flags, ma_sound_group* group)
    {
        const std::filesystem::path path = pathOf(id);
        if (path.empty()) {
            return nullptr;
        }
        auto* sound = new ma_sound;
#ifdef _WIN32
        // Windows paths are UTF-16; the wide version opens any file name.
        const ma_result result = ma_sound_init_from_file_w(&engine, path.c_str(), flags | kFlat, group, nullptr, sound);
#else
        const ma_result result = ma_sound_init_from_file(&engine, path.c_str(), flags | kFlat, group, nullptr, sound);
#endif
        if (result != MA_SUCCESS) {
            delete sound;
            if (reported.insert(id).second) {
                const AssetRecord* record = assets->find(id);
                log.warning(kCategory, "Could not play " + record->path + ": " + ma_result_description(result)
                                           + ". Rolo plays WAV, OGG and MP3 files.");
            }
            return nullptr;
        }
        return SoundPtr(sound);
    }

    // An effect's decoded sound, read the first time it is needed.
    const ma_sound* effect(AssetId id)
    {
        if (const auto found = decoded.find(id); found != decoded.end()) {
            return found->second.get();
        }
        SoundPtr sound = load(id, MA_SOUND_FLAG_DECODE, &effectsGroup);
        if (!sound) {
            return nullptr;
        }
        return decoded.emplace(id, std::move(sound)).first->second.get();
    }

    // Frees effects that have played to their end, and music that faded out.
    void collect()
    {
        for (auto it = playing.begin(); it != playing.end();) {
            ma_sound* sound = it->second.get();
            const bool held = std::find(paused.begin(), paused.end(), sound) != paused.end();
            if (!held && !ma_sound_is_playing(sound)) {
                it = playing.erase(it);
            } else {
                ++it;
            }
        }
        std::erase_if(fadingOut, [](const SoundPtr& sound) { return !ma_sound_is_playing(sound.get()); });
    }

    void fadeOutMusic(float seconds)
    {
        if (!music) {
            return;
        }
        if (seconds <= 0.0f) {
            ma_sound_stop(music.get());
            music.reset();
        } else {
            ma_sound_stop_with_fade_in_milliseconds(music.get(), milliseconds(seconds));
            fadingOut.push_back(std::move(music));
        }
        musicId = {};
    }
};

AudioEngine::AudioEngine(const Logger& log)
    : AudioEngine(log, Settings{})
{
}

AudioEngine::AudioEngine(const Logger& log, Settings settings)
    : m(std::make_unique<Impl>(log, settings))
{
    ma_engine_config config = ma_engine_config_init();
    if (!settings.device) {
        config.noDevice = MA_TRUE;
        config.channels = 2;
        config.sampleRate = settings.sampleRate;
    }
    const ma_result result = ma_engine_init(&config, &m->engine);
    if (result != MA_SUCCESS) {
        // Not a warning: a computer without a sound card runs games silently.
        log.info(kCategory, std::string("No sound output (") + ma_result_description(result)
                                + "); the game plays without sound.");
        return;
    }
    if (ma_sound_group_init(&m->engine, 0, nullptr, &m->musicGroup) != MA_SUCCESS
        || ma_sound_group_init(&m->engine, 0, nullptr, &m->effectsGroup) != MA_SUCCESS) {
        log.info(kCategory, "Could not set up sound; the game plays without sound.");
        ma_engine_uninit(&m->engine);
        return;
    }
    m->ready = true;
    if (settings.device) {
        if (ma_device* device = ma_engine_get_device(&m->engine); device != nullptr && device->pContext->backend == ma_backend_null) {
            // miniaudio's last resort: sound is mixed, and goes nowhere.
            log.info(kCategory, "No sound output found; the game plays without sound.");
        } else if (device != nullptr) {
            log.info(kCategory, std::string("Sound output: ") + device->playback.name + " ("
                                    + ma_get_backend_name(device->pContext->backend) + ", "
                                    + std::to_string(ma_engine_get_sample_rate(&m->engine)) + " Hz)");
        }
    }
}

AudioEngine::~AudioEngine()
{
    if (m->ready) {
        // Sounds first: they belong to the groups, and the groups to the engine.
        m->paused.clear();
        m->playing.clear();
        m->fadingOut.clear();
        m->music.reset();
        m->decoded.clear();
        ma_sound_group_uninit(&m->effectsGroup);
        ma_sound_group_uninit(&m->musicGroup);
        ma_engine_uninit(&m->engine);
    }
}

void AudioEngine::setAssets(const AssetDatabase* assets)
{
    stopAll();
    m->decoded.clear();
    m->reported.clear();
    m->assets = assets;
}

bool AudioEngine::isAvailable() const noexcept
{
    return m->ready;
}

void AudioEngine::update()
{
    if (m->ready) {
        m->collect();
    }
}

std::size_t AudioEngine::playingCount() const
{
    return m->playing.size();
}

std::size_t AudioEngine::mix(std::span<float> stereo)
{
    if (!m->ready || m->settings.device) {
        return 0;
    }
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&m->engine, stereo.data(), stereo.size() / 2, &read);
    return static_cast<std::size_t>(read);
}

unsigned AudioEngine::sampleRate() const noexcept
{
    return m->ready ? ma_engine_get_sample_rate(&m->engine) : 0;
}

SoundHandle AudioEngine::play(AssetId sound, const SoundOptions& options)
{
    if (!m->ready || sound.isNull()) {
        return 0;
    }
    m->collect();
    const ma_sound* source = m->effect(sound);
    if (source == nullptr) {
        return 0;
    }
    // A copy shares the decoded samples; only its position and settings are its own.
    SoundPtr voice(new ma_sound);
    if (ma_sound_init_copy(&m->engine, source, kFlat, &m->effectsGroup, voice.get()) != MA_SUCCESS) {
        voice.release();   // never initialised: nothing to uninit
        return 0;
    }
    ma_sound_set_volume(voice.get(), std::clamp(options.volume, 0.0f, 2.0f));
    ma_sound_set_pitch(voice.get(), std::clamp(options.pitch, 0.1f, 4.0f));
    ma_sound_set_pan(voice.get(), std::clamp(options.pan, -1.0f, 1.0f));
    ma_sound_set_looping(voice.get(), options.loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(voice.get());
    const SoundHandle handle = m->nextHandle++;
    if (m->nextHandle == 0) {
        m->nextHandle = 1;   // 0 means "none"
    }
    m->playing.emplace(handle, std::move(voice));
    return handle;
}

void AudioEngine::stop(SoundHandle handle)
{
    if (const auto found = m->playing.find(handle); found != m->playing.end()) {
        std::erase(m->paused, found->second.get());
        m->playing.erase(found);   // uninit stops it
    }
}

bool AudioEngine::isPlaying(SoundHandle handle) const
{
    const auto found = m->playing.find(handle);
    if (found == m->playing.end()) {
        return false;
    }
    ma_sound* sound = found->second.get();
    return ma_sound_is_playing(sound) || std::find(m->paused.begin(), m->paused.end(), sound) != m->paused.end();
}

void AudioEngine::setVolume(SoundHandle handle, float volume)
{
    if (const auto found = m->playing.find(handle); found != m->playing.end()) {
        ma_sound_set_volume(found->second.get(), std::clamp(volume, 0.0f, 2.0f));
    }
}

void AudioEngine::playMusic(AssetId music, float fadeSeconds, float volume)
{
    if (!m->ready || music.isNull()) {
        return;
    }
    volume = std::clamp(volume, 0.0f, 2.0f);
    if (m->music && m->musicId == music) {
        // Already playing: carry on, gliding to the new volume.
        ma_sound_set_fade_in_milliseconds(m->music.get(), -1.0f, volume, milliseconds(std::min(fadeSeconds, 0.25f)));
        return;
    }
    // Streaming reads ahead on a helper thread, in step with the sound card.
    // Without one, mix() runs faster than real time and would outrun it, so
    // the music is read whole instead.
    const ma_uint32 how = m->settings.device ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    SoundPtr next = m->load(music, how, &m->musicGroup);
    if (!next) {
        return;
    }
    m->fadeOutMusic(fadeSeconds);
    ma_sound_set_looping(next.get(), MA_TRUE);
    ma_sound_set_fade_in_milliseconds(next.get(), fadeSeconds > 0.0f ? 0.0f : volume, volume, milliseconds(fadeSeconds));
    ma_sound_start(next.get());
    m->music = std::move(next);
    m->musicId = music;
}

void AudioEngine::stopMusic(float fadeSeconds)
{
    if (m->ready) {
        std::erase(m->paused, m->music.get());
        m->fadeOutMusic(fadeSeconds);
    }
}

AssetId AudioEngine::music() const
{
    return m->musicId;
}

void AudioEngine::setChannelVolume(SoundChannel channel, float volume)
{
    volume = std::clamp(volume, 0.0f, 1.0f);
    m->volumes[static_cast<int>(channel)] = volume;
    if (!m->ready) {
        return;
    }
    switch (channel) {
    case SoundChannel::Master: ma_engine_set_volume(&m->engine, volume); break;
    case SoundChannel::Music: ma_sound_group_set_volume(&m->musicGroup, volume); break;
    case SoundChannel::Effects: ma_sound_group_set_volume(&m->effectsGroup, volume); break;
    }
}

float AudioEngine::channelVolume(SoundChannel channel) const
{
    return m->volumes[static_cast<int>(channel)];
}

void AudioEngine::preload(AssetId sound)
{
    if (m->ready && !sound.isNull()) {
        m->effect(sound);
    }
}

void AudioEngine::setPaused(bool paused)
{
    if (!m->ready) {
        return;
    }
    if (paused) {
        if (!m->paused.empty()) {
            return;   // already paused
        }
        // Music fading out would come back on resume: let it go now.
        m->fadingOut.clear();
        for (auto& [handle, sound] : m->playing) {
            if (ma_sound_is_playing(sound.get())) {
                ma_sound_stop(sound.get());   // keeps its place, to continue from
                m->paused.push_back(sound.get());
            }
        }
        if (m->music && ma_sound_is_playing(m->music.get())) {
            ma_sound_stop(m->music.get());
            m->paused.push_back(m->music.get());
        }
    } else {
        for (ma_sound* sound : m->paused) {
            ma_sound_start(sound);
        }
        m->paused.clear();
    }
}

void AudioEngine::stopAll()
{
    m->paused.clear();
    m->playing.clear();
    m->fadingOut.clear();
    m->music.reset();
    m->musicId = {};
}

} // namespace rolo
