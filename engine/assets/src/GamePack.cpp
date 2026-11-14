#include "rolo/assets/GamePack.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <system_error>

namespace rolo {

namespace fs = std::filesystem;

namespace {

using Json = nlohmann::ordered_json;

constexpr std::size_t kMagicLength = 10;
// An index larger than this is not a real pack's (it lists paths and sizes only).
constexpr std::uint32_t kMaxIndexBytes = 64u * 1024u * 1024u;

bool hidden(const fs::path& p)
{
    const std::string name = p.filename().string();
    return !name.empty() && name.front() == '.';
}

// A path inside a pack must stay inside the folder it is unpacked into.
bool safePath(const std::string& path)
{
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos
        || path.find(':') != std::string::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

struct Listed {
    std::string path;   // in the pack
    fs::path file;      // on disk
    std::uint64_t size = 0;
};

// The files to pack, in alphabetical order (folders are followed through links).
bool list(const fs::path& folder, const std::string& prefix, const std::function<bool(const fs::path&)>& keep,
          std::vector<Listed>& out, std::string& error)
{
    std::error_code ec;
    std::vector<fs::path> entries;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
        entries.push_back(entry.path());
    }
    if (ec) {
        error = "Could not read " + folder.string() + ": " + ec.message();
        return false;
    }
    std::sort(entries.begin(), entries.end(),
              [](const fs::path& a, const fs::path& b) { return a.filename().generic_u8string() < b.filename().generic_u8string(); });
    for (const fs::path& entry : entries) {
        if (keep ? !keep(entry) : hidden(entry)) {
            continue;
        }
        const std::u8string name8 = entry.filename().generic_u8string();
        const std::string path = prefix + std::string(name8.begin(), name8.end());
        if (fs::is_directory(entry, ec)) {
            if (!list(entry, path + "/", keep, out, error)) {
                return false;
            }
        } else if (fs::is_regular_file(entry, ec)) {
            const std::uint64_t size = fs::file_size(entry, ec);
            if (ec) {
                error = "Could not read " + entry.string() + ": " + ec.message();
                return false;
            }
            out.push_back({path, entry, size});
        }
    }
    return true;
}

void setError(std::string* error, std::string message)
{
    if (error != nullptr) {
        *error = std::move(message);
    }
}

} // namespace

std::string gamePackSavesName(const std::string& gameName)
{
    std::string out;
    for (const char c : gameName) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x80 && std::isalnum(u) != 0) {
            out += static_cast<char>(std::tolower(u));
        } else if (!out.empty() && out.back() != '-') {
            out += '-';
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out.empty() ? std::string("game") : out;
}

GamePackResult writeGamePack(const fs::path& folder, const fs::path& output, const std::string& name,
                             const std::string& saves, const std::function<bool(const fs::path&)>& keep)
{
    GamePackResult result;
    std::vector<Listed> files;
    std::string error;
    if (!list(folder, "", keep, files, error)) {
        result.status = Status::failure(error);
        return result;
    }
    Json index;
    index["format"] = GamePackFormat::kFormat;
    index["version"] = GamePackFormat::kVersion;
    index["name"] = name;
    index["saves"] = saves;
    Json list = Json::array();
    for (const Listed& f : files) {
        list.push_back({{"path", f.path}, {"size", f.size}});
    }
    index["files"] = std::move(list);
    const std::string indexText = index.dump();

    // Written beside the output first and renamed at the end, so a pack that
    // exists is always a whole one.
    fs::path temporary = output;
    temporary += ".tmp";
    std::error_code ec;
    fs::create_directories(output.parent_path(), ec);
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.status = Status::failure("Could not write " + output.string() + ".");
            return result;
        }
        out.write(GamePackFormat::kMagic, static_cast<std::streamsize>(kMagicLength));
        const auto length = static_cast<std::uint32_t>(indexText.size());
        const std::array<char, 4> lengthBytes{static_cast<char>(length & 0xff), static_cast<char>((length >> 8) & 0xff),
                                              static_cast<char>((length >> 16) & 0xff),
                                              static_cast<char>((length >> 24) & 0xff)};
        out.write(lengthBytes.data(), 4);
        out.write(indexText.data(), static_cast<std::streamsize>(indexText.size()));
        std::vector<char> buffer(1 << 16);
        for (const Listed& f : files) {
            std::ifstream in(f.file, std::ios::binary);
            std::uint64_t copied = 0;
            while (in && copied < f.size) {
                const auto want = static_cast<std::streamsize>(std::min<std::uint64_t>(buffer.size(), f.size - copied));
                in.read(buffer.data(), want);
                const std::streamsize got = in.gcount();
                if (got <= 0) {
                    break;
                }
                out.write(buffer.data(), got);
                copied += static_cast<std::uint64_t>(got);
            }
            if (copied != f.size) {   // unreadable, or changed while packing
                out.close();
                fs::remove(temporary, ec);
                result.status = Status::failure("Could not read " + f.file.string() + ".");
                return result;
            }
        }
        out.flush();
        if (!out) {
            out.close();
            fs::remove(temporary, ec);
            result.status = Status::failure("Could not write " + output.string() + " (is the disk full?).");
            return result;
        }
    }
    fs::rename(temporary, output, ec);
    if (ec) {
        fs::remove(temporary, ec);
        result.status = Status::failure("Could not write " + output.string() + ": " + ec.message());
        return result;
    }
    result.status = Status::success();
    result.files = files.size();
    result.bytes = fs::file_size(output, ec);
    return result;
}

std::optional<GamePackIndex> readGamePackIndex(const fs::path& pack, std::string* error)
{
    std::ifstream in(pack, std::ios::binary);
    if (!in) {
        setError(error, "Could not open " + pack.string() + ".");
        return std::nullopt;
    }
    std::array<char, kMagicLength + 4> head{};
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    if (in.gcount() != static_cast<std::streamsize>(head.size())
        || std::memcmp(head.data(), GamePackFormat::kMagic, kMagicLength) != 0) {
        setError(error, pack.string() + " is not a Rolo game pack.");
        return std::nullopt;
    }
    const auto byte = [&head](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(head[kMagicLength + i])); };
    const std::uint32_t length = byte(0) | (byte(1) << 8) | (byte(2) << 16) | (byte(3) << 24);
    if (length > kMaxIndexBytes) {
        setError(error, pack.string() + " is damaged (its list of files is too large).");
        return std::nullopt;
    }
    std::string text(length, '\0');
    in.read(text.data(), static_cast<std::streamsize>(length));
    if (in.gcount() != static_cast<std::streamsize>(length)) {
        setError(error, pack.string() + " is cut short.");
        return std::nullopt;
    }
    const Json json = Json::parse(text, nullptr, false);
    if (!json.is_object() || json.value("format", "") != GamePackFormat::kFormat || !json.contains("files")
        || !json["files"].is_array()) {
        setError(error, pack.string() + " is damaged (its list of files cannot be read).");
        return std::nullopt;
    }
    if (json.value("version", 0) > GamePackFormat::kVersion) {
        setError(error, pack.string() + " was made by a newer Rolo.");
        return std::nullopt;
    }
    GamePackIndex index;
    index.name = json.value("name", "");
    index.saves = json.value("saves", "");
    index.dataOffset = head.size() + length;
    for (const Json& f : json["files"]) {
        if (!f.is_object() || !f.contains("path") || !f["path"].is_string() || !f.contains("size")
            || !f["size"].is_number_unsigned()) {
            setError(error, pack.string() + " is damaged (a file's entry cannot be read).");
            return std::nullopt;
        }
        GamePackFile file{f["path"].get<std::string>(), f["size"].get<std::uint64_t>()};
        if (!safePath(file.path)) {
            setError(error, pack.string() + " names a file outside the game (" + file.path + ").");
            return std::nullopt;
        }
        index.files.push_back(std::move(file));
    }
    return index;
}

Status extractGamePack(const fs::path& pack, const fs::path& folder)
{
    std::string error;
    const auto index = readGamePackIndex(pack, &error);
    if (!index) {
        return Status::failure(error);
    }
    std::ifstream in(pack, std::ios::binary);
    in.seekg(static_cast<std::streamoff>(index->dataOffset));
    std::vector<char> buffer(1 << 16);
    std::error_code ec;
    for (const GamePackFile& f : index->files) {
        const fs::path target = folder / fs::path(std::u8string(f.path.begin(), f.path.end()));
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        std::uint64_t left = f.size;
        while (left > 0 && in && out) {
            const auto want = static_cast<std::streamsize>(std::min<std::uint64_t>(buffer.size(), left));
            in.read(buffer.data(), want);
            const std::streamsize got = in.gcount();
            if (got <= 0) {
                break;
            }
            out.write(buffer.data(), got);
            left -= static_cast<std::uint64_t>(got);
        }
        if (left != 0 || !out) {
            return Status::failure(left != 0 ? pack.string() + " is cut short." : "Could not write " + target.string() + ".");
        }
    }
    return Status::success();
}

} // namespace rolo
