#include "CommandLine.hpp"

#include <charconv>
#include <limits>
#include <optional>

namespace rolo::player {

namespace {

// Parses a whole string as a number within [minimum, maximum].
template <typename T>
std::optional<T> parseNumber(std::string_view text, T minimum, T maximum)
{
    T value{};
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end || value < minimum || value > maximum) {
        return std::nullopt;
    }
    return value;
}

bool equalsIgnoringCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<Key> keyFromName(std::string_view name)
{
    for (int i = 1; i < static_cast<int>(Key::Count); ++i) {
        const Key key = static_cast<Key>(i);
        if (equalsIgnoringCase(keyName(key), name)) {
            return key;
        }
    }
    return std::nullopt;
}

ParseResult parseCommandLine(std::span<const std::string_view> args)
{
    ParseResult result;
    PlayerOptions& opt = result.options;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];

        // Options that take a value read the next argument.
        auto nextValue = [&](std::string_view& out) -> bool {
            if (i + 1 >= args.size()) {
                result.error = std::string(arg) + " needs a value.";
                return false;
            }
            out = args[++i];
            return true;
        };

        std::string_view value;
        if (arg == "--help" || arg == "-h") {
            opt.showHelp = true;
        } else if (arg == "--version") {
            opt.showVersion = true;
        } else if (arg == "--no-stats" || arg == "--stats") {
            opt.showStats = arg == "--stats";
            opt.statsChosen = true;
        } else if (arg == "--mute") {
            opt.sound = false;
        } else if (arg == "--fixed-step") {
            opt.fixedStep = true;
        } else if (arg == "--save-dir") {
            if (!nextValue(value)) {
                return result;
            }
            opt.saveDir = std::string(value);
        } else if (arg == "--tap") {
            if (!nextValue(value)) {
                return result;
            }
            // KEY@FRAME (pressed for one frame) or KEY@FROM-TO (held from FROM until TO).
            const std::size_t at = value.rfind('@');
            const auto key = at != std::string_view::npos ? keyFromName(value.substr(0, at)) : std::nullopt;
            const std::string_view frames = at != std::string_view::npos ? value.substr(at + 1) : std::string_view();
            const std::size_t dash = frames.find('-');
            constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
            const auto from = parseNumber<std::uint64_t>(frames.substr(0, dash), 0, kMax - 1);
            const auto to = dash == std::string_view::npos
                                ? (from ? std::optional<std::uint64_t>(*from + 1) : std::nullopt)
                                : parseNumber<std::uint64_t>(frames.substr(dash + 1), 0, kMax);
            if (!key || !from || !to || *to <= *from) {
                result.error = "--tap needs KEY@FRAME or KEY@FROM-TO, e.g. E@120 or Right@10-70, got '"
                               + std::string(value) + "'.";
                return result;
            }
            opt.tappedKeys.push_back({*key, *from, *to});
        } else if (arg == "--hold") {
            if (!nextValue(value)) {
                return result;
            }
            const auto key = keyFromName(value);
            if (!key) {
                result.error = "--hold needs a key name such as Right, Space or W, got '" + std::string(value) + "'.";
                return result;
            }
            opt.heldKeys.push_back(*key);
        } else if (arg == "--title") {
            if (!nextValue(value)) {
                return result;
            }
            opt.title = std::string(value);
        } else if (arg == "--width" || arg == "--height") {
            if (!nextValue(value)) {
                return result;
            }
            const auto number = parseNumber<int>(value, 64, 16384);
            if (!number) {
                result.error = std::string(arg) + " must be a whole number between 64 and 16384, got '"
                               + std::string(value) + "'.";
                return result;
            }
            (arg == "--width" ? opt.width : opt.height) = *number;
        } else if (arg == "--frames") {
            if (!nextValue(value)) {
                return result;
            }
            const auto number =
                parseNumber<std::uint64_t>(value, 1, std::numeric_limits<std::uint64_t>::max());
            if (!number) {
                result.error = "--frames must be a whole number of at least 1, got '" + std::string(value) + "'.";
                return result;
            }
            opt.maxFrames = *number;
        } else if (arg == "--project" || arg == "--scene") {
            if (!nextValue(value)) {
                return result;
            }
            (arg == "--project" ? opt.projectPath : opt.scenePath) = std::string(value);
        } else if (arg == "--screenshot") {
            if (!nextValue(value)) {
                return result;
            }
            opt.screenshotPath = std::string(value);
        } else {
            result.error = "Unknown option: " + std::string(arg);
            return result;
        }
    }

    if (!opt.projectPath.empty() && !opt.scenePath.empty()) {
        result.error = "Use either --project or --scene, not both.";
        return result;
    }
    if (!opt.screenshotPath.empty() && opt.maxFrames == 0) {
        result.error = "--screenshot needs --frames N, so the player knows which frame to capture.";
    }
    return result;
}

std::string helpText()
{
    return "Usage: rolo_player [options]\n"
           "\n"
           "Without --project or --scene, the player plays the game exported with it\n"
           "(a game folder beside it), or else shows Rolo's start scene.\n"
           "\n"
           "Options:\n"
           "  --help             Show this message\n"
           "  --version          Print version information and exit\n"
           "  --project FOLDER   Play a Rolo project (its start scene)\n"
           "  --scene FILE       Play one scene file (.lscene)\n"
           "  --title TEXT       Window title (default: Rolo Engine)\n"
           "  --width N          Window width in points (default: 1280)\n"
           "  --height N         Window height in points (default: 720)\n"
           "  --no-stats         Hide the frame-rate overlay\n"
           "  --stats            Show it (exported games hide it by default)\n"
           "  --mute             Play without sound\n"
           "  --frames N         Quit automatically after N frames (used by tests)\n"
           "  --screenshot FILE  Save the last frame as a PNG (needs --frames)\n"
           "  --hold KEY         Hold a key down for the whole run, e.g. Right (tests)\n"
           "  --tap KEY@FRAME    Press a key at that frame, e.g. E@120; KEY@FROM-TO holds\n"
           "                     it from one frame to another, e.g. Right@10-70 (tests)\n"
           "  --save-dir FOLDER  Keep save slots in FOLDER\n"
           "  --fixed-step       Count every frame as 1/60 s, for repeatable runs (tests)\n"
           "\n"
           "In the game: F5 saves to the quick slot, F9 loads it. Escape or closing\n"
           "the window quits.\n";
}

std::optional<std::filesystem::path> bundledGameFolder(const std::filesystem::path& executable)
{
    const std::filesystem::path dir = executable.parent_path();
    for (const std::filesystem::path& candidate : {dir / "game", dir.parent_path() / "Resources" / "game"}) {
        std::error_code ec;
        if (std::filesystem::exists(candidate / "project.rolo", ec)) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace rolo::player
