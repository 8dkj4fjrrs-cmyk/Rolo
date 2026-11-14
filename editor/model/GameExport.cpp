#include "GameExport.hpp"

#include "rolo/assets/GamePack.hpp"
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

// Text made safe inside an HTML page.
std::string htmlEscaped(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

std::string webReadme(const ExportOptions& o, const std::string& gameName)
{
    return gameName + " (for the web)\n" + std::string(gameName.size() + 14, '=') + "\n\n"
           + "This folder is the game, ready for a web host. Players open its page in a browser:\n"
             "nothing to download or install.\n\n"
             "To share it\n"
             "-----------\n"
             "- itch.io: zip the CONTENTS of this folder (so that index.html is at the top of the zip),\n"
             "  create a project, choose \"HTML\" as the kind of project, upload the zip and tick\n"
             "  \"This file will be played in the browser\". Set the viewport to 1280 x 720.\n"
             "- Any web host (GitHub Pages, Netlify, your own site): upload this folder as it is.\n"
             "  No special server settings are needed.\n\n"
             "To try it on this computer\n"
             "--------------------------\n"
             "Opening index.html straight from the folder does not work: browsers only let a page load\n"
             "its files from a web address. Start a small web server in this folder instead, e.g. with\n"
             "Python (already on macOS and Linux):\n\n"
             "    python3 -m http.server 8000\n\n"
             "and open http://localhost:8000 in a browser.\n\n"
             "Good to know\n"
             "------------\n"
             "- Saved games are kept by the player's browser, for this web address.\n"
             "- Sound starts with the player's first click or key press (browsers insist on that).\n"
             "- You may edit index.html (colours, text around the game); see the comment at its top.\n"
             "- It needs a browser with WebAssembly and WebGL 2: Chrome, Firefox, Edge or Safari of\n"
             "  the last few years.\n\n"
           + "Made with Rolo Engine " + o.version + " (MIT License).\n"
           + "This game contains Rolo and the libraries it is built on; their licenses are in the\n"
             "licenses folder, which must stay with the game when you share it.\n";
}

// A game for the web: Rolo's web runtime, its page with the game's name, and
// the project's files as one pack.
ExportResult exportWebGame(const ExportOptions& o, ExportResult result, const std::string& name)
{
    const auto fail = [&](std::string message) {
        result.ok = false;
        result.error = std::move(message);
        return result;
    };
    std::error_code ec;
    for (const char* file : kWebPlayerFiles) {
        if (o.webPlayer.empty() || !fs::exists(o.webPlayer / file, ec)) {
            return fail("Rolo's web runtime was not found"
                        + (o.webPlayer.empty() ? std::string() : " (" + (o.webPlayer / file).string() + " is missing)")
                        + ". It comes with Rolo's download; when working from source, build it first "
                          "(docs/building.md, \"For the web\").");
        }
    }
    Copier copy{result, {}};
    for (const char* file : kWebPlayerFiles) {
        if (std::string_view(file) == "index.html") {
            continue;
        }
        if (!copy.file(o.webPlayer / file, result.game / file)) {
            return fail(copy.error);
        }
    }
    // The page, with the game's name where the template has {{TITLE}}.
    auto page = readTextFile(o.webPlayer / "index.html");
    if (!page) {
        return fail("Could not read " + (o.webPlayer / "index.html").string() + ".");
    }
    const std::string title = htmlEscaped(o.gameName.empty() ? name : o.gameName);
    for (std::size_t at = page->find("{{TITLE}}"); at != std::string::npos; at = page->find("{{TITLE}}", at + title.size())) {
        page->replace(at, 9, title);
    }
    if (const Status s = writeTextFileAtomically(result.game / "index.html", *page); !s) {
        return fail(s.message);
    }
    result.files += 1;
    result.bytes += page->size();

    const auto projectFile = [](const fs::path& p) { return !isHidden(p); };   // no .git, .DS_Store...
    const GamePackResult pack = writeGamePack(o.project, result.game / "game.pack",
                                              o.gameName.empty() ? name : o.gameName, gamePackSavesName(name),
                                              projectFile);
    if (!pack.status) {
        return fail(pack.status.message);
    }
    result.files += 1;
    result.bytes += pack.bytes;

    const auto anyFile = [](const fs::path& p) { return !isBuildLeftover(p); };
    if (!copy.tree(o.licenses, result.game / "licenses", anyFile)) {
        return fail(copy.error);
    }
    const std::string text = webReadme(o, name);
    if (const Status s = writeTextFileAtomically(result.game / "README.txt", text); !s) {
        return fail(s.message);
    }
    result.files += 1;
    result.bytes += text.size();
    if (const Status s = writeTextFileAtomically(result.game / kMarker, "Exported by Rolo " + o.version + "\n"); !s) {
        return fail(s.message);
    }
    result.program = result.game / "index.html";
    result.ok = true;
    return result;
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
    const bool web = o.target == ExportTarget::Web;
    if (!web && (o.player.empty() || !fs::exists(o.player, ec))) {
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
    const bool mac = !web && o.os == HostOs::MacOS;
    result.game = o.output / (web ? name + " Web" : mac ? name + ".app" : name);
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

    if (web) {
        return exportWebGame(o, std::move(result), name);
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
