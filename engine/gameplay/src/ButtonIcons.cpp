#include "rolo/gameplay/ButtonIcons.hpp"

namespace rolo {

namespace {

constexpr std::string_view kStyleNames[] = {"generic", "xbox", "playstation", "nintendo"};

std::string_view styleName(GamepadStyle style)
{
    return kStyleNames[static_cast<std::size_t>(style)];
}

std::optional<GamepadStyle> styleFromName(std::string_view name)
{
    for (std::size_t i = 0; i < std::size(kStyleNames); ++i) {
        if (kStyleNames[i] == name) {
            return static_cast<GamepadStyle>(i);
        }
    }
    return std::nullopt;
}

// "{icon:pad:A:xbox}" -> the icon (nothing if it is not one).
std::optional<ButtonIcon> parseToken(std::string_view inside)
{
    // `inside` is what is between "{icon:" and "}".
    const std::size_t colon = inside.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view kind = inside.substr(0, colon);
    std::string_view rest = inside.substr(colon + 1);
    ButtonIcon icon;
    if (kind == "pad") {
        const std::size_t styleAt = rest.find(':');
        if (styleAt != std::string_view::npos) {
            const auto style = styleFromName(rest.substr(styleAt + 1));
            if (!style) {
                return std::nullopt;
            }
            icon.style = *style;
            rest = rest.substr(0, styleAt);
        }
        const auto b = parseBinding("pad " + std::string(rest));
        if (!b) {
            return std::nullopt;
        }
        icon.binding = *b;
        return icon;
    }
    if (kind == "key" || kind == "mouse") {
        const auto b = parseBinding((kind == "mouse" ? "mouse " : "") + std::string(rest));
        if (!b) {
            return std::nullopt;
        }
        icon.binding = *b;
        return icon;
    }
    return std::nullopt;
}

} // namespace

std::string iconToken(const ButtonIcon& icon)
{
    const Binding& b = icon.binding;
    switch (b.kind) {
    case Binding::Kind::Key: return "{icon:key:" + std::string(keyName(b.key)) + "}";
    case Binding::Kind::Mouse: return "{icon:mouse:" + bindingName(b).substr(6) + "}";   // after "Mouse "
    case Binding::Kind::Pad:
        return "{icon:pad:" + std::string(gamepadButtonName(b.pad)) + ":" + std::string(styleName(icon.style)) + "}";
    }
    return {};
}

std::vector<TextPiece> splitIcons(std::string_view text)
{
    std::vector<TextPiece> out;
    std::string words;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 6, "{icon:") == 0) {
            const std::size_t close = text.find('}', i);
            if (close != std::string_view::npos) {
                if (const auto icon = parseToken(text.substr(i + 6, close - i - 6))) {
                    if (!words.empty()) {
                        out.push_back({std::move(words), std::nullopt});
                        words.clear();
                    }
                    out.push_back({{}, icon});
                    i = close + 1;
                    continue;
                }
            }
        }
        words += text[i++];
    }
    if (!words.empty() || out.empty()) {
        out.push_back({std::move(words), std::nullopt});
    }
    return out;
}

std::string_view padButtonLabel(GamepadButton button, GamepadStyle style)
{
    switch (style) {
    case GamepadStyle::PlayStation:
        switch (button) {
        case GamepadButton::A: return "Cross";
        case GamepadButton::B: return "Circle";
        case GamepadButton::X: return "Square";
        case GamepadButton::Y: return "Triangle";
        case GamepadButton::LeftBumper: return "L1";
        case GamepadButton::RightBumper: return "R1";
        case GamepadButton::Back: return "Share";
        case GamepadButton::Start: return "Options";
        case GamepadButton::Guide: return "PS";
        case GamepadButton::LeftStick: return "L3";
        case GamepadButton::RightStick: return "R3";
        default: break;
        }
        break;
    case GamepadStyle::Nintendo:
        // Nintendo prints the letters the other way round: bottom B, right A,
        // left Y, top X. Rolo's names are by position, so the labels swap.
        switch (button) {
        case GamepadButton::A: return "B";
        case GamepadButton::B: return "A";
        case GamepadButton::X: return "Y";
        case GamepadButton::Y: return "X";
        case GamepadButton::LeftBumper: return "L";
        case GamepadButton::RightBumper: return "R";
        case GamepadButton::Back: return "-";
        case GamepadButton::Start: return "+";
        case GamepadButton::Guide: return "Home";
        case GamepadButton::LeftStick: return "L3";
        case GamepadButton::RightStick: return "R3";
        default: break;
        }
        break;
    case GamepadStyle::Xbox:
        switch (button) {
        case GamepadButton::Back: return "View";
        case GamepadButton::Start: return "Menu";
        case GamepadButton::Guide: return "Xbox";
        default: break;
        }
        break;
    case GamepadStyle::Generic: break;
    }
    return gamepadButtonName(button);
}

std::string resolveButtons(std::string_view text, const Input& input)
{
    if (text.find("{button:") == std::string_view::npos) {
        return std::string(text);
    }
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 8, "{button:") == 0) {
            const std::size_t close = text.find('}', i);
            if (close != std::string_view::npos) {
                const std::string_view name = text.substr(i + 8, close - i - 8);
                std::optional<Binding> binding = input.hasAction(name)
                                                     ? input.bindingFor(name, input.lastDevice())
                                                     : parseBinding(name);
                if (binding) {
                    out += iconToken({*binding, input.gamepadStyle()});
                    i = close + 1;
                    continue;
                }
            }
        }
        out += text[i++];
    }
    return out;
}

} // namespace rolo
