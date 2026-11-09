// Sound in games (rolo/gameplay/Sound.hpp): the Sound component, the sound
// actions of triggers and dialogues, Lua's audio table, and the notation and
// checks for the actions. The game is given a SoundPlayer that records what
// it is asked to do; test_audio_engine.cpp checks that the real one plays.

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/content/ContentLibrary.hpp"
#include "rolo/content/Logic.hpp"
#include "rolo/content/Notation.hpp"
#include "rolo/core/Log.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/Script.hpp"
#include "rolo/gameplay/Sound.hpp"
#include "rolo/scene/Components.hpp"
#include "support/TempFolder.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace rolo;
using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;

namespace {

// Writes down what the game asks of its audio, e.g. "play 0000000000000abc".
struct RecordingSound final : SoundPlayer {
    std::vector<std::string> calls;
    std::map<SoundHandle, SoundOptions> effects;   // playing
    SoundHandle next = 1;
    AssetId current;
    float volumes[3] = {1.0f, 1.0f, 1.0f};
    bool paused = false;

    SoundHandle play(AssetId sound, const SoundOptions& o) override
    {
        calls.push_back("play " + sound.toString() + (o.loop ? " loop" : ""));
        effects[next] = o;
        return next++;
    }
    void stop(SoundHandle handle) override
    {
        if (effects.erase(handle) > 0) {
            calls.push_back("stop " + std::to_string(handle));
        }
    }
    bool isPlaying(SoundHandle handle) const override { return effects.count(handle) > 0; }
    void setVolume(SoundHandle handle, float volume) override { effects[handle].volume = volume; }
    void playMusic(AssetId music, float fade, float volume) override
    {
        calls.push_back("music " + music.toString() + " fade " + std::to_string(static_cast<int>(fade * 10.0f))
                        + " volume " + std::to_string(static_cast<int>(volume * 10.0f)));
        current = music;
    }
    void stopMusic(float) override
    {
        calls.push_back("stopMusic");
        current = {};
    }
    AssetId music() const override { return current; }
    void setChannelVolume(SoundChannel channel, float volume) override { volumes[static_cast<int>(channel)] = volume; }
    float channelVolume(SoundChannel channel) const override { return volumes[static_cast<int>(channel)]; }
    void preload(AssetId sound) override { calls.push_back("preload " + sound.toString()); }
    void setPaused(bool p) override { paused = p; }
    void stopAll() override
    {
        calls.push_back("stopAll");
        effects.clear();
        current = {};
    }

    bool called(const std::string& call) const { return std::find(calls.begin(), calls.end(), call) != calls.end(); }
    std::ptrdiff_t count(const std::string& call) const { return std::count(calls.begin(), calls.end(), call); }
};

// A project with two sounds and room for scripts.
struct SoundGame {
    fs::path folder = test::uniqueTempFolder("sound-game");
    AssetDatabase assets{folder};
    Logger log;
    std::vector<std::string> problems;
    RecordingSound sound;
    ContentLibrary content;
    std::map<AssetId, Scene> scenes;
    AssetId tone;
    AssetId song;

    SoundGame()
    {
        fs::create_directories(folder / "sounds");
        fs::copy_file(fs::path(ROLO_TEST_DATA_DIR) / "audio" / "tone.wav", folder / "sounds" / "tone.wav");
        fs::copy_file(fs::path(ROLO_TEST_DATA_DIR) / "audio" / "tone.ogg", folder / "sounds" / "song.ogg");
        assets.refresh();
        tone = assets.findByPath("sounds/tone.wav")->id;
        song = assets.findByPath("sounds/song.ogg")->id;
        content = ContentLibrary::load(assets);
        log.addSink([this](const LogMessage& m) {
            if (m.level >= LogLevel::Warning) {
                problems.push_back(m.text);
            }
        });
    }
    ~SoundGame()
    {
        std::error_code ignored;
        fs::remove_all(folder, ignored);
    }

    std::unique_ptr<Game> makeGame()
    {
        GameSetup setup;
        setup.assets = &assets;
        setup.content = &content;
        setup.sound = &sound;
        setup.loadScene = [this](AssetId id, std::string& error) -> std::optional<Scene> {
            if (const auto found = scenes.find(id); found != scenes.end()) {
                return found->second;
            }
            error = "no such scene";
            return std::nullopt;
        };
        return std::make_unique<Game>(log, std::move(setup));
    }

    AssetId writeScript(const std::string& path, const std::string& code)
    {
        fs::create_directories((folder / path).parent_path());
        std::ofstream(folder / path, std::ios::binary) << code;
        assets.refresh();
        return assets.findByPath(path)->id;
    }

    bool hasProblem(const std::string& part) const
    {
        return std::any_of(problems.begin(), problems.end(),
                           [&](const std::string& p) { return p.find(part) != std::string::npos; });
    }
};

EntityId addSound(Scene& scene, const std::string& name, AssetId soundId, bool loop = false, bool music = false)
{
    const EntityId id = scene.create(name)->id();
    auto component = std::make_unique<Sound>();
    component->sound = soundId;
    component->loop = loop;
    component->music = music;
    component->fade = 2.0f;
    component->volume = 0.5f;
    scene.addComponent(id, std::move(component));
    return id;
}

void frames(Game& game, int n)
{
    for (int i = 0; i < n; ++i) {
        game.update(1.0f / 60.0f);
    }
}

Action action(Action::Kind kind, AssetId asset = {}, EntityId entity = {}, bool active = true)
{
    Action a;
    a.kind = kind;
    a.asset = asset;
    a.entity = entity;
    a.active = active;
    return a;
}

} // namespace

TEST_CASE("A Sound plays when its object appears, and its file is read when the scene starts", "[gameplay][sound]")
{
    SoundGame g;
    Scene scene;
    addSound(scene, "Chest", g.tone);
    addSound(scene, "Theme", g.song, false, true);
    auto quiet = std::make_unique<Sound>();
    quiet->sound = g.tone;
    quiet->playOnStart = false;
    const EntityId later = scene.create("Later")->id();
    scene.addComponent(later, std::move(quiet));

    auto game = g.makeGame();
    game->start(scene);
    // Effects are read before the first frame; music streams, so it is not.
    CHECK(g.sound.count("preload " + g.tone.toString()) == 2);
    CHECK_FALSE(g.sound.called("preload " + g.song.toString()));
    CHECK(g.sound.calls.size() == 3);   // stopAll (a new game) and the preloads

    frames(*game, 1);
    CHECK(g.sound.count("play " + g.tone.toString()) == 1);   // "Later" waits to be played
    CHECK(g.sound.called("music " + g.song.toString() + " fade 20 volume 5"));
    CHECK(g.sound.music() == g.song);
    frames(*game, 10);
    CHECK(g.sound.count("play " + g.tone.toString()) == 1);   // once, not every frame

    CHECK(game->playSound(later) != 0);
    CHECK(g.sound.count("play " + g.tone.toString()) == 2);
    CHECK(g.problems.empty());
}

TEST_CASE("A looping Sound stops when its object goes; a short one plays to its end", "[gameplay][sound]")
{
    SoundGame g;
    Scene scene;
    const EntityId fountain = addSound(scene, "Fountain", g.tone, true);
    const EntityId coin = addSound(scene, "Coin", g.tone, false);
    auto game = g.makeGame();
    game->start(scene);
    frames(*game, 1);
    REQUIRE(g.sound.effects.size() == 2);

    game->destroy(coin);
    game->destroy(fountain);
    frames(*game, 2);
    CHECK(g.sound.effects.size() == 1);   // the coin's sound carries on
    CHECK(g.sound.effects.begin()->second.loop == false);
}

TEST_CASE("Switching an object off stops its loop; switching it on plays it again", "[gameplay][sound]")
{
    SoundGame g;
    Scene scene;
    const EntityId fire = addSound(scene, "Fire", g.tone, true);
    auto game = g.makeGame();
    game->start(scene);
    frames(*game, 1);
    CHECK(g.sound.effects.size() == 1);

    game->content().run({action(Action::Kind::SetActive, {}, fire, false)});
    frames(*game, 1);
    CHECK(g.sound.effects.empty());
    game->content().run({action(Action::Kind::SetActive, {}, fire, true)});
    frames(*game, 1);
    CHECK(g.sound.effects.size() == 1);
    CHECK(g.sound.count("play " + g.tone.toString() + " loop") == 2);

    // Playing a loop that is already going does not stack it.
    game->playSound(fire);
    CHECK(g.sound.effects.size() == 1);
    game->stopSound(fire);
    CHECK(g.sound.effects.empty());
}

TEST_CASE("A new scene stops the old scene's loops; music carries on", "[gameplay][sound]")
{
    SoundGame g;
    Scene first;
    addSound(first, "Rain", g.tone, true);
    addSound(first, "Theme", g.song, false, true);
    Scene second;
    addSound(second, "Theme", g.song, false, true);
    const AssetId secondId{0x2};
    g.scenes[secondId] = second;

    auto game = g.makeGame();
    game->start(first);
    frames(*game, 1);
    CHECK(g.sound.effects.size() == 1);
    game->changeScene(secondId);
    frames(*game, 2);
    CHECK(g.sound.effects.empty());          // the rain stopped
    CHECK(g.sound.music() == g.song);        // asked for again: the player carries it on
    CHECK_FALSE(g.sound.called("stopMusic"));
}

TEST_CASE("Stopping the game stops its sounds", "[gameplay][sound]")
{
    SoundGame g;
    Scene scene;
    addSound(scene, "Rain", g.tone, true);
    {
        auto game = g.makeGame();
        game->start(scene);
        frames(*game, 1);
        g.sound.calls.clear();
        game->stop();
        CHECK(g.sound.called("stopAll"));
        g.sound.calls.clear();
    }
    CHECK_FALSE(g.sound.called("stopAll"));   // stopped already: nothing more to stop
    {
        auto game = g.makeGame();
        game->start(scene);
        g.sound.calls.clear();
    }   // destroyed while running (the editor's Stop)
    CHECK(g.sound.called("stopAll"));
}

TEST_CASE("A game without audio runs silently", "[gameplay][sound]")
{
    SoundGame g;
    Scene scene;
    const EntityId rain = addSound(scene, "Rain", g.tone, true);
    Game game(g.log, &g.assets);
    game.start(scene);
    frames(game, 2);
    CHECK(game.playSound(rain) == 0);
    game.sound().setChannelVolume(SoundChannel::Music, 0.25f);
    CHECK(game.sound().channelVolume(SoundChannel::Music) == 0.25f);
    CHECK(g.problems.empty());
}

TEST_CASE("Actions play sounds and music", "[gameplay][sound][content]")
{
    SoundGame g;
    Scene scene;
    scene.create("Empty");
    auto game = g.makeGame();
    game->start(scene);
    game->content().run({action(Action::Kind::PlaySound, g.tone), action(Action::Kind::PlayMusic, g.song)});
    CHECK(g.sound.called("play " + g.tone.toString()));
    CHECK(g.sound.called("music " + g.song.toString() + " fade 10 volume 10"));
    game->content().run({action(Action::Kind::StopMusic)});
    CHECK(g.sound.called("stopMusic"));
}

TEST_CASE("Sound actions are written, read and checked like the others", "[content][sound]")
{
    SoundGame g;
    const NotationNames names = projectNotationNames(&g.assets, nullptr);
    std::string error;

    const auto play = parseActionNotation("playSound tone", names, &error);
    REQUIRE(play);
    CHECK(play->kind == Action::Kind::PlaySound);
    CHECK(play->asset == g.tone);
    CHECK(formatAction(*play, names) == "playSound tone");
    const auto music = parseActionNotation("playMusic \"sounds/song.ogg\"", names, &error);
    REQUIRE(music);
    CHECK(music->asset == g.song);
    CHECK(formatAction(*music, names) == "playMusic song");
    const auto stop = parseActionNotation("stopMusic", names, &error);
    REQUIRE(stop);
    CHECK(stop->kind == Action::Kind::StopMusic);
    CHECK(formatAction(*stop, names) == "stopMusic");

    CHECK_FALSE(parseActionNotation("playSound nothing", names, &error));
    CHECK(error == "there is no sound called \"nothing\"");
    CHECK_FALSE(parseActionNotation("playMusic", names, &error));
    CHECK_THAT(error, ContainsSubstring("expected the name of a sound"));

    // JSON: {"type": "playSound", "sound": "..."}
    const std::vector<Action> actions{*play, *music, *stop};
    const auto read = parseActions(serializeActions(actions), &error);
    INFO(error);
    REQUIRE(read);
    CHECK(*read == actions);
    CHECK_THAT(serializeActions(actions), ContainsSubstring("\"sound\""));

    // A sound that is not in the project is an error in the content check.
    ContentLibrary library = ContentLibrary::load(g.assets);
    Quest quest;
    quest.name = "Q";
    Objective objective;
    objective.id = "go";
    objective.text = "Go";
    quest.objectives.push_back(objective);
    quest.rewards.push_back(action(Action::Kind::PlaySound, AssetId{0x999}));
    quest.rewards.push_back(action(Action::Kind::PlayMusic, g.song));
    library.addQuest(AssetId{0x77}, "quests/q.lquest", quest);
    const std::vector<FileIssue> issues = library.check();
    REQUIRE(issues.size() == 1);
    CHECK_THAT(issues[0].message, ContainsSubstring("The sound 0000000000000999 does not exist"));
}

TEST_CASE("Scripts play sounds with the audio table", "[gameplay][sound][script]")
{
    SoundGame g;
    const AssetId script = g.writeScript("scripts/noise.lua", R"(
        function start(self)
            self.loop = audio.play("tone", { volume = 0.5, pitch = 1.5, pan = -1, loop = true })
            audio.play("sounds/tone.wav")
            audio.playMusic("song", { fade = 3, volume = 0.8 })
            audio.setVolume("effects", 0.25)
            assert(audio.volume("effects") == 0.25)
            assert(audio.music() == "song")
            assert(audio.isPlaying(self.loop))
            audio.stop(self.loop)
            assert(not audio.isPlaying(self.loop))
            audio.stop(nil)
            assert(not audio.isPlaying(nil))
            audio.stopMusic(0)
            assert(audio.music() == nil)
            self.object:playSound()
            print("done")
        end
    )");
    Scene scene;
    const EntityId box = scene.create("Box")->id();
    auto sound = std::make_unique<Sound>();
    sound->sound = g.tone;
    sound->playOnStart = false;
    scene.addComponent(box, std::move(sound));
    auto component = std::make_unique<Script>();
    component->script = script;
    scene.addComponent(box, std::move(component));

    auto game = g.makeGame();
    game->start(scene);
    frames(*game, 1);
    const std::string firstProblem = g.problems.empty() ? std::string() : g.problems.front();
    INFO(firstProblem);
    CHECK(g.problems.empty());
    CHECK(g.sound.called("play " + g.tone.toString() + " loop"));
    CHECK(g.sound.count("play " + g.tone.toString()) == 2);   // the second effect, and the object's Sound
    CHECK(g.sound.called("music " + g.song.toString() + " fade 30 volume 8"));
    CHECK(g.sound.called("stopMusic"));
    CHECK(g.sound.channelVolume(SoundChannel::Effects) == 0.25f);
}

TEST_CASE("Script mistakes with sounds say what was wrong", "[gameplay][sound][script]")
{
    SoundGame g;
    struct Case {
        const char* code;
        const char* message;
    };
    const Case cases[] = {
        {"audio.play('missing')", "audio.play: there is no sound called \"missing\""},
        {"audio.play('tone', { speed = 2 })", "unknown option 'speed' (known: volume, pitch, pan, loop)"},
        {"audio.play('tone', { volume = 5 })", "volume must be from 0 to 2, not 5"},
        {"audio.playMusic('song', { pitch = 2 })", "unknown option 'pitch' (known: volume, fade)"},
        {"audio.setVolume('voices', 1)", "invalid option 'voices'"},
        {"self.object:playSound()", "this object has no Sound component"},
    };
    for (const Case& c : cases) {
        INFO(c.code);
        g.problems.clear();
        const AssetId script =
            g.writeScript("scripts/bad.lua", std::string("function start(self) ") + c.code + " end");
        Scene scene;
        const EntityId box = scene.create("Box")->id();
        auto component = std::make_unique<Script>();
        component->script = script;
        scene.addComponent(box, std::move(component));
        auto game = g.makeGame();
        game->start(scene);
        frames(*game, 1);
        CHECK(g.hasProblem(c.message));
    }
}
