#include "GameExport.hpp"

#include "rolo/core/Files.hpp"
#include "rolo/scene/Project.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace rolo::editor {

namespace fs = std::filesystem;

namespace {

// Written into every export, so a later export may safely replace it.
constexpr const char* kMarker = ".rolo-export";

// Build leftovers that never belong in a game (Windows debug files and the like).
bool isBuildLeftover(const fs::path& file)
{
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".pdb" || ext == ".ilk" || ext == ".exp" || ext == ".lib" || ext == ".log";
}

bool isHidden(const fs::path& name)
{
    const std::string n = name.filename().string();
    return !n.empty() && n.front() == '.';
}

bool isInside(const fs::path& inner, const fs::path& outer)
{
    std::error_code ec;
    const fs::path a = fs::weakly_canonical(inner, ec);
    const fs::path b = fs::weakly_canonical(outer, ec);
    auto ai = a.begin();
    for (auto bi = b.begin(); bi != b.end(); ++bi, ++ai) {
        if (bi->empty()) {
            continue;   // a trailing separator
        }
        if (ai == a.end() || *ai != *bi) {
            return false;
        }
    }
    return true;
}

// Copies a folder, following links (so the game gets real files, not links
// into the build folder). `keep` decides per entry.
struct Copier {
    ExportResult& result;
    std::string error;

    bool file(const fs::path& from, const fs::path& to)
    {
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Could not copy " + from.string() + " to " + to.string() + ": " + ec.message();
            return false;
        }
        result.files += 1;
        result.bytes += fs::file_size(to, ec);
        return true;
    }

    bool tree(const fs::path& from, const fs::path& to, const std::function<bool(const fs::path&)>& keep)
    {
        std::error_code ec;
        fs::create_directories(to, ec);
        for (const fs::directory_entry& entry : fs::directory_iterator(from, ec)) {
            if (!keep(entry.path())) {
                continue;
            }
            const fs::path target = to / entry.path().filename();
            if (fs::is_directory(entry.path(), ec)) {   // follows links
                if (!tree(entry.path(), target, keep)) {
                    return false;
                }
            } else if (!file(entry.path(), target)) {
                return false;
            }
        }
        if (ec) {
            error = "Could not read " + from.string() + ": " + ec.message();
            return false;
        }
        return true;
    }
};

std::string readme(const ExportOptions& o, const std::string& gameName, const std::string& program)
{
    const std::string how = o.os == HostOs::MacOS
                                ? "Open " + gameName + ".app. (The first time, macOS may say it cannot check the app: "
                                  "right-click it and choose Open.)"
                                : "Run " + program + ".";
    // The keys every Rolo game has, unless the project turned them off.
    ProjectSettings settings;
    if (const auto project = Project::open(o.project)) {
        settings = project->settings();
    }
    std::string keys = "Arrow keys or W A S D walk (in most Rolo games)";
    if (settings.quickSaveKeys) {
        keys += ", F5 saves, F9 loads";
    }
    keys += settings.escapeQuits ? ", Escape quits." : ".";
    return gameName + "\n" + std::string(gameName.size(), '=') + "\n\n"
           + "To play: " + how + "\n\n"
           + keys + "\n"
             "Saved games are kept in your user folder, not here.\n\n"
           + "Made with Rolo Engine " + o.version + " (MIT License).\n"
           + "This game contains Rolo and the libraries it is built on; their licenses are in the\n"
             "licenses folder, which must stay with the game when you share it.\n";
}

} // namespace

std::string exportFileName(const std::string& gameName)
{
    std::string out;
    for (const char c : gameName) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0 || c == ' ' || c == '-' || c == '_' || u >= 0x80) {
            out += c;
        }
    }
    while (!out.empty() && out.front() == ' ') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out.empty() ? std::string("Game") : out;
}

ExportResult exportGame(const ExportOptions& o)
{
    ExportResult result;
    const auto fail = [&](std::string message) {
        result.ok = false;
        result.error = std::move(message);
        return result;
    };
    std::error_code ec;
    if (!isProjectFolder(o.project)) {
        return fail(o.project.string() + " is not a Rolo project (it has no project.rolo).");
    }
    if (o.player.empty() || !fs::exists(o.player, ec)) {
        return fail("Rolo's game runtime (rolo_player) was not found"
                    + (o.player.empty() ? std::string() : " at " + o.player.string())
                    + ". Reinstall Rolo, or build the rolo_player target when working from source.");
    }
    if (o.licenses.empty() || !fs::exists(o.licenses / "README.md", ec)) {
        return fail("The license texts that must go with every game were not found"
                    + (o.licenses.empty() ? std::string() : " in " + o.licenses.string()) + ". Reinstall Rolo.");
    }
    if (o.output.empty()) {
        return fail("Choose a folder to export the game into.");
    }
    if (isInside(o.output, o.project)) {
        return fail("Export outside the project folder: a game exported inside its own project would be copied "
                    "into the next export.");
    }

    const std::string name = exportFileName(o.gameName);
    const bool mac = o.os == HostOs::MacOS;
    result.game = o.output / (mac ? name + ".app" : name);
    if (fs::exists(result.game, ec)) {
        if (!fs::exists(result.game / kMarker, ec) && !fs::exists(result.game / "Contents/Resources" / kMarker, ec)) {
            return fail(result.game.string() + " already exists and is not an earlier export of a game. Choose "
                                               "another folder or name.");
        }
        fs::remove_all(result.game, ec);   // an earlier export: replace it
        if (ec) {
            return fail("Could not replace the earlier export " + result.game.string() + ": " + ec.message());
        }
    }

    Copier copy{result, {}};
    const auto anyFile = [](const fs::path& p) { return !isBuildLeftover(p); };
    const auto projectFile = [](const fs::path& p) { return !isHidden(p); };   // no .git, .DS_Store...
    fs::path resources;   // where the game's data and licenses go
    if (mac) {
        if (!copy.tree(o.player, result.game, anyFile)) {
            return fail(copy.error);
        }
        resources = result.game / "Contents" / "Resources";
        result.program = result.game / "Contents" / "MacOS" / "rolo_player";
        // The app's name (in Finder, the Dock and the menu bar) and identity.
        const fs::path plist = result.game / "Contents" / "Info.plist";
        if (auto text = readTextFile(plist)) {
            std::string id;
            for (const char c : name) {
                if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
                    id += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
            }
            *text = std::regex_replace(*text, std::regex("(<key>CFBundleName</key>\\s*<string>)[^<]*"), "$1" + name);
            *text = std::regex_replace(*text, std::regex("(<key>CFBundleIdentifier</key>\\s*<string>)[^<]*"),
                                       "$1dev.rolo-engine.game." + (id.empty() ? std::string("game") : id));
            if (const Status s = writeTextFileAtomically(plist, *text); !s) {
                return fail(s.message);
            }
        }
    } else {
        // The runtime's folder: the program (renamed) and what it needs beside it.
        const fs::path runtime = o.player.parent_path();
        const std::string program = o.os == HostOs::Windows ? name + ".exe" : name;
        const auto runtimeFile = [&](const fs::path& p) {
            return anyFile(p) && p.filename() != "game" && p.filename() != "licenses";
        };
        if (!copy.tree(runtime, result.game, runtimeFile)) {
            return fail(copy.error);
        }
        fs::rename(result.game / o.player.filename(), result.game / program, ec);
        if (ec) {
            return fail("Could not name the game's program " + program + ": " + ec.message());
        }
        result.program = result.game / program;
        if (o.os != HostOs::Windows) {
            fs::permissions(result.program, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                            fs::perm_options::add, ec);
        }
        resources = result.game;
        if (const Status s = writeTextFileAtomically(result.game / "README.txt", readme(o, name, program)); !s) {
            return fail(s.message);
        }
        result.files += 1;
    }
    if (!copy.tree(o.project, resources / "game", projectFile) || !copy.tree(o.licenses, resources / "licenses", anyFile)) {
        return fail(copy.error);
    }
    if (const Status s = writeTextFileAtomically(resources / kMarker, "Exported by Rolo " + o.version + "\n"); !s) {
        return fail(s.message);
    }
    result.ok = true;
    return result;
}

} // namespace rolo::editor
