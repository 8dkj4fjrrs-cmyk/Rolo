// rolo_pack — packs a project folder into one file, a game pack.
//
//   rolo_pack PROJECT_FOLDER OUTPUT.pack
//
// The pack holds every file of the project except hidden ones (names
// starting with '.'), as the editor's web export makes it. See
// docs/formats/game-pack.md.

#include "rolo/assets/GamePack.hpp"
#include "rolo/scene/Project.hpp"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "Usage: rolo_pack PROJECT_FOLDER OUTPUT.pack\n"
                     "Packs a Rolo project (the folder with project.rolo) into one game pack file.\n";
        return 2;
    }
    const std::filesystem::path folder = argv[1];
    const std::filesystem::path output = argv[2];

    std::string error;
    const auto project = rolo::Project::open(folder, &error);
    if (!project) {
        std::cerr << "Error: " << error << "\n";
        return 1;
    }
    const std::string name = project->settings().name;
    // No .git, .DS_Store and the like: hidden files are not part of a game.
    const auto notHidden = [](const std::filesystem::path& path) {
        const std::string file = path.filename().string();
        return file.empty() || file.front() != '.';
    };
    const rolo::GamePackResult packed =
        rolo::writeGamePack(folder, output, name, rolo::gamePackSavesName(name), notHidden);
    if (!packed.status) {
        std::cerr << "Error: " << packed.status.message << "\n";
        return 1;
    }
    std::cout << "Packed '" << name << "': " << packed.files << " files, " << packed.bytes << " bytes, into "
              << output.string() << "\n";
    return 0;
}
