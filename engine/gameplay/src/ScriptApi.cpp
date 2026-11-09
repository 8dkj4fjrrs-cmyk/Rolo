// What Lua scripts can call: the `game`, `input` and `audio` tables, print, require,
// and the methods of objects (self.object, game.find(...)).
//
// docs/scripting.md lists all of it for script writers; keep the two in step.
//
// Conventions, so scripts feel the same everywhere:
//   * positions are two numbers, x then y, in points; y goes up;
//   * angles are in degrees;
//   * objects are handles: they stay valid as long as the object exists, and
//     using one after its object is gone is a clear error;
//   * names (objects, scenes, dialogues, variables) are the ones shown in
//     the editor.
// A mistake in how a function is called (a missing argument, an unknown key
// name) is a Lua error that says what was expected; the script stops with
// it, and the Console shows it with the file and line.

#include "ScriptHost.hpp"
#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/content/ContentLibrary.hpp"
#include "rolo/content/Notation.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/scene/Components.hpp"
#include "lauxlib.h"
#include "lua.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>

namespace rolo {

namespace {

constexpr const char* kObjectType = "rolo.Object";
constexpr std::string_view kCategory = "script";

// --- Helpers ---------------------------------------------------------------------------------------------

Game& gameOf(lua_State* L)
{
    return ScriptHost::of(L).game();
}

// Rolo code that throws a C++ exception inside a Lua call must turn it into
// a Lua error, or Lua cannot report it properly. Every binding goes through this.
template <int (*Function)(lua_State*)> int guarded(lua_State* L)
{
    std::string message;
    try {
        return Function(L);
    } catch (const std::exception& error) {
        message = error.what();
    }
    // Outside the catch block: luaL_error itself unwinds with an exception.
    return luaL_error(L, "%s", message.c_str());
}

float checkFloat(lua_State* L, int index)
{
    const lua_Number value = luaL_checknumber(L, index);
    if (!std::isfinite(value)) {
        luaL_argerror(L, index, "a finite number expected");
    }
    return static_cast<float>(value);
}

std::string_view checkText(lua_State* L, int index)
{
    std::size_t length = 0;
    const char* text = luaL_checklstring(L, index, &length);
    return {text, length};
}

void pushText(lua_State* L, std::string_view text)
{
    lua_pushlstring(L, text.data(), text.size());
}

// --- Objects ---------------------------------------------------------------------------------------------

struct ObjectHandle {
    std::uint64_t id = 0;
};

EntityId objectId(lua_State* L, int index)
{
    const auto* handle = static_cast<const ObjectHandle*>(luaL_checkudata(L, index, kObjectType));
    return EntityId{handle->id};
}

// The object a handle refers to; an error if it is gone.
const Entity& objectEntity(lua_State* L, int index)
{
    const EntityId id = objectId(L, index);
    const Game& game = gameOf(L);
    const Entity* entity = game.isRunning() ? game.scene().find(id) : nullptr;
    if (entity == nullptr) {
        luaL_error(L, "this object no longer exists (it was destroyed, or the scene changed)");
    }
    return *entity;
}

Transform& objectTransform(lua_State* L, int index)
{
    const EntityId id = objectEntity(L, index).id();
    Transform* transform = gameOf(L).transform(id);
    if (transform == nullptr) {
        luaL_error(L, "this object has no Transform");
    }
    return *transform;
}

int objectName(lua_State* L)
{
    pushText(L, objectEntity(L, 1).name());
    return 1;
}

int objectIsValid(lua_State* L)
{
    const EntityId id = objectId(L, 1);
    const Game& game = gameOf(L);
    lua_pushboolean(L, game.isRunning() && game.scene().find(id) != nullptr);
    return 1;
}

int objectPosition(lua_State* L)
{
    const Transform& t = objectTransform(L, 1);
    lua_pushnumber(L, static_cast<lua_Number>(t.position.x));
    lua_pushnumber(L, static_cast<lua_Number>(t.position.y));
    return 2;
}

int objectSetPosition(lua_State* L)
{
    Transform& t = objectTransform(L, 1);
    t.position = Vec2{checkFloat(L, 2), checkFloat(L, 3)};
    return 0;
}

int objectWorldPosition(lua_State* L)
{
    const Vec2 p = gameOf(L).worldPosition(objectEntity(L, 1).id());
    lua_pushnumber(L, static_cast<lua_Number>(p.x));
    lua_pushnumber(L, static_cast<lua_Number>(p.y));
    return 2;
}

// obj:move(dx, dy) -> movedX, movedY, blocked, hitObject
int objectMove(lua_State* L)
{
    const EntityId id = objectEntity(L, 1).id();
    const MoveResult result = gameOf(L).move(id, Vec2{checkFloat(L, 2), checkFloat(L, 3)});
    lua_pushnumber(L, static_cast<lua_Number>(result.moved.x));
    lua_pushnumber(L, static_cast<lua_Number>(result.moved.y));
    lua_pushboolean(L, result.blockedX || result.blockedY);
    pushObjectForScripts(L, result.hit);
    return 4;
}

int objectRotation(lua_State* L)
{
    lua_pushnumber(L, static_cast<lua_Number>(objectTransform(L, 1).rotation));
    return 1;
}

int objectSetRotation(lua_State* L)
{
    objectTransform(L, 1).rotation = checkFloat(L, 2);
    return 0;
}

int objectScale(lua_State* L)
{
    const Transform& t = objectTransform(L, 1);
    lua_pushnumber(L, static_cast<lua_Number>(t.scale.x));
    lua_pushnumber(L, static_cast<lua_Number>(t.scale.y));
    return 2;
}

int objectSetScale(lua_State* L)
{
    Transform& t = objectTransform(L, 1);
    const float x = checkFloat(L, 2);
    const float y = lua_isnoneornil(L, 3) ? x : checkFloat(L, 3); // one number: the same both ways
    t.scale = Vec2{x, y};
    return 0;
}

int objectIsActive(lua_State* L)
{
    lua_pushboolean(L, objectEntity(L, 1).isActive());
    return 1;
}

int objectSetActive(lua_State* L)
{
    const EntityId id = objectEntity(L, 1).id();
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    ScriptHost::of(L).setActive(id, lua_toboolean(L, 2) != 0);
    return 0;
}

int objectDestroy(lua_State* L)
{
    gameOf(L).destroy(objectEntity(L, 1).id());
    return 0;
}

int objectPlay(lua_State* L)
{
    const EntityId id = objectEntity(L, 1).id();
    const std::string clip(checkText(L, 2));
    const bool restart = lua_toboolean(L, 3) != 0;
    lua_pushboolean(L, gameOf(L).play(id, clip, restart));
    return 1;
}

int objectStopAnimation(lua_State* L)
{
    gameOf(L).stopAnimation(objectEntity(L, 1).id());
    return 0;
}

int objectClip(lua_State* L)
{
    pushText(L, gameOf(L).currentClip(objectEntity(L, 1).id()));
    return 1;
}

int objectIsAnimationPlaying(lua_State* L)
{
    lua_pushboolean(L, gameOf(L).isAnimationPlaying(objectEntity(L, 1).id()));
    return 1;
}

int objectIsAnimationFinished(lua_State* L)
{
    lua_pushboolean(L, gameOf(L).isAnimationFinished(objectEntity(L, 1).id()));
    return 1;
}

int objectOverlapping(lua_State* L)
{
    const auto others = gameOf(L).overlapping(objectEntity(L, 1).id());
    lua_createtable(L, static_cast<int>(others.size()), 0);
    lua_Integer i = 1;
    for (const EntityId other : others) {
        pushObjectForScripts(L, other);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int objectIsTouching(lua_State* L)
{
    const EntityId self = objectEntity(L, 1).id();
    const EntityId other = objectEntity(L, 2).id();
    bool touching = false;
    for (const EntityId id : gameOf(L).overlapping(self)) {
        touching = touching || id == other;
    }
    lua_pushboolean(L, touching);
    return 1;
}

int objectText(lua_State* L)
{
    const Text* text = objectEntity(L, 1).get<Text>();
    if (text == nullptr) {
        return luaL_error(L, "this object has no Text component");
    }
    pushText(L, text->text);
    return 1;
}

int objectSetText(lua_State* L)
{
    const EntityId id = objectEntity(L, 1).id();
    const std::string_view value = luaL_tolstring(L, 2, nullptr); // numbers and booleans too
    Text* text = gameOf(L).get<Text>(id);
    if (text == nullptr) {
        return luaL_error(L, "this object has no Text component");
    }
    if (text->text != value) { // changing text redraws it: only when it changes
        text->text = std::string(value);
    }
    return 0;
}

int objectSetFlipX(lua_State* L)
{
    const EntityId id = objectEntity(L, 1).id();
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    const bool flip = lua_toboolean(L, 2) != 0;
    const Sprite* sprite = objectEntity(L, 1).get<Sprite>();
    if (sprite == nullptr) {
        return luaL_error(L, "this object has no Sprite component");
    }
    if (sprite->flipX != flip) {
        gameOf(L).get<Sprite>(id)->flipX = flip;
    }
    return 0;
}

int objectEquals(lua_State* L)
{
    lua_pushboolean(L, objectId(L, 1) == objectId(L, 2));
    return 1;
}

int objectToString(lua_State* L)
{
    const EntityId id = objectId(L, 1);
    const Game& game = gameOf(L);
    const Entity* entity = game.isRunning() ? game.scene().find(id) : nullptr;
    if (entity != nullptr) {
        lua_pushfstring(L, "object \"%s\"", entity->name().c_str());
    } else {
        lua_pushstring(L, "object (destroyed)");
    }
    return 1;
}

// --- game.* ---------------------------------------------------------------------------------------------

int gameFind(lua_State* L)
{
    const std::string_view name = checkText(L, 1);
    pushObjectForScripts(L, gameOf(L).find(name));
    return 1;
}

int gamePlayer(lua_State* L)
{
    pushObjectForScripts(L, ScriptHost::of(L).player());
    return 1;
}

int gameTime(lua_State* L)
{
    lua_pushnumber(L, static_cast<lua_Number>(gameOf(L).time()));
    return 1;
}

int gameEmit(lua_State* L)
{
    gameOf(L).emit(checkText(L, 1));
    return 0;
}

int gameWasEmitted(lua_State* L)
{
    lua_pushboolean(L, gameOf(L).wasEmitted(checkText(L, 1)));
    return 1;
}

void pushValue(lua_State* L, const Value& value)
{
    if (const bool* b = std::get_if<bool>(&value)) {
        lua_pushboolean(L, *b);
    } else if (const int* i = std::get_if<int>(&value)) {
        lua_pushinteger(L, *i);
    } else {
        pushText(L, std::get<std::string>(value));
    }
}

int gameGet(lua_State* L)
{
    const std::string_view name = checkText(L, 1);
    const auto value = gameOf(L).content().state().variable(name);
    if (!value) {
        return luaL_error(L, "there is no game variable called \"%s\" (variables are made in a .lvars file)",
                          std::string(name).c_str());
    }
    pushValue(L, *value);
    return 1;
}

int gameSet(lua_State* L)
{
    Game& game = gameOf(L);
    const std::string name(checkText(L, 1));
    const VariableDefinition* definition = game.content().content().variable(name);
    if (definition == nullptr) {
        return luaL_error(L, "there is no game variable called \"%s\" (variables are made in a .lvars file)",
                          name.c_str());
    }
    luaL_checkany(L, 2);
    Value value;
    switch (definition->type) {
    case ValueType::Bool:
        if (!lua_isboolean(L, 2)) {
            return luaL_error(L, "game variable \"%s\" is true or false, not a %s", name.c_str(),
                              luaL_typename(L, 2));
        }
        value = lua_toboolean(L, 2) != 0;
        break;
    case ValueType::Int: {
        int isInteger = 0;
        const lua_Integer number = lua_tointegerx(L, 2, &isInteger);
        if (isInteger == 0 || number < INT32_MIN || number > INT32_MAX) {
            return luaL_error(L, "game variable \"%s\" is a whole number, not %s", name.c_str(),
                              luaL_tolstring(L, 2, nullptr));
        }
        value = static_cast<int>(number);
        break;
    }
    case ValueType::Text:
        if (lua_type(L, 2) != LUA_TSTRING) {
            return luaL_error(L, "game variable \"%s\" is text, not a %s", name.c_str(), luaL_typename(L, 2));
        }
        value = std::string(checkText(L, 2));
        break;
    }
    game.content().state().setVariable(name, std::move(value));
    return 0;
}

NotationNames namesFor(Game& game)
{
    return projectNotationNames(game.assets(), game.isRunning() ? &game.scene() : nullptr);
}

// game.run("startDialogue elder") — any action the editor's action fields accept.
int gameRun(lua_State* L)
{
    Game& game = gameOf(L);
    const std::string_view text = checkText(L, 1);
    std::string error;
    const auto action = parseActionNotation(text, namesFor(game), &error);
    if (!action) {
        return luaL_error(L, "game.run: %s", error.c_str());
    }
    game.content().run({*action});
    return 0;
}

// game.check("gems >= 3") — any condition the editor's condition fields accept.
int gameCheck(lua_State* L)
{
    Game& game = gameOf(L);
    const std::string_view text = checkText(L, 1);
    std::string error;
    const auto condition = parseConditionNotation(text, namesFor(game), &error);
    if (!condition) {
        return luaL_error(L, "game.check: %s", error.c_str());
    }
    lua_pushboolean(L, game.content().evaluate(*condition));
    return 1;
}

int gameChangeScene(lua_State* L)
{
    Game& game = gameOf(L);
    const std::string_view name = checkText(L, 1);
    const std::string arriveAt = lua_isnoneornil(L, 2) ? std::string() : std::string(checkText(L, 2));
    std::string error;
    const NotationNames names = namesFor(game);
    const auto scene = names.findAsset ? names.findAsset(name, "scene", error) : std::nullopt;
    if (!scene) {
        return luaL_error(L, "game.changeScene: %s", error.empty() ? "no project is open" : error.c_str());
    }
    game.changeScene(*scene, arriveAt);
    return 0;
}

int gameSave(lua_State* L)
{
    lua_pushboolean(L, gameOf(L).save(checkText(L, 1)));
    return 1;
}

int gameLoad(lua_State* L)
{
    gameOf(L).load(checkText(L, 1));
    return 0;
}

int gameInDialogue(lua_State* L)
{
    lua_pushboolean(L, gameOf(L).isInDialogue());
    return 1;
}

// --- input.* ----------------------------------------------------------------------------------------------

// Key names as scripts write them: "a", "space", "left", "left shift" (or
// "leftshift", "left_shift"), "f1"..., and the mouse buttons "mouse left",
// "mouse right", "mouse middle". Case does not matter. "shift", "control"
// ("ctrl") and "alt" mean either side.
std::string normalizedKey(std::string_view name)
{
    std::string out;
    for (const char c : name) {
        if (c != ' ' && c != '_' && c != '-') {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

struct KeySet {
    std::array<Key, 2> keys{Key::Unknown, Key::Unknown};
    std::optional<MouseButton> button;
};

KeySet checkKey(lua_State* L, int index)
{
    const std::string wanted = normalizedKey(checkText(L, index));
    if (wanted == "shift") {
        return {{Key::LeftShift, Key::RightShift}, {}};
    }
    if (wanted == "control" || wanted == "ctrl") {
        return {{Key::LeftControl, Key::RightControl}, {}};
    }
    if (wanted == "alt") {
        return {{Key::LeftAlt, Key::RightAlt}, {}};
    }
    if (wanted == "return") {
        return {{Key::Enter, Key::Unknown}, {}};
    }
    if (wanted == "esc") {
        return {{Key::Escape, Key::Unknown}, {}};
    }
    if (wanted == "mouseleft") {
        return {{}, MouseButton::Left};
    }
    if (wanted == "mouseright") {
        return {{}, MouseButton::Right};
    }
    if (wanted == "mousemiddle") {
        return {{}, MouseButton::Middle};
    }
    for (int k = 1; k < static_cast<int>(Key::Count); ++k) {
        const Key key = static_cast<Key>(k);
        if (normalizedKey(keyName(key)) == wanted) {
            return {{key, Key::Unknown}, {}};
        }
    }
    luaL_error(L,
               "unknown key \"%s\"; use names like \"a\", \"space\", \"left\", \"left shift\", \"f1\" or "
               "\"mouse left\"",
               lua_tostring(L, index));
    return {};
}

template <bool (Input::*KeyQuery)(Key) const noexcept, bool (Input::*ButtonQuery)(MouseButton) const noexcept>
int inputQuery(lua_State* L)
{
    const KeySet set = checkKey(L, 1);
    const Input& input = std::as_const(gameOf(L)).input();
    bool result = false;
    if (set.button) {
        result = (input.*ButtonQuery)(*set.button);
    } else {
        for (const Key key : set.keys) {
            result = result || (key != Key::Unknown && (input.*KeyQuery)(key));
        }
    }
    lua_pushboolean(L, result);
    return 1;
}

int inputDirection(lua_State* L)
{
    const Vec2 d = std::as_const(gameOf(L)).input().moveDirection();
    lua_pushnumber(L, static_cast<lua_Number>(d.x));
    lua_pushnumber(L, static_cast<lua_Number>(d.y));
    return 2;
}

int inputPointer(lua_State* L)
{
    const Vec2 p = std::as_const(gameOf(L)).input().pointer();
    lua_pushnumber(L, static_cast<lua_Number>(p.x));
    lua_pushnumber(L, static_cast<lua_Number>(p.y));
    return 2;
}

// --- print and require ------------------------------------------------------------------------------------

// print(...) writes to the Console (the Log), with the script's file name.
int scriptPrint(lua_State* L)
{
    const int count = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= count; ++i) {
        std::size_t length = 0;
        const char* text = luaL_tolstring(L, i, &length);
        if (i > 1) {
            line += '\t';
        }
        line.append(text, length);
        lua_pop(L, 1);
    }
    ScriptHost& host = ScriptHost::of(L);
    const std::string& file = host.currentFile();
    host.log().info(kCategory, file.empty() ? line : file + ": " + line);
    return 0;
}

int scriptRequire(lua_State* L)
{
    ScriptHost::of(L).require(L, checkText(L, 1));
    return 1;
}

// --- Sound ---------------------------------------------------------------------------------------------

// A sound asset by the name the editor shows ("coin", or a path).
AssetId checkSound(lua_State* L, int index, const char* function)
{
    Game& game = gameOf(L);
    const std::string_view name = checkText(L, index);
    std::string error;
    const NotationNames names = namesFor(game);
    const auto sound = names.findAsset ? names.findAsset(name, "audio", error) : std::nullopt;
    if (!sound) {
        luaL_error(L, "%s: %s", function, error.empty() ? "no project is open" : error.c_str());
    }
    return *sound;
}

float checkRange(lua_State* L, int index, const char* what, float low, float high)
{
    const float value = checkFloat(L, index);
    if (value < low || value > high) {
        const auto text = [](float number) {
            std::ostringstream out;   // 0.5, 2: as people write them
            out << number;
            return out.str();
        };
        luaL_error(L, "%s must be from %s to %s, not %s", what, text(low).c_str(), text(high).c_str(),
                   text(value).c_str());
    }
    return value;
}

// The options table of audio.play and audio.playMusic: { volume = 0.5, ... }.
// `fade` is set only if the table has it (music only).
SoundOptions checkSoundOptions(lua_State* L, int index, bool music, float* fade)
{
    SoundOptions options;
    if (lua_isnoneornil(L, index)) {
        return options;
    }
    index = lua_absindex(L, index);
    luaL_checktype(L, index, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        const std::string key = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : std::string();
        const int value = lua_gettop(L);
        if (key == "volume") {
            options.volume = checkRange(L, value, "volume", 0.0f, 2.0f);
        } else if (key == "pitch" && !music) {
            options.pitch = checkRange(L, value, "pitch", 0.1f, 4.0f);
        } else if (key == "pan" && !music) {
            options.pan = checkRange(L, value, "pan", -1.0f, 1.0f);
        } else if (key == "loop" && !music) {
            luaL_checktype(L, value, LUA_TBOOLEAN);
            options.loop = lua_toboolean(L, value) != 0;
        } else if (key == "fade" && music && fade != nullptr) {
            *fade = checkRange(L, value, "fade", 0.0f, 30.0f);
        } else {
            luaL_error(L, "unknown option '%s' (known: %s)", key.c_str(),
                       music ? "volume, fade" : "volume, pitch, pan, loop");
        }
        lua_pop(L, 1);
    }
    return options;
}

SoundHandle checkHandle(lua_State* L, int index)
{
    const lua_Integer handle = luaL_checkinteger(L, index);
    return handle > 0 && handle <= 0xffffffff ? static_cast<SoundHandle>(handle) : 0;
}

// audio.play("coin", { volume = 0.5, pitch = 1.2, pan = 0, loop = false }) -> handle or nil
int audioPlay(lua_State* L)
{
    const AssetId sound = checkSound(L, 1, "audio.play");
    const SoundOptions options = checkSoundOptions(L, 2, false, nullptr);
    const SoundHandle handle = gameOf(L).playSound(sound, options);
    if (handle == 0) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, static_cast<lua_Integer>(handle));
    }
    return 1;
}

int audioStop(lua_State* L)
{
    if (!lua_isnil(L, 1)) {   // stopping nil (a sound that never played) is fine
        gameOf(L).sound().stop(checkHandle(L, 1));
    }
    return 0;
}

int audioIsPlaying(lua_State* L)
{
    lua_pushboolean(L, !lua_isnil(L, 1) && gameOf(L).sound().isPlaying(checkHandle(L, 1)));
    return 1;
}

// audio.playMusic("theme", { fade = 2, volume = 0.8 })
int audioPlayMusic(lua_State* L)
{
    const AssetId music = checkSound(L, 1, "audio.playMusic");
    float fade = 1.0f;
    const SoundOptions options = checkSoundOptions(L, 2, true, &fade);
    gameOf(L).playMusic(music, fade, options.volume);
    return 0;
}

int audioStopMusic(lua_State* L)
{
    const float fade = lua_isnoneornil(L, 1) ? 1.0f : checkRange(L, 1, "fade", 0.0f, 30.0f);
    gameOf(L).stopMusic(fade);
    return 0;
}

// audio.music() -> the music's name, or nil
int audioMusic(lua_State* L)
{
    Game& game = gameOf(L);
    const AssetId music = game.sound().music();
    if (music.isNull()) {
        lua_pushnil(L);
        return 1;
    }
    const NotationNames names = namesFor(game);
    const auto name = names.assetName ? names.assetName(music) : std::nullopt;
    pushText(L, name ? *name : music.toString());
    return 1;
}

SoundChannel checkChannel(lua_State* L, int index)
{
    static const char* const names[] = {"master", "music", "effects", nullptr};
    return static_cast<SoundChannel>(luaL_checkoption(L, index, nullptr, names));
}

// audio.setVolume("music", 0.5) — "master", "music" or "effects"
int audioSetVolume(lua_State* L)
{
    const SoundChannel channel = checkChannel(L, 1);
    gameOf(L).sound().setChannelVolume(channel, checkRange(L, 2, "volume", 0.0f, 1.0f));
    return 0;
}

int audioVolume(lua_State* L)
{
    lua_pushnumber(L, static_cast<lua_Number>(gameOf(L).sound().channelVolume(checkChannel(L, 1))));
    return 1;
}

// object:playSound() / object:stopSound(): the object's Sound component.
int objectPlaySound(lua_State* L)
{
    const Entity& entity = objectEntity(L, 1);
    if (entity.get<Sound>() == nullptr) {
        return luaL_error(L, "this object has no Sound component");
    }
    const SoundHandle handle = gameOf(L).playSound(entity.id());
    if (handle == 0) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, static_cast<lua_Integer>(handle));
    }
    return 1;
}

int objectStopSound(lua_State* L)
{
    gameOf(L).stopSound(objectEntity(L, 1).id());
    return 0;
}

void setFunctions(lua_State* L, const luaL_Reg* functions)
{
    luaL_setfuncs(L, functions, 0);
}

} // namespace

void pushObjectForScripts(lua_State* L, EntityId id)
{
    if (id.isNull()) {
        lua_pushnil(L);
        return;
    }
    auto* handle = static_cast<ObjectHandle*>(lua_newuserdatauv(L, sizeof(ObjectHandle), 0));
    handle->id = id.value();
    luaL_setmetatable(L, kObjectType);
}

void openScriptApi(ScriptHost& host)
{
    lua_State* L = host.state();

    // Objects: a metatable whose __index holds the methods.
    static const luaL_Reg objectMethods[] = {
        {"name", guarded<objectName>},
        {"isValid", guarded<objectIsValid>},
        {"position", guarded<objectPosition>},
        {"setPosition", guarded<objectSetPosition>},
        {"worldPosition", guarded<objectWorldPosition>},
        {"move", guarded<objectMove>},
        {"rotation", guarded<objectRotation>},
        {"setRotation", guarded<objectSetRotation>},
        {"scale", guarded<objectScale>},
        {"setScale", guarded<objectSetScale>},
        {"isActive", guarded<objectIsActive>},
        {"setActive", guarded<objectSetActive>},
        {"destroy", guarded<objectDestroy>},
        {"play", guarded<objectPlay>},
        {"stopAnimation", guarded<objectStopAnimation>},
        {"clip", guarded<objectClip>},
        {"isAnimationPlaying", guarded<objectIsAnimationPlaying>},
        {"isAnimationFinished", guarded<objectIsAnimationFinished>},
        {"overlapping", guarded<objectOverlapping>},
        {"isTouching", guarded<objectIsTouching>},
        {"text", guarded<objectText>},
        {"setText", guarded<objectSetText>},
        {"setFlipX", guarded<objectSetFlipX>},
        {"playSound", guarded<objectPlaySound>},
        {"stopSound", guarded<objectStopSound>},
        {nullptr, nullptr},
    };
    luaL_newmetatable(L, kObjectType);
    lua_newtable(L);
    setFunctions(L, objectMethods);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, guarded<objectEquals>);
    lua_setfield(L, -2, "__eq");
    lua_pushcfunction(L, guarded<objectToString>);
    lua_setfield(L, -2, "__tostring");
    lua_pushliteral(L, "object");
    lua_setfield(L, -2, "__name");
    lua_pop(L, 1);

    static const luaL_Reg gameFunctions[] = {
        {"find", guarded<gameFind>},
        {"player", guarded<gamePlayer>},
        {"time", guarded<gameTime>},
        {"emit", guarded<gameEmit>},
        {"wasEmitted", guarded<gameWasEmitted>},
        {"get", guarded<gameGet>},
        {"set", guarded<gameSet>},
        {"run", guarded<gameRun>},
        {"check", guarded<gameCheck>},
        {"changeScene", guarded<gameChangeScene>},
        {"save", guarded<gameSave>},
        {"load", guarded<gameLoad>},
        {"inDialogue", guarded<gameInDialogue>},
        {nullptr, nullptr},
    };
    lua_newtable(L);
    setFunctions(L, gameFunctions);
    lua_setglobal(L, "game");

    static const luaL_Reg inputFunctions[] = {
        {"isDown", guarded<inputQuery<&Input::isDown, &Input::isDown>>},
        {"wasPressed", guarded<inputQuery<&Input::wasPressed, &Input::wasPressed>>},
        {"wasReleased", guarded<inputQuery<&Input::wasReleased, &Input::wasReleased>>},
        {"direction", guarded<inputDirection>},
        {"pointer", guarded<inputPointer>},
        {nullptr, nullptr},
    };
    lua_newtable(L);
    setFunctions(L, inputFunctions);
    lua_setglobal(L, "input");

    static const luaL_Reg audioFunctions[] = {
        {"play", guarded<audioPlay>},
        {"stop", guarded<audioStop>},
        {"isPlaying", guarded<audioIsPlaying>},
        {"playMusic", guarded<audioPlayMusic>},
        {"stopMusic", guarded<audioStopMusic>},
        {"music", guarded<audioMusic>},
        {"setVolume", guarded<audioSetVolume>},
        {"volume", guarded<audioVolume>},
        {nullptr, nullptr},
    };
    lua_newtable(L);
    setFunctions(L, audioFunctions);
    lua_setglobal(L, "audio");

    lua_pushcfunction(L, guarded<scriptPrint>);
    lua_setglobal(L, "print");
    lua_pushcfunction(L, guarded<scriptRequire>);
    lua_setglobal(L, "require");
}

} // namespace rolo
