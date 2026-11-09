#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/core/Names.hpp"

#include "rolo/core/Files.hpp"
#include "rolo/scene/Project.hpp"
#include "rolo/scene/Scene.hpp"

#include <nlohmann/json.hpp>
// stb_image, compiled into this file only and with every function `static`:
// Axmol carries its own copy of stb_image, and two non-static copies would
// clash when the editor and games are linked. Only PNG and JPEG are needed.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_WINDOWS_UTF8   // file names are UTF-8 on every OS
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace rolo {

namespace {

using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// "assets/hero.png" for a path inside the project; forward slashes on every OS.
std::string relativeTo(const fs::path& folder, const fs::path& path)
{
    return path.lexically_relative(folder).generic_string();
}

fs::path metaPathFor(const fs::path& file)
{
    fs::path meta = file;
    meta += std::string(AssetFormat::kMetaExtension);
    return meta;
}

bool isHidden(const fs::path& path)
{
    const std::string name = path.filename().string();
    return !name.empty() && name.front() == '.';
}

void setError(std::string* error, std::string message)
{
    if (error != nullptr) {
        *error = std::move(message);
    }
}

std::string_view filterName(TextureFilter f)
{
    return f == TextureFilter::Pixel ? "pixel" : "smooth";
}

struct Meta {
    AssetId id;
    std::optional<AssetType> type;
    TextureFilter filter = TextureFilter::Smooth;
};

std::optional<Meta> parseMeta(std::string_view text, std::string& problem)
{
    Json j;
    try {
        j = Json::parse(text.begin(), text.end());
    } catch (const nlohmann::json::parse_error&) {
        problem = "its .meta file is not valid JSON";
        return std::nullopt;
    }
    if (!j.is_object() || !isFormatName(j.value("format", ""), AssetFormat::kName)) {
        problem = "its .meta file is not a Rolo asset file";
        return std::nullopt;
    }
    if (j.contains("version") && j["version"].is_number_integer() && j["version"].get<int>() > AssetFormat::kVersion) {
        problem = "its .meta file was written by a newer version of Rolo";
        return std::nullopt;
    }
    Meta meta;
    const auto id = j.contains("id") && j["id"].is_string() ? AssetId::parse(j["id"].get<std::string>()) : std::nullopt;
    if (!id) {
        problem = "its .meta file has no valid \"id\"";
        return std::nullopt;
    }
    meta.id = *id;
    if (j.contains("type") && j["type"].is_string()) {
        meta.type = assetTypeFromName(j["type"].get<std::string>());
    }
    if (j.value("filter", "") == "pixel") {
        meta.filter = TextureFilter::Pixel;
    }
    return meta;
}

/// Picks "name 2.ext", "name 3.ext"... until the name is free.
fs::path freeName(const fs::path& wanted)
{
    std::error_code ec;
    if (!fs::exists(wanted, ec) && !fs::exists(metaPathFor(wanted), ec)) {
        return wanted;
    }
    const fs::path folder = wanted.parent_path();
    const std::string stem = wanted.stem().string();
    const std::string ext = wanted.extension().string();
    for (int n = 2; n < 10000; ++n) {
        const fs::path candidate = folder / (stem + " " + std::to_string(n) + ext);
        if (!fs::exists(candidate, ec) && !fs::exists(metaPathFor(candidate), ec)) {
            return candidate;
        }
    }
    return wanted;
}

} // namespace

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
std::string_view assetTypeName(AssetType type)
{
    switch (type) {
    case AssetType::Image: return "image";
    case AssetType::Tileset: return "tileset";
    case AssetType::Audio: return "audio";
    case AssetType::Font: return "font";
    case AssetType::Scene: return "scene";
    case AssetType::Variables: return "variables";
    case AssetType::Dialogue: return "dialogue";
    case AssetType::Quest: return "quest";
    case AssetType::Animation: return "animation";
    case AssetType::Script: return "script";
    }
    return "image";
}

std::optional<AssetType> assetTypeFromName(std::string_view name)
{
    for (AssetType t : {AssetType::Image, AssetType::Tileset, AssetType::Audio, AssetType::Font, AssetType::Scene,
                        AssetType::Variables, AssetType::Dialogue, AssetType::Quest, AssetType::Animation,
                        AssetType::Script}) {
        if (assetTypeName(t) == name) {
            return t;
        }
    }
    return std::nullopt;
}

std::optional<AssetType> assetTypeForPath(const fs::path& path)
{
    const std::string ext = lower(path.extension().string());
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
        return AssetType::Image;
    }
    if (ext == TilesetFormat::kExtension) {
        return AssetType::Tileset;
    }
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") {
        return AssetType::Audio;
    }
    if (ext == ".ttf" || ext == ".otf") {
        return AssetType::Font;
    }
    if (ext == SceneFormat::kExtension) {
        return AssetType::Scene;
    }
    // RPG content (read and checked by rolo_content's ContentLibrary).
    if (ext == ".lvars") {
        return AssetType::Variables;
    }
    if (ext == ".ldialogue") {
        return AssetType::Dialogue;
    }
    if (ext == ".lquest") {
        return AssetType::Quest;
    }
    if (ext == AnimationFormat::kExtension) {
        return AssetType::Animation;
    }
    if (ext == ".lua" || ext == ".luau") {   // Roblox's tools now name Luau files .luau
        return AssetType::Script;
    }
    return std::nullopt;
}

std::string AssetRecord::fileName() const
{
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// ---------------------------------------------------------------------------
// AssetDatabase
// ---------------------------------------------------------------------------
AssetDatabase::AssetDatabase(fs::path projectFolder, Access access)
    : m_folder(std::move(projectFolder))
    , m_access(access)
{
}

AssetId AssetDatabase::newId()
{
    AssetId id;
    do {
        id = AssetId(m_ids.nextValue());
    } while (m_byId.count(id) > 0);
    return id;
}

void AssetDatabase::index()
{
    std::sort(m_assets.begin(), m_assets.end(),
              [](const AssetRecord& a, const AssetRecord& b) { return a.path < b.path; });
    m_byId.clear();
    m_byPath.clear();
    for (std::size_t i = 0; i < m_assets.size(); ++i) {
        m_byId[m_assets[i].id] = i;
        m_byPath[m_assets[i].path] = i;
    }
}

Status AssetDatabase::writeMeta(const AssetRecord& record) const
{
    if (m_access == Access::ReadOnly) {
        return Status::success(); // games never change the project
    }
    Json j;
    j["format"] = AssetFormat::kName;
    j["version"] = AssetFormat::kVersion;
    j["id"] = record.id.toString();
    j["type"] = assetTypeName(record.type);
    if (record.type == AssetType::Image) {
        j["filter"] = filterName(record.filter);
    }
    return writeTextFileAtomically(metaPathFor(m_folder / record.path), j.dump(2) + "\n");
}

void AssetDatabase::readAsset(AssetRecord& record, std::vector<FileIssue>& issues, bool report)
{
    record.problem.clear();
    record.image.reset();
    record.tileset.reset();
    record.animation.reset();
    const fs::path file = m_folder / record.path;

    if (record.type == AssetType::Image) {
        int w = 0;
        int h = 0;
        int channels = 0;
        const std::string native = file.string();
        if (stbi_info(native.c_str(), &w, &h, &channels) == 1 && w > 0 && h > 0) {
            record.image = ImageInfo{w, h};
            if (w > 8192 || h > 8192) {
                record.problem = "The image is " + std::to_string(w) + " x " + std::to_string(h)
                                 + " pixels; the largest supported size is 8192 x 8192.";
            }
        } else {
            record.problem = "This file is not a readable PNG or JPEG image (is it damaged, or a different "
                             "format renamed?).";
        }
    } else if (record.type == AssetType::Animation) {
        std::string error;
        const auto text = readTextFile(file);
        if (!text) {
            record.problem = "The animation file could not be read.";
        } else if (auto set = parseAnimationSet(*text, &error)) {
            record.animation = std::move(*set);
        } else {
            record.problem = error;
        }
    } else if (record.type == AssetType::Tileset) {
        std::string error;
        const auto text = readTextFile(file);
        if (!text) {
            record.problem = "The tileset file could not be read.";
        } else if (auto tileset = parseTileset(*text, &error)) {
            record.tileset = std::move(*tileset);
        } else {
            record.problem = error;
        }
    }

    if (!report) {
        return;
    }
    const auto previously = m_reported.find(record.path);
    if (!record.problem.empty()
        && (previously == m_reported.end() || previously->second != record.problem)) {
        issues.push_back({FileIssue::Severity::Warning, record.path, record.problem});
    }
    if (record.problem.empty()) {
        m_reported.erase(record.path);
    } else {
        m_reported[record.path] = record.problem;
    }
}

std::vector<FileIssue> AssetDatabase::refresh()
{
    std::vector<FileIssue> issues;
    std::error_code ec;
    if (!fs::is_directory(m_folder, ec)) {
        if (!m_assets.empty()) {
            m_assets.clear();
            index();
            ++m_revision;
        }
        return issues;
    }

    // 1. Every supported file in the project (skipping hidden files and folders).
    std::vector<fs::path> files;
    std::vector<fs::path> metas;
    for (auto it = fs::recursive_directory_iterator(m_folder, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            break;
        }
        if (isHidden(it->path())) {
            if (it->is_directory(ec)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        if (it->path().extension() == AssetFormat::kMetaExtension) {
            metas.push_back(it->path());
        } else if (assetTypeForPath(it->path())) {
            files.push_back(it->path());
        }
    }

    // 2. Read or create the .meta of each file.
    std::vector<AssetRecord> found;
    std::unordered_map<AssetId, std::string> idOwner;
    bool changed = false;
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        AssetRecord record;
        record.path = relativeTo(m_folder, file);
        record.type = *assetTypeForPath(file);

        const fs::path metaPath = metaPathFor(file);
        bool needsMeta = true;
        if (const auto text = readTextFile(metaPath)) {
            std::string problem;
            if (const auto meta = parseMeta(*text, problem)) {
                record.id = meta->id;
                record.filter = meta->filter;
                needsMeta = false;
                if (const auto owner = idOwner.find(record.id); owner != idOwner.end()) {
                    // A file and its .meta were copied: the copy needs its own ID.
                    issues.push_back({FileIssue::Severity::Warning, record.path,
                                      "It had the same ID as " + owner->second
                                          + " (copied with its .meta file?), so it was given a new ID."});
                    record.id = AssetId{};
                    needsMeta = true;
                }
            } else {
                issues.push_back({FileIssue::Severity::Warning, record.path,
                                  "Its .meta file could not be used (" + problem
                                      + "), so it was given a new ID. Scenes that used the old ID will show it as missing."});
            }
        }
        if (record.id.isNull()) {
            // Keep the ID this database already gave the file (e.g. its .meta could not be written).
            if (const auto known = m_byPath.find(record.path); known != m_byPath.end()) {
                record.id = m_assets[known->second].id;
                record.filter = m_assets[known->second].filter;
            } else {
                do {
                    record.id = AssetId(m_ids.nextValue());
                } while (idOwner.count(record.id) > 0 || m_byId.count(record.id) > 0);
            }
        }
        idOwner[record.id] = record.path;
        if (needsMeta && record.type == AssetType::Image && m_byPath.count(record.path) == 0) {
            // A new image: small ones are usually pixel art (same rule as import()).
            int w = 0;
            int h = 0;
            int channels = 0;
            const std::string native = file.string();
            if (stbi_info(native.c_str(), &w, &h, &channels) == 1 && w <= 256 && h <= 256) {
                record.filter = TextureFilter::Pixel;
            }
        }
        if (needsMeta) {
            if (const Status s = writeMeta(record); !s) {
                issues.push_back({FileIssue::Severity::Warning, record.path, "Could not write its .meta file: " + s.message});
            }
            changed = true;
        }
        found.push_back(std::move(record));
    }

    // 3. .meta files whose asset file is gone.
    for (const fs::path& meta : metas) {
        fs::path file = meta;
        file.replace_extension(); // drop ".meta"
        if (!fs::exists(file, ec)) {
            const std::string rel = relativeTo(m_folder, file);
            if (m_reported.emplace(rel, "missing").second) {
                issues.push_back({FileIssue::Severity::Warning, rel,
                                  "This asset's file is missing (only its .meta file is left). Scenes that use "
                                  "it will show a placeholder. Put the file back, or delete the .meta file."});
            }
        } else {
            m_reported.erase(relativeTo(m_folder, file) + "#missing");
        }
    }

    // 4. Check contents of new or changed files.
    for (AssetRecord& record : found) {
        const fs::file_time_type stamp = fs::last_write_time(m_folder / record.path, ec);
        const auto old = m_byPath.find(record.path);
        const bool known = old != m_byPath.end() && m_assets[old->second].id == record.id;
        const auto lastStamp = m_stamps.find(record.path);
        if (known && lastStamp != m_stamps.end() && lastStamp->second == stamp
            && m_assets[old->second].filter == record.filter) {
            const AssetRecord& previous = m_assets[old->second];
            // Unchanged: keep what was read from the file last time.
            record.image = previous.image;
            record.tileset = previous.tileset;
            record.animation = previous.animation;
            record.problem = previous.problem;
            continue;
        }
        m_stamps[record.path] = stamp;
        readAsset(record, issues, true);
        changed = true;
    }
    if (found.size() != m_assets.size()) {
        changed = true;
    }
    // Forget stamps of files that are gone.
    for (auto it = m_stamps.begin(); it != m_stamps.end();) {
        const bool present = std::any_of(found.begin(), found.end(),
                                         [&](const AssetRecord& r) { return r.path == it->first; });
        it = present ? std::next(it) : m_stamps.erase(it);
    }

    m_assets = std::move(found);
    index();
    if (changed) {
        ++m_revision;
    }
    return issues;
}

const AssetRecord* AssetDatabase::find(AssetId id) const
{
    const auto it = m_byId.find(id);
    return it == m_byId.end() ? nullptr : &m_assets[it->second];
}

const AssetRecord* AssetDatabase::findByPath(std::string_view relativePath) const
{
    const auto it = m_byPath.find(std::string(relativePath));
    return it == m_byPath.end() ? nullptr : &m_assets[it->second];
}

std::vector<const AssetRecord*> AssetDatabase::all(std::optional<AssetType> type) const
{
    std::vector<const AssetRecord*> out;
    for (const AssetRecord& a : m_assets) {
        if (!type || a.type == *type) {
            out.push_back(&a);
        }
    }
    return out;
}

fs::path AssetDatabase::absolutePath(const AssetRecord& asset) const
{
    return m_folder / fs::path(asset.path);
}

std::optional<AssetId> AssetDatabase::import(const fs::path& source, std::string_view folder, std::string* error)
{
    const auto type = assetTypeForPath(source);
    if (!type) {
        setError(error, source.filename().string()
                            + " is not a supported asset (images: PNG, JPEG; sounds: WAV, OGG, MP3; fonts: TTF, OTF).");
        return std::nullopt;
    }
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        setError(error, "There is no file at " + source.string() + ".");
        return std::nullopt;
    }
    const fs::path destFolder = m_folder / fs::path(std::string(folder));
    fs::create_directories(destFolder, ec);
    if (ec) {
        setError(error, "Could not create the folder " + destFolder.string() + ": " + ec.message());
        return std::nullopt;
    }
    // Already inside the project (e.g. dropped from the project folder itself): just register it.
    const fs::path canonicalSource = fs::weakly_canonical(source, ec);
    const fs::path canonicalProject = fs::weakly_canonical(m_folder, ec);
    const std::string rel = canonicalSource.lexically_relative(canonicalProject).generic_string();
    fs::path target;
    if (!rel.empty() && rel.rfind("..", 0) != 0) {
        target = m_folder / fs::path(rel);
    } else {
        target = freeName(destFolder / source.filename());
        fs::copy_file(source, target, ec);
        if (ec) {
            setError(error, "Could not copy " + source.filename().string() + " into the project: " + ec.message());
            return std::nullopt;
        }
    }
    AssetRecord record;
    record.path = relativeTo(m_folder, target);
    record.type = *type;
    record.id = newId();
    if (const auto existing = findByPath(record.path)) {
        return existing->id;
    }
    // Pixel art is usually small: default to hard pixel edges for images up to 256 pixels.
    std::vector<FileIssue> ignored;
    readAsset(record, ignored, false);
    if (record.image && record.image->width <= 256 && record.image->height <= 256) {
        record.filter = TextureFilter::Pixel;
    }
    if (const Status s = writeMeta(record); !s) {
        setError(error, "Could not write the .meta file: " + s.message);
        return std::nullopt;
    }
    m_stamps[record.path] = fs::last_write_time(target, ec);
    const AssetId id = record.id;
    m_assets.push_back(std::move(record));
    index();
    ++m_revision;
    return id;
}

std::optional<AssetId> AssetDatabase::createTileset(AssetId image, Vec2 tileSize, std::string_view name, std::string* error)
{
    const AssetRecord* img = find(image);
    if (img == nullptr || img->type != AssetType::Image) {
        setError(error, "Choose an image to make the tileset from.");
        return std::nullopt;
    }
    Tileset t;
    t.image = image;
    t.tileWidth = std::max(1, static_cast<int>(tileSize.x));
    t.tileHeight = std::max(1, static_cast<int>(tileSize.y));
    const std::size_t slash = img->path.rfind('/');
    const std::string folder = slash == std::string::npos ? std::string() : img->path.substr(0, slash);
    const fs::path target = freeName(m_folder / fs::path(folder) / (std::string(name) + std::string(TilesetFormat::kExtension)));
    if (const Status s = writeTextFileAtomically(target, serializeTileset(t)); !s) {
        setError(error, "Could not write the tileset: " + s.message);
        return std::nullopt;
    }
    AssetRecord record;
    record.path = relativeTo(m_folder, target);
    record.type = AssetType::Tileset;
    record.id = newId();
    record.tileset = t;
    if (const Status s = writeMeta(record); !s) {
        setError(error, "Could not write the .meta file: " + s.message);
        return std::nullopt;
    }
    std::error_code ec;
    m_stamps[record.path] = fs::last_write_time(target, ec);
    const AssetId id = record.id;
    m_assets.push_back(std::move(record));
    index();
    ++m_revision;
    return id;
}

std::optional<AssetId> AssetDatabase::createAnimationSet(AssetId image, Vec2 frameSize, std::string_view name,
                                                        std::string* error)
{
    const AssetRecord* img = find(image);
    if (img == nullptr || img->type != AssetType::Image) {
        setError(error, "Choose an image (a sprite sheet) to make the animations from.");
        return std::nullopt;
    }
    AnimationSet set;
    set.image = image;
    set.frameWidth = std::max(1, static_cast<int>(frameSize.x));
    set.frameHeight = std::max(1, static_cast<int>(frameSize.y));
    set.clips.push_back({"idle", {0}, 8.0f, true});
    const std::size_t slash = img->path.rfind('/');
    const std::string folder = slash == std::string::npos ? std::string() : img->path.substr(0, slash);
    return createTextAsset(folder, name, AnimationFormat::kExtension, serializeAnimationSet(set), error);
}

std::optional<AssetId> AssetDatabase::createTextAsset(std::string_view folder, std::string_view name,
                                                      std::string_view extension, std::string_view text,
                                                      std::string* error)
{
    if (m_access == Access::ReadOnly) {
        setError(error, "The project is open read-only.");
        return std::nullopt;
    }
    const fs::path target = freeName(m_folder / fs::path(folder) / (std::string(name) + std::string(extension)));
    const auto type = assetTypeForPath(target);
    if (!type) {
        setError(error, "\"" + std::string(extension) + "\" is not a supported asset type.");
        return std::nullopt;
    }
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (const Status s = writeTextFileAtomically(target, text); !s) {
        setError(error, "Could not write the file: " + s.message);
        return std::nullopt;
    }
    AssetRecord record;
    record.path = relativeTo(m_folder, target);
    record.type = *type;
    record.id = newId();
    if (const Status s = writeMeta(record); !s) {
        setError(error, "Could not write the .meta file: " + s.message);
        return std::nullopt;
    }
    std::vector<FileIssue> ignored;
    readAsset(record, ignored, false);
    m_stamps[record.path] = fs::last_write_time(target, ec);
    const AssetId id = record.id;
    m_assets.push_back(std::move(record));
    index();
    ++m_revision;
    return id;
}

Status AssetDatabase::writeText(AssetId id, std::string_view text)
{
    const auto it = m_byId.find(id);
    if (it == m_byId.end()) {
        return Status::failure("There is no such asset.");
    }
    if (m_access == Access::ReadOnly) {
        return Status::failure("The project is open read-only.");
    }
    AssetRecord& record = m_assets[it->second];
    if (const Status s = writeTextFileAtomically(m_folder / record.path, text); !s) {
        return s;
    }
    std::vector<FileIssue> ignored;
    readAsset(record, ignored, false);   // e.g. a tileset's definition
    std::error_code ec;
    m_stamps[record.path] = fs::last_write_time(m_folder / record.path, ec);
    ++m_revision;
    return Status::success();
}

std::optional<std::string> AssetDatabase::readText(AssetId id) const
{
    const AssetRecord* record = find(id);
    return record != nullptr ? readTextFile(absolutePath(*record)) : std::nullopt;
}

Status AssetDatabase::saveTileset(AssetId id, const Tileset& definition)
{
    const auto it = m_byId.find(id);
    if (it == m_byId.end() || m_assets[it->second].type != AssetType::Tileset) {
        return Status::failure("There is no such tileset.");
    }
    AssetRecord& record = m_assets[it->second];
    if (const Status s = writeTextFileAtomically(m_folder / record.path, serializeTileset(definition)); !s) {
        return s;
    }
    record.tileset = definition;
    record.problem.clear();
    std::error_code ec;
    m_stamps[record.path] = fs::last_write_time(m_folder / record.path, ec);
    ++m_revision;
    return Status::success();
}

Status AssetDatabase::setFilter(AssetId id, TextureFilter filter)
{
    const auto it = m_byId.find(id);
    if (it == m_byId.end()) {
        return Status::failure("There is no such asset.");
    }
    AssetRecord& record = m_assets[it->second];
    record.filter = filter;
    ++m_revision;
    return writeMeta(record);
}

Status AssetDatabase::move(AssetId id, std::string_view newRelativePath)
{
    const auto it = m_byId.find(id);
    if (it == m_byId.end()) {
        return Status::failure("There is no such asset.");
    }
    if (!Project::isValidRelativePath(newRelativePath)) {
        return Status::failure("\"" + std::string(newRelativePath) + "\" is not a valid place inside the project.");
    }
    AssetRecord& record = m_assets[it->second];
    const fs::path from = m_folder / fs::path(record.path);
    const fs::path to = m_folder / fs::path(std::string(newRelativePath));
    if (assetTypeForPath(to) != record.type) {
        return Status::failure("The new name must keep the file's extension (" + from.extension().string() + ").");
    }
    std::error_code ec;
    if (fs::exists(to, ec)) {
        return Status::failure("There is already a file called " + std::string(newRelativePath) + ".");
    }
    fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    if (ec) {
        return Status::failure("Could not move the file: " + ec.message());
    }
    fs::rename(metaPathFor(from), metaPathFor(to), ec);
    if (ec) {
        fs::rename(to, from, ec); // put the file back rather than separate it from its ID
        return Status::failure("Could not move the .meta file, so nothing was moved.");
    }
    m_stamps.erase(record.path);
    record.path = relativeTo(m_folder, to);
    m_stamps[record.path] = fs::last_write_time(to, ec);
    index();
    ++m_revision;
    return Status::success();
}

Status AssetDatabase::remove(AssetId id)
{
    const auto it = m_byId.find(id);
    if (it == m_byId.end()) {
        return Status::failure("There is no such asset.");
    }
    const fs::path file = m_folder / fs::path(m_assets[it->second].path);
    std::error_code ec;
    fs::remove(file, ec);
    if (ec) {
        return Status::failure("Could not delete " + file.filename().string() + ": " + ec.message());
    }
    fs::remove(metaPathFor(file), ec);
    m_stamps.erase(m_assets[it->second].path);
    m_assets.erase(m_assets.begin() + static_cast<std::ptrdiff_t>(it->second));
    index();
    ++m_revision;
    return Status::success();
}

std::vector<std::string> AssetDatabase::scenesReferencing(AssetId id) const
{
    // Asset IDs are 16 random hex digits, so finding the text in a scene file
    // is a reliable (and fast) way to find references without loading scenes.
    std::vector<std::string> scenes;
    const std::string needle = "\"" + id.toString() + "\"";
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(m_folder, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            break;
        }
        if (it->path().extension() == SceneFormat::kExtension) {
            if (const auto text = readTextFile(it->path()); text && text->find(needle) != std::string::npos) {
                scenes.push_back(relativeTo(m_folder, it->path()));
            }
        }
    }
    std::sort(scenes.begin(), scenes.end());
    return scenes;
}

// ---------------------------------------------------------------------------
// References from a scene
// ---------------------------------------------------------------------------
std::vector<FileIssue> checkAssetReferences(const Scene& scene, const AssetDatabase& assets)
{
    std::vector<FileIssue> issues;
    scene.forEach([&](const Entity& entity, int) {
        for (const auto& component : entity.components()) {
            for (const PropertyInfo& property : component->type().properties) {
                if (property.kind != PropertyKind::Asset) {
                    continue;
                }
                const auto value = component->get(property.key);
                const AssetId id = value ? std::get<AssetId>(*value) : AssetId{};
                if (id.isNull()) {
                    continue;
                }
                const std::string where = "entity '" + entity.name() + "' (" + entity.id().toString() + "), "
                                          + component->type().displayName + " " + property.label;
                const AssetRecord* asset = assets.find(id);
                if (asset == nullptr) {
                    issues.push_back({FileIssue::Severity::Warning, where,
                                      "The asset " + id.toString()
                                          + " is missing from the project (deleted, or moved outside the editor "
                                            "without its .meta file). A placeholder is shown; choose another asset."});
                } else if (!property.options.assetType.empty()
                           && assetTypeName(asset->type) != property.options.assetType) {
                    issues.push_back({FileIssue::Severity::Warning, where,
                                      asset->path + " is " + std::string(assetTypeName(asset->type)) + ", but "
                                          + property.options.assetType + " is needed here."});
                } else if (!asset->isUsable()) {
                    issues.push_back({FileIssue::Severity::Warning, where, asset->path + ": " + asset->problem});
                }
            }
        }
    });
    return issues;
}

} // namespace rolo
