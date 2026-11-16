#include "rolo/gameplay/ScriptProperties.hpp"

#include <Luau/Allocator.h>
#include <Luau/Ast.h>
#include <Luau/Lexer.h>
#include <Luau/Parser.h>

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
        const auto value = plainValue(item.value);
        if (!value) {
            out.problems.push_back(lineOf(*item.value) + ": '" + name + "' is not a plain value. The Inspector can show "
                                   "a number, a text in quotes, or true / false.");
            continue;
        }
        out.list.push_back({name, *value, static_cast<int>(item.value->location.begin.line) + 1});
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
    if (const auto found = own.find(property.name); found != own.end() && found->second.index() == property.value.index()) {
        return found->second;
    }
    return property.value;
}

std::string withScriptPropertyValue(std::string_view values, const ScriptProperty& property, const DataValue& value)
{
    DataValues own = parseDataValues(values);
    if (value == property.value || value.index() != property.value.index()) {
        own.erase(property.name);
    } else {
        own[property.name] = value;
    }
    return writeDataValues(own);
}

} // namespace rolo
