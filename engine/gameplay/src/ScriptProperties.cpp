#include "rolo/gameplay/ScriptProperties.hpp"

#include <Luau/Allocator.h>
#include <Luau/Ast.h>
#include <Luau/Lexer.h>
#include <Luau/Parser.h>

#include "rolo/scene/EntityId.hpp"
#include "rolo/scene/Types.hpp"

#include <cctype>
#include <cstring>

namespace rolo {

namespace {
constexpr const char* kTableName = "properties";

std::string lineOf(const Luau::AstNode& node)
{
    return "line " + std::to_string(node.location.begin.line + 1);
}

// A value written out plainly: 90, -1.5, "text", true.
std::optional<DataValue> plainValue(Luau::AstExpr* expr)
{
    if (const auto* number = expr->as<Luau::AstExprConstantNumber>()) {
        return DataValue(number->value);
    }
    if (const auto* flag = expr->as<Luau::AstExprConstantBool>()) {
        return DataValue(flag->value);
    }
    if (const auto* text = expr->as<Luau::AstExprConstantString>()) {
        return DataValue(std::string(text->value.data, text->value.size));
    }
    if (const auto* unary = expr->as<Luau::AstExprUnary>(); unary != nullptr && unary->op == Luau::AstExprUnary::Op::Minus) {
        if (const auto* number = unary->expr->as<Luau::AstExprConstantNumber>()) {
            return DataValue(-number->value);
        }
    }
    return std::nullopt;
}

std::optional<double> plainNumber(Luau::AstExpr* expr)
{
    if (const auto value = plainValue(expr); value && std::holds_alternative<double>(*value)) {
        return std::get<double>(*value);
    }
    return std::nullopt;
}

bool isGlobal(const Luau::AstExpr* expr, const char* name)
{
    const auto* global = expr->as<Luau::AstExprGlobal>();
    return global != nullptr && global->name.value != nullptr && std::strcmp(global->name.value, name) == 0;
}

struct Read {
    ScriptProperty::Kind kind;
    DataValue value;
};

// What one property's value says, or why the Inspector cannot show it.
std::optional<Read> readValue(Luau::AstExpr* expr, std::string& why)
{
    if (const auto plain = plainValue(expr)) {
        const auto kind = std::holds_alternative<bool>(*plain)     ? ScriptProperty::Kind::Flag
                          : std::holds_alternative<double>(*plain) ? ScriptProperty::Kind::Number
                                                                   : ScriptProperty::Kind::Text;
        return Read{kind, *plain};
    }
    const auto* call = expr->as<Luau::AstExprCall>();
    if (call != nullptr && isGlobal(call->func, "color")) {
        // color("#ffb84d")
        const auto* text = call->args.size == 1 ? call->args.data[0]->as<Luau::AstExprConstantString>() : nullptr;
        const auto colour = text != nullptr ? Color::fromHex(std::string_view(text->value.data, text->value.size))
                                            : std::nullopt;
        if (!colour) {
            why = "is not a colour the Inspector can show. Write it like color(\"#ffb84d\").";
            return std::nullopt;
        }
        return Read{ScriptProperty::Kind::Color, DataValue(colour->toHex())};
    }
    if (call != nullptr && isGlobal(call->func, "object")) {
        // object(): another object, none until one is chosen
        if (call->args.size != 0) {
            why = "names an object. Write object() with nothing in the brackets, and choose the object in the Inspector.";
            return std::nullopt;
        }
        return Read{ScriptProperty::Kind::Object, DataValue(std::string())};
    }
    if (const auto* index = call != nullptr ? call->func->as<Luau::AstExprIndexName>() : nullptr;
        index != nullptr && isGlobal(index->expr, "vector") && index->index.value != nullptr
        && std::strcmp(index->index.value, "create") == 0) {
        // vector.create(x, y): a point (a third number is allowed and ignored, as everywhere in Rolo)
        const auto x = call->args.size >= 2 ? plainNumber(call->args.data[0]) : std::nullopt;
        const auto y = call->args.size >= 2 ? plainNumber(call->args.data[1]) : std::nullopt;
        if (!x || !y || call->args.size > 3) {
            why = "is not a point the Inspector can show. Write it with two plain numbers, like vector.create(300, 120).";
            return std::nullopt;
        }
        return Read{ScriptProperty::Kind::Point, DataValue(Vec2{static_cast<float>(*x), static_cast<float>(*y)})};
    }
    why = "is not a plain value. The Inspector can show a number, a text in quotes, true / false, "
          "a colour (color(\"#ffb84d\")), a point (vector.create(x, y)) or another object (object()).";
    return std::nullopt;
}

// Whether `value` can be an object's own value for `property`.
bool fits(const ScriptProperty& property, const DataValue& value)
{
    if (value.index() != property.value.index()) {
        return false;
    }
    const std::string* text = std::get_if<std::string>(&value);
    if (property.kind == ScriptProperty::Kind::Color) {
        return text != nullptr && Color::fromHex(*text).has_value();
    }
    if (property.kind == ScriptProperty::Kind::Object) {
        return text != nullptr && (text->empty() || EntityId::parse(*text).has_value());
    }
    return true;
}

void readTable(const Luau::AstExprTable& table, ScriptProperties& out)
{
    for (const Luau::AstExprTable::Item& item : table.items) {
        const auto* key = item.key != nullptr ? item.key->as<Luau::AstExprConstantString>() : nullptr;
        if (item.kind == Luau::AstExprTable::Item::Kind::List || key == nullptr) {
            out.problems.push_back(lineOf(*item.value) + ": each property needs a name, like speed = 90.");
            continue;
        }
        const std::string name(key->value.data, key->value.size);
        if (name == "object") {
            out.problems.push_back(lineOf(*item.value) + ": a property cannot be called 'object' (self.object is the "
                                                         "script's object).");
            continue;
        }
        std::string why;
        const auto read = readValue(item.value, why);
        if (!read) {
            out.problems.push_back(lineOf(*item.value) + ": '" + name + "' " + why);
            continue;
        }
        out.list.push_back({name, read->value, static_cast<int>(item.value->location.begin.line) + 1, read->kind});
    }
}
} // namespace

ScriptProperties readScriptProperties(std::string_view code)
{
    ScriptProperties out;
    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    const Luau::ParseResult parsed = Luau::Parser::parse(code.data(), code.size(), names, allocator);
    if (!parsed.errors.empty() || parsed.root == nullptr) {
        return out;
    }
    // Only at the top of the file, outside every function: that is where the
    // running game looks as well.
    for (Luau::AstStat* statement : parsed.root->body) {
        if (const auto* local = statement->as<Luau::AstStatLocal>()) {
            for (const Luau::AstLocal* variable : local->vars) {
                if (variable->name.value != nullptr && std::strcmp(variable->name.value, kTableName) == 0) {
                    out.problems.push_back("line " + std::to_string(variable->location.begin.line + 1)
                                           + ": write properties = { ... } without 'local', or Rolo cannot see it.");
                }
            }
            continue;
        }
        const auto* assign = statement->as<Luau::AstStatAssign>();
        if (assign == nullptr || assign->vars.size != 1 || assign->values.size != 1) {
            continue;
        }
        const auto* global = assign->vars.data[0]->as<Luau::AstExprGlobal>();
        if (global == nullptr || global->name.value == nullptr || std::strcmp(global->name.value, kTableName) != 0) {
            continue;
        }
        const auto* table = assign->values.data[0]->as<Luau::AstExprTable>();
        if (table == nullptr) {
            out.problems.push_back(lineOf(*assign) + ": properties must be a list in braces: properties = { speed = 90 }.");
            continue;
        }
        out.list.clear();   // (written twice: the last one counts, as when it runs)
        readTable(*table, out);
    }
    return out;
}

std::string scriptPropertyLabel(std::string_view name)
{
    std::string label;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '_') {
            if (!label.empty() && label.back() != ' ') {
                label += ' ';
            }
            continue;
        }
        const bool upper = std::isupper(static_cast<unsigned char>(c)) != 0;
        const bool afterLower = i > 0 && std::islower(static_cast<unsigned char>(name[i - 1])) != 0;
        if (upper && afterLower) {
            label += ' ';   // walkSpeed: a new word
        }
        const bool startsWord = label.empty() || label.back() == ' ';
        label += startsWord ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
    }
    return label;
}

DataValue scriptPropertyValue(const ScriptProperty& property, std::string_view values)
{
    const DataValues own = parseDataValues(values);
    if (const auto found = own.find(property.name); found != own.end() && fits(property, found->second)) {
        if (property.kind == ScriptProperty::Kind::Color) {
            return DataValue(Color::fromHex(std::get<std::string>(found->second))->toHex());   // always "#rrggbbaa"
        }
        return found->second;
    }
    return property.value;
}

std::string withScriptPropertyValue(std::string_view values, const ScriptProperty& property, const DataValue& value)
{
    DataValues own = parseDataValues(values);
    DataValue wanted = value;
    if (property.kind == ScriptProperty::Kind::Color && fits(property, value)) {
        wanted = Color::fromHex(std::get<std::string>(value))->toHex();
    }
    if (wanted == property.value || !fits(property, wanted)) {
        own.erase(property.name);
    } else {
        own[property.name] = wanted;
    }
    return writeDataValues(own);
}

} // namespace rolo
