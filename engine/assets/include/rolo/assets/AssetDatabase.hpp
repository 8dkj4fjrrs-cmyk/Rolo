#pragma once

// AssetDatabase: the files a project's scenes use (images, tilesets, sounds,
// fonts), each with a permanent AssetId.
//
// Every asset file has a small ".meta" file beside it:
//
//     assets/hero.png
//     assets/hero.png.meta      { "format": "rolo.asset", "version": 1,
//                                 "id": "8c1f02e94b7d3a60", "type": "image",
//                                 "filter": "pixel" }
//
// Scenes refer to assets by that ID, never by path, so renaming or moving an
// asset in the editor (which moves its .meta along) keeps every reference
// working. A file copied into the project without a .meta gets one, with a
// new ID, on the next refresh().
//
// The database reads files only; it never needs the graphics backend, so it
// is unit-tested in every preset. Image sizes come from stb_image.

#include "rolo/assets/Animation.hpp"
#include "rolo/assets/Tileset.hpp"
#include "rolo/scene/EntityId.hpp"
#include "rolo/scene/SceneFile.hpp" // FileIssue

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rolo {

class Scene;

enum class AssetType { Image, Tileset, Audio, Font, Scene, Variables, Dialogue, Quest, Animation, Script, Prefab };

/// "image", "tileset", "audio", "font", "scene", "variables", "dialogue", "quest", "animation", "script", "prefab"
/// (as in .meta files and PropertyOptions::assetType).
std::string_view assetTypeName(AssetType type);
std::optional<AssetType> assetTypeFromName(std::string_view name);
/// The type of asset a file name's extension stands for, if Rolo supports it:
/// .png .jpg .jpeg (Image), .ltileset (Tileset), .wav .ogg .mp3 (Audio), .ttf .otf (Font),
/// .lscene (Scene: scenes have IDs too, so gameplay can say "go to that scene"
/// and keep working when the scene file is renamed), .lvars (Variables),
/// .ldialogue (Dialogue), .lquest (Quest): RPG content, see rolo_content;
/// .lanim (Animation): flipbook clips cut from an image; .lua (Script): gameplay
/// code in Lua (run by rolo_gameplay's Script component); .lprefab (Prefab): an
/// object kept as a file, to put into scenes and to copy while playing.
std::optional<AssetType> assetTypeForPath(const std::filesystem::path& path);

/// How an image is smoothed when it is drawn larger or smaller than its size.
enum class TextureFilter {
    Smooth,  // blends neighbouring pixels: photos, painted art
    Pixel,   // keeps hard pixel edges: pixel art
};

struct ImageInfo {
    int width = 0;
    int height = 0;
};

struct AssetRecord {
    AssetId id;
    AssetType type = AssetType::Image;
    std::string path;                    // project-relative, forward slashes
    TextureFilter filter = TextureFilter::Smooth;   // images (and the images of tilesets)
    std::optional<ImageInfo> image;      // images: size, if the file is a valid image
    std::optional<Tileset> tileset;      // tilesets: the definition, if valid
    std::optional<AnimationSet> animation;   // animations: the definition, if valid
    std::string problem;                 // empty when the asset is usable; else what is wrong

    bool isUsable() const noexcept { return problem.empty(); }
    /// The file name without folders, e.g. "hero.png".
    std::string fileName() const;
};

namespace AssetFormat {
inline constexpr std::string_view kName = "rolo.asset";
inline constexpr int kVersion = 1;
inline constexpr std::string_view kMetaExtension = ".meta";
inline constexpr std::string_view kDefaultFolder = "assets";
} // namespace AssetFormat

class AssetDatabase {
public:
    enum class Access {
        ReadWrite,   // the editor: writes .meta files for new assets
        ReadOnly,    // games: never writes; new files get IDs for this run only
    };
    /// `projectFolder` is the folder containing project.rolo.
    explicit AssetDatabase(std::filesystem::path projectFolder, Access access = Access::ReadWrite);

    const std::filesystem::path& projectFolder() const noexcept { return m_folder; }

    /// Scans the project folder: reads .meta files, writes new ones for new
    /// assets, checks every asset, and notices files that were added, changed
    /// or removed. Returns what is worth telling the user (new problems only;
    /// an unchanged project gives no issues on later refreshes).
    std::vector<FileIssue> refresh();

    /// Increases whenever refresh() or a change through this class finds the
    /// set of assets or their contents changed.
    std::uint64_t revision() const noexcept { return m_revision; }

    /// The asset with this ID, or null. The pointer is only good until the
    /// next refresh() or change (records are rebuilt then, even when nothing
    /// changed), so look assets up again rather than keeping it.
    const AssetRecord* find(AssetId id) const;
    const AssetRecord* findByPath(std::string_view relativePath) const;
    /// Every asset (optionally only one type), sorted by path.
    std::vector<const AssetRecord*> all(std::optional<AssetType> type = {}) const;
    std::filesystem::path absolutePath(const AssetRecord& asset) const;

    /// Copies a file from outside the project into `folder` (project-relative,
    /// e.g. "assets"), giving it a .meta and an ID. A file of the same name
    /// gets a number added ("hero 2.png"). Returns the new asset's ID.
    std::optional<AssetId> import(const std::filesystem::path& source, std::string_view folder = AssetFormat::kDefaultFolder,
                                  std::string* error = nullptr);

    /// Creates a tileset asset for an image (`folder`/<name>.ltileset).
    std::optional<AssetId> createTileset(AssetId image, Vec2 tileSize, std::string_view name, std::string* error = nullptr);
    /// Saves a changed tileset definition.
    Status saveTileset(AssetId tileset, const Tileset& definition);

    /// Creates a text asset (e.g. a new dialogue) as `folder`/<name><extension>
    /// with `text` as its content, giving it a .meta and an ID. A file of the
    /// same name gets a number added. `extension` must be a supported one.
    std::optional<AssetId> createTextAsset(std::string_view folder, std::string_view name, std::string_view extension,
                                           std::string_view text, std::string* error = nullptr);
    /// Replaces a text asset's content (a content file, a tileset...). The
    /// file is replaced safely (written aside, then renamed).
    Status writeText(AssetId id, std::string_view text);
    /// The current text of a text asset, read from disk.
    std::optional<std::string> readText(AssetId id) const;

    /// Creates an animation set for an image (`folder`/<name>.lanim), with
    /// one clip showing the first frame.
    std::optional<AssetId> createAnimationSet(AssetId image, Vec2 frameSize, std::string_view name,
                                              std::string* error = nullptr);

    /// Changes an image's filter (saved in its .meta).
    Status setFilter(AssetId id, TextureFilter filter);
    /// Renames or moves an asset (and its .meta) inside the project. The ID stays.
    Status move(AssetId id, std::string_view newRelativePath);
    /// Deletes an asset's file and .meta.
    Status remove(AssetId id);

    /// Scenes (project-relative paths) whose files mention `id`.
    std::vector<std::string> scenesReferencing(AssetId id) const;

private:
    struct Scanned;
    void index();
    void readAsset(AssetRecord& record, std::vector<FileIssue>& issues, bool report);
    Status writeMeta(const AssetRecord& record) const;
    AssetId newId();

    std::filesystem::path m_folder;
    Access m_access = Access::ReadWrite;
    std::vector<AssetRecord> m_assets;
    std::unordered_map<AssetId, std::size_t> m_byId;
    std::unordered_map<std::string, std::size_t> m_byPath;
    std::unordered_map<std::string, std::string> m_reported;      // path -> problem already reported
    std::unordered_map<std::string, std::filesystem::file_time_type> m_stamps;   // path -> last seen change time
    std::uint64_t m_revision = 1;
    EntityIdGenerator m_ids;
};

/// How scene files get at the project's prefabs (linked copies, ADR 0035):
/// pass it to loadSceneFile / saveSceneFile. It reads from `assets` each
/// time it is asked, so `assets` must outlive it.
PrefabTexts prefabTextsOf(const AssetDatabase& assets);

/// Checks that every asset a scene refers to exists and has the right type.
/// Problems are warnings: the scene still opens (with placeholders).
std::vector<FileIssue> checkAssetReferences(const Scene& scene, const AssetDatabase& assets);

} // namespace rolo
