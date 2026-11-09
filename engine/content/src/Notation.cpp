#include "rolo/content/Notation.hpp"

#include "rolo/content/Content.hpp"   // isValidContentId
#include "rolo/core/Numbers.hpp"
#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/scene/Scene.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <vector>

namespace rolo {

namespace {

// --- Tokens --------------------------------------------------------------------------------------

struct Token {
    enum class Kind { Word, Number, Text, Operator, Open, Close, End };
    Kind kind = Kind::End;
    std::string text;   // the word, number, unquoted text or operator
};

bool isWordChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.' || c == '/' || c == '-';
}

std::optional<std::vector<Token>> tokenize(std::string_view s, std::string& error)
{
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            ++i;
        } else if (c == '(') {
            out.push_back({Token::Kind::Open, "("});
            ++i;
        } else if (c == ')') {
            out.push_back({Token::Kind::Close, ")"});
            ++i;
        } else if (c == '"') {
            std::string text;
            ++i;
            bool closed = false;
            while (i < s.size()) {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    text += s[i + 1];
                    i += 2;
                } else if (s[i] == '"') {
                    closed = true;
                    ++i;
                    break;
                } else {
                    text += s[i++];
                }
            }
            if (!closed) {
                error = "a text in quotes is not closed with \"";
                return std::nullopt;
            }
            out.push_back({Token::Kind::Text, std::move(text)});
        } else if (c == '=' || c == '!' || c == '<' || c == '>') {
            std::string op(1, c);
            if (i + 1 < s.size() && s[i + 1] == '=') {
                op += '=';
            }
            if (op == "=" || op == "!") {
                error = "\"" + op + "\" is not an operator; use == or !=";
                return std::nullopt;
            }
            out.push_back({Token::Kind::Operator, op});
            i += op.size();
        } else if (isWordChar(c)) {
            std::size_t j = i;
            while (j < s.size() && isWordChar(s[j])) {
                ++j;
            }
            std::string word(s.substr(i, j - i));
            // A number: optional minus, digits, optional decimals.
            const bool numeric = !word.empty()
                                 && (std::isdigit(static_cast<unsigned char>(word[0])) != 0
                                     || (word.size() > 1 && word[0] == '-' && std::isdigit(static_cast<unsigned char>(word[1])) != 0))
                                 && word.find_first_not_of("-0123456789.") == std::string::npos && word.size() != 16;
            out.push_back({numeric ? Token::Kind::Number : Token::Kind::Word, std::move(word)});
            i = j;
        } else {
            error = std::string("unexpected '") + c + "'";
            return std::nullopt;
        }
    }
    out.push_back({Token::Kind::End, ""});
    return out;
}

bool isHexId(std::string_view s)
{
    return s.size() == 16 && s.find_first_not_of("0123456789abcdefABCDEF") == std::string_view::npos;
}

std::string quote(std::string_view text)
{
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

/// A word when it can be written bare, else in quotes.
std::string wordOrQuoted(std::string_view text)
{
    if (!text.empty() && std::all_of(text.begin(), text.end(), isWordChar) && !std::isdigit(static_cast<unsigned char>(text[0]))) {
        return std::string(text);
    }
    return quote(text);
}

std::string formatValue(const Value& v)
{
    if (const auto* text = std::get_if<std::string>(&v)) {
        return quote(*text);
    }
    return toDisplayString(v);
}

// --- Reading -------------------------------------------------------------------------------------


// How an asset kind is named in messages: audio assets are "sounds".
std::string_view kindLabel(std::string_view kind)
{
    return kind == "audio" ? std::string_view("sound") : kind;
}

class Parser {
public:
    Parser(std::vector<Token> tokens, const NotationNames& names)
        : m_tokens(std::move(tokens))
        , m_names(names)
    {
    }

    std::string error;

    const Token& peek() const { return m_tokens[m_pos]; }
    bool atEnd() const { return peek().kind == Token::Kind::End; }
    Token next() { return atEnd() ? peek() : m_tokens[m_pos++]; }
    bool isWord(std::string_view w) const { return peek().kind == Token::Kind::Word && peek().text == w; }
    bool fail(std::string message)
    {
        if (error.empty()) {
            error = std::move(message);
        }
        return false;
    }
    std::string describeNext() const
    {
        if (atEnd()) {
            return "the end";
        }
        return "\"" + peek().text + "\"";
    }

    std::optional<std::string> identifier(const char* what)
    {
        if (peek().kind != Token::Kind::Word || !isValidContentId(peek().text)) {
            fail(std::string("expected ") + what + ", not " + describeNext());
            return std::nullopt;
        }
        return next().text;
    }

    std::optional<Value> value()
    {
        const Token t = next();
        if (t.kind == Token::Kind::Text) {
            return Value{t.text};
        }
        if (t.kind == Token::Kind::Word && (t.text == "true" || t.text == "false")) {
            return Value{t.text == "true"};
        }
        if (t.kind == Token::Kind::Number) {
            int n = 0;
            const auto [p, ec] = std::from_chars(t.text.data(), t.text.data() + t.text.size(), n);
            if (ec == std::errc{} && p == t.text.data() + t.text.size()) {
                return Value{n};
            }
        }
        fail("expected a value (true, false, a whole number or \"text\"), not "
             + (t.kind == Token::Kind::End ? std::string("the end") : "\"" + t.text + "\""));
        return std::nullopt;
    }

    std::optional<AssetId> asset(std::string_view kind)
    {
        const Token t = next();
        const std::string what(kindLabel(kind));
        if (t.kind != Token::Kind::Word && t.kind != Token::Kind::Text) {
            fail("expected the name of a " + what + ", not "
                 + (t.kind == Token::Kind::End ? std::string("the end") : "\"" + t.text + "\""));
            return std::nullopt;
        }
        if (isHexId(t.text)) {
            return AssetId::parse(t.text);
        }
        std::string reason = "there is no " + what + " called \"" + t.text + "\"";
        if (m_names.findAsset) {
            if (auto id = m_names.findAsset(t.text, kind, reason)) {
                return id;
            }
        }
        fail(reason);
        return std::nullopt;
    }

    std::optional<EntityId> entity()
    {
        const Token t = next();
        if (t.kind == Token::Kind::Word && t.text == "this") {
            return EntityId{};
        }
        if (t.kind != Token::Kind::Word && t.kind != Token::Kind::Text) {
            fail("expected an object's name (or this), not "
                 + (t.kind == Token::Kind::End ? std::string("the end") : "\"" + t.text + "\""));
            return std::nullopt;
        }
        if (isHexId(t.text)) {
            return EntityId::parse(t.text);
        }
        std::string reason = "there is no object called \"" + t.text + "\" here";
        if (m_names.findEntity) {
            if (auto id = m_names.findEntity(t.text, reason)) {
                return id;
            }
        }
        fail(reason);
        return std::nullopt;
    }

    // --- Conditions ------------------------------------------------------------------------------
    std::optional<Condition> anyOf(int depth)
    {
        return chain(depth, "or", Condition::Kind::Any);
    }

    std::optional<Condition> chain(int depth, std::string_view word, Condition::Kind kind)
    {
        std::vector<Condition> parts;
        do {
            auto part = kind == Condition::Kind::Any ? chain(depth, "and", Condition::Kind::All) : unary(depth);
            if (!part) {
                return std::nullopt;
            }
            parts.push_back(std::move(*part));
        } while (isWord(word) && (next(), true));
        if (parts.size() == 1) {
            return std::move(parts.front());
        }
        Condition c;
        c.kind = kind;
        c.children = std::move(parts);
        return c;
    }

    std::optional<Condition> unary(int depth)
    {
        if (depth > 32) {
            fail("the condition is nested too deeply");
            return std::nullopt;
        }
        if (isWord("not")) {
            next();
            auto inner = unary(depth + 1);
            if (!inner) {
                return std::nullopt;
            }
            Condition c;
            c.kind = Condition::Kind::Not;
            c.children.push_back(std::move(*inner));
            return c;
        }
        if (peek().kind == Token::Kind::Open) {
            next();
            auto inner = anyOf(depth + 1);
            if (!inner) {
                return std::nullopt;
            }
            if (peek().kind != Token::Kind::Close) {
                fail("expected ) to close the (, not " + describeNext());
                return std::nullopt;
            }
            next();
            return inner;
        }
        return atom();
    }

    std::optional<Condition> atom()
    {
        Condition c;
        if (isWord("always")) {
            next();
            return c;
        }
        if (isWord("quest")) {
            next();
            const auto quest = asset("quest");
            if (!quest) {
                return std::nullopt;
            }
            if (!isWord("is")) {
                fail("expected \"is\" after the quest, as in: quest gems is completed");
                return std::nullopt;
            }
            next();
            const Token state = next();
            const auto s = questStateFromName(state.text);
            if (!s) {
                fail("expected notStarted, active, completed or failed, not \"" + state.text + "\"");
                return std::nullopt;
            }
            c.kind = Condition::Kind::QuestIs;
            c.quest = *quest;
            c.state = *s;
            return c;
        }
        if (isWord("objective")) {
            next();
            const auto quest = asset("quest");
            const auto objective = quest ? identifier("the objective's id") : std::nullopt;
            if (!objective) {
                return std::nullopt;
            }
            if (isWord("done")) {
                next();
            }
            c.kind = Condition::Kind::ObjectiveDone;
            c.quest = *quest;
            c.objective = *objective;
            return c;
        }
        const auto variable = identifier("a variable, quest, objective, not or (");
        if (!variable) {
            return std::nullopt;
        }
        c.kind = Condition::Kind::Compare;
        c.variable = *variable;
        if (peek().kind != Token::Kind::Operator) {
            c.value = true;   // a bare variable: "met_elder" means met_elder == true
            return c;
        }
        const std::string op = next().text;
        static constexpr std::pair<std::string_view, CompareOp> kOps[] = {
            {"==", CompareOp::Equal}, {"!=", CompareOp::NotEqual},     {"<", CompareOp::Less},
            {"<=", CompareOp::LessEqual}, {">", CompareOp::Greater}, {">=", CompareOp::GreaterEqual}};
        for (const auto& [name, value] : kOps) {
            if (name == op) {
                c.op = value;
            }
        }
        const auto v = value();
        if (!v) {
            return std::nullopt;
        }
        c.value = *v;
        return c;
    }

private:
    std::vector<Token> m_tokens;
    std::size_t m_pos = 0;
    const NotationNames& m_names;
};

std::string assetText(AssetId id, const NotationNames& names)
{
    if (names.assetName) {
        if (auto name = names.assetName(id)) {
            return wordOrQuoted(*name);
        }
    }
    return id.toString();
}

std::string entityText(EntityId id, const NotationNames& names)
{
    if (id.isNull()) {
        return "this";
    }
    if (names.entityName) {
        if (auto name = names.entityName(id)) {
            return wordOrQuoted(*name);
        }
    }
    return id.toString();
}

std::string formatCondition(const Condition& c, const NotationNames& names, bool nested)
{
    switch (c.kind) {
    case Condition::Kind::Always: return "always";
    case Condition::Kind::Compare:
        if (std::holds_alternative<bool>(c.value) && std::get<bool>(c.value) && c.op == CompareOp::Equal) {
            return c.variable;
        }
        return c.variable + " " + std::string(compareOpName(c.op)) + " " + formatValue(c.value);
    case Condition::Kind::QuestIs:
        return "quest " + assetText(c.quest, names) + " is " + std::string(questStateName(c.state));
    case Condition::Kind::ObjectiveDone:
        return "objective " + assetText(c.quest, names) + " " + c.objective + " done";
    case Condition::Kind::All:
    case Condition::Kind::Any: {
        if (c.children.empty()) {
            return c.kind == Condition::Kind::All ? "always" : "not always";
        }
        std::string out;
        for (const Condition& child : c.children) {
            const bool group = child.kind == Condition::Kind::All || child.kind == Condition::Kind::Any;
            out += (out.empty() ? "" : (c.kind == Condition::Kind::All ? " and " : " or "))
                   + formatCondition(child, names, group);
        }
        return nested ? "(" + out + ")" : out;
    }
    case Condition::Kind::Not: {
        if (c.children.empty()) {
            return "not always";
        }
        const Condition& inner = c.children.front();
        const bool simple = inner.kind == Condition::Kind::Always || inner.kind == Condition::Kind::Not
                            || (inner.kind == Condition::Kind::Compare && std::holds_alternative<bool>(inner.value)
                                && std::get<bool>(inner.value) && inner.op == CompareOp::Equal);
        const std::string text = formatCondition(inner, names, true);
        return "not " + (simple || text.front() == '(' ? text : "(" + text + ")");
    }
    }
    return "always";
}

} // namespace

// --- Public ----------------------------------------------------------------------------------------

std::string formatCondition(const Condition& condition, const NotationNames& names)
{
    return formatCondition(condition, names, false);
}

std::optional<Condition> parseConditionNotation(std::string_view text, const NotationNames& names, std::string* error)
{
    std::string message;
    auto tokens = tokenize(text, message);
    std::optional<Condition> result;
    if (tokens) {
        if (tokens->size() == 1) {
            return Condition{};   // empty: always
        }
        Parser p(std::move(*tokens), names);
        result = p.anyOf(0);
        if (result && !p.atEnd()) {
            p.fail("expected \"and\", \"or\" or the end, not " + p.describeNext());
            result.reset();
        }
        message = p.error;
    }
    if (!result && error != nullptr) {
        *error = message;
    }
    return result;
}

std::string formatAction(const Action& a, const NotationNames& names)
{
    const std::string type(actionTypeName(a.kind));
    switch (a.kind) {
    case Action::Kind::Set: return type + " " + a.variable + " " + formatValue(a.value);
    case Action::Kind::Add: return type + " " + a.variable + " " + formatValue(a.value);
    case Action::Kind::StartQuest:
    case Action::Kind::FailQuest: return type + " " + assetText(a.quest, names);
    case Action::Kind::CompleteObjective: return type + " " + assetText(a.quest, names) + " " + a.objective;
    case Action::Kind::StartDialogue:
    case Action::Kind::ChangeScene:
    case Action::Kind::PlaySound:
    case Action::Kind::PlayMusic: return type + " " + assetText(a.asset, names);
    case Action::Kind::StopMusic: return type;
    case Action::Kind::SetActive: return type + " " + entityText(a.entity, names) + (a.active ? " on" : " off");
    case Action::Kind::Destroy: return type + " " + entityText(a.entity, names);
    case Action::Kind::Emit: return type + " " + wordOrQuoted(a.text);
    case Action::Kind::Log: return type + " " + quote(a.text);
    case Action::Kind::Wait: {
        std::string seconds = std::to_string(a.seconds);
        seconds.erase(seconds.find_last_not_of('0') + 1);
        if (!seconds.empty() && seconds.back() == '.') {
            seconds.pop_back();
        }
        return type + " " + seconds;
    }
    case Action::Kind::SaveGame:
    case Action::Kind::LoadGame: return type + " " + wordOrQuoted(a.text);
    }
    return type;
}

std::optional<Action> parseActionNotation(std::string_view text, const NotationNames& names, std::string* error)
{
    std::string message;
    std::optional<Action> result;
    auto tokens = tokenize(text, message);
    if (tokens) {
        Parser p(std::move(*tokens), names);
        const Token verb = p.next();
        Action a;
        bool known = false;
        for (int k = 0; k <= static_cast<int>(Action::Kind::StopMusic); ++k) {
            if (actionTypeName(static_cast<Action::Kind>(k)) == verb.text) {
                a.kind = static_cast<Action::Kind>(k);
                known = true;
            }
        }
        bool ok = known;
        if (!known) {
            p.fail(verb.kind == Token::Kind::End ? std::string("write an action, e.g. add gems 1")
                                                 : "\"" + verb.text + "\" is not an action (known: set, add, startQuest, "
                                                   "completeObjective, failQuest, startDialogue, changeScene, setActive, "
                                                   "destroy, emit, log, wait, saveGame, loadGame, playSound, playMusic, stopMusic)");
        }
        const auto word = [&](const char* what) -> std::optional<std::string> {
            const Token t = p.next();
            if ((t.kind == Token::Kind::Word || t.kind == Token::Kind::Text) && !t.text.empty()) {
                return t.text;
            }
            p.fail(std::string("expected ") + what);
            return std::nullopt;
        };
        if (ok) {
            switch (a.kind) {
            case Action::Kind::Set:
            case Action::Kind::Add: {
                const auto variable = p.identifier("a variable");
                const auto value = variable ? p.value() : std::nullopt;
                ok = value.has_value();
                if (ok && a.kind == Action::Kind::Add && !std::holds_alternative<int>(*value)) {
                    ok = p.fail("add needs a whole number, e.g. add gems 1");
                }
                if (ok) {
                    a.variable = *variable;
                    a.value = *value;
                }
                break;
            }
            case Action::Kind::StartQuest:
            case Action::Kind::FailQuest:
            case Action::Kind::CompleteObjective: {
                const auto quest = p.asset("quest");
                ok = quest.has_value();
                if (ok) {
                    a.quest = *quest;
                }
                if (ok && a.kind == Action::Kind::CompleteObjective) {
                    const auto objective = p.identifier("the objective's id");
                    ok = objective.has_value();
                    if (ok) {
                        a.objective = *objective;
                    }
                }
                break;
            }
            case Action::Kind::StartDialogue:
            case Action::Kind::ChangeScene: {
                const auto asset = p.asset(a.kind == Action::Kind::StartDialogue ? "dialogue" : "scene");
                ok = asset.has_value();
                if (ok) {
                    a.asset = *asset;
                }
                break;
            }
            case Action::Kind::PlaySound:
            case Action::Kind::PlayMusic: {
                const auto asset = p.asset("audio");
                ok = asset.has_value();
                if (ok) {
                    a.asset = *asset;
                }
                break;
            }
            case Action::Kind::StopMusic: break;
            case Action::Kind::SetActive:
            case Action::Kind::Destroy: {
                const auto entity = p.entity();
                ok = entity.has_value();
                if (ok) {
                    a.entity = *entity;
                }
                if (ok && a.kind == Action::Kind::SetActive) {
                    if (p.isWord("on") || p.isWord("off")) {
                        a.active = p.next().text == "on";
                    } else {
                        ok = p.fail("expected on or off, as in: setActive Door off");
                    }
                }
                break;
            }
            case Action::Kind::Emit:
            case Action::Kind::SaveGame:
            case Action::Kind::LoadGame: {
                const auto name = word(a.kind == Action::Kind::Emit ? "the event's name" : "the save slot's name");
                ok = name.has_value();
                if (ok) {
                    a.text = *name;
                }
                break;
            }
            case Action::Kind::Log: {
                const Token t = p.next();
                ok = t.kind == Token::Kind::Text || p.fail("log needs text in quotes, e.g. log \"Hello\"");
                if (ok) {
                    a.text = t.text;
                }
                break;
            }
            case Action::Kind::Wait: {
                const Token t = p.next();
                double seconds = -1.0;
                if (t.kind == Token::Kind::Number) {
                    // rolo::parseNumber, not std::from_chars: Apple's standard
                    // library has no from_chars for decimals (see Numbers.hpp).
                    seconds = parseNumber(t.text).value_or(-1.0);
                }
                ok = (seconds >= 0.0 && seconds <= 3600.0) || p.fail("wait needs seconds from 0 to 3600, e.g. wait 1.5");
                if (ok) {
                    a.seconds = static_cast<float>(seconds);
                }
                break;
            }
            }
        }
        if (ok && !p.atEnd()) {
            ok = p.fail("unexpected " + p.describeNext() + " at the end");
        }
        if (ok) {
            result = std::move(a);
        }
        message = p.error;
    }
    if (!result && error != nullptr) {
        *error = message;
    }
    return result;
}

// --- Names in a project ----------------------------------------------------------------------------

namespace {
std::string fileStemOf(const std::string& path)
{
    std::string name = path.substr(path.rfind('/') + 1);
    return name.substr(0, name.rfind('.'));
}
} // namespace

NotationNames projectNotationNames(const AssetDatabase* assets, const Scene* scene)
{
    NotationNames names;
    if (assets != nullptr) {
        // Assets by file name ("gems"), or by path when two share a name.
        names.assetName = [assets](AssetId id) -> std::optional<std::string> {
            const AssetRecord* a = assets->find(id);
            if (a == nullptr) {
                return std::nullopt;
            }
            const std::string stem = fileStemOf(a->path);
            const auto all = assets->all(a->type);
            const auto same =
                std::count_if(all.begin(), all.end(), [&](const AssetRecord* o) { return fileStemOf(o->path) == stem; });
            return same == 1 ? stem : a->path;
        };
        names.findAsset = [assets](std::string_view name, std::string_view kind,
                                   std::string& error) -> std::optional<AssetId> {
            const auto type = assetTypeFromName(kind);
            if (!type) {
                return std::nullopt;
            }
            std::vector<const AssetRecord*> matches;
            for (const AssetRecord* a : assets->all(*type)) {
                if (a->path == name || fileStemOf(a->path) == name) {
                    matches.push_back(a);
                }
            }
            if (matches.size() == 1) {
                return matches.front()->id;
            }
            const std::string what(kindLabel(kind));
            error = matches.empty() ? "there is no " + what + " called \"" + std::string(name) + "\""
                                    : "several " + what + "s are called \"" + std::string(name)
                                          + "\"; write the path, e.g. \"" + matches.front()->path + "\"";
            return std::nullopt;
        };
    }
    if (scene != nullptr) {
        names.entityName = [scene](EntityId id) -> std::optional<std::string> {
            const Entity* e = scene->find(id);
            if (e == nullptr) {
                return std::nullopt;
            }
            int same = 0;
            scene->forEach([&](const Entity& o, int) { same += o.name() == e->name() ? 1 : 0; });
            return same == 1 ? std::optional<std::string>(e->name()) : std::nullopt;
        };
        names.findEntity = [scene](std::string_view name, std::string& error) -> std::optional<EntityId> {
            std::vector<EntityId> matches;
            scene->forEach([&](const Entity& e, int) {
                if (e.name() == name) {
                    matches.push_back(e.id());
                }
            });
            if (matches.size() == 1) {
                return matches.front();
            }
            error = matches.empty() ? "there is no object called \"" + std::string(name) + "\" in this scene"
                                    : "several objects are called \"" + std::string(name) + "\"; rename one";
            return std::nullopt;
        };
    }
    return names;
}

} // namespace rolo
