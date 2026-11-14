// rolo_stress: plays the stress-test scenes without a window and reports how
// long gameplay takes per frame (README.md beside this file).
//
//   rolo_stress                         every scene, 600 frames each
//   rolo_stress --scene garbage         one scene
//   rolo_stress --frames 1200 --csv out.csv
//
// Each scene runs in a new game with fixed 1/60 s steps, after a warm-up,
// exactly as the player would run it, but without drawing. Times are wall
// time on this computer: compare runs made on the same computer.

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/content/ContentLibrary.hpp"
#include "rolo/core/Log.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/scene/SceneFile.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

const char* const kScenes[] = {"scripts", "garbage", "tidy", "vectors", "bullets", "physics", "sprites", "busy"};

struct Options {
    fs::path project = ROLO_STRESS_PROJECT;
    std::vector<std::string> scenes;
    int frames = 600;
    int warmup = 120;
    fs::path csv;
};

struct Result {
    std::string scene;
    std::size_t objects = 0;
    std::vector<double> frame;   // ms per frame (the whole Game::update)
    double behaviours = 0.0;     // mean ms
    double physics = 0.0;
    double other = 0.0;
    double memoryMb = 0.0;       // scripts' memory at the end
    double peakMemoryMb = 0.0;
    double allocatedKbPerFrame = 0.0;   // garbage made per frame (mean)
    int problems = 0;            // warnings and errors logged
};

const char* levelName(rolo::LogLevel level)
{
    return level >= rolo::LogLevel::Error ? "[ERROR]" : "[WARN]";
}

double percentile(std::vector<double> values, double p)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto i = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1) + 0.5);
    return values[std::min(i, values.size() - 1)];
}

double mean(const std::vector<double>& values)
{
    return values.empty() ? 0.0 : std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

int countOver(const std::vector<double>& values, double limit)
{
    return static_cast<int>(std::count_if(values.begin(), values.end(), [limit](double v) { return v > limit; }));
}

std::optional<Result> run(const Options& o, const std::string& name, rolo::AssetDatabase& assets,
                          const rolo::ComponentRegistry& registry, const rolo::ContentLibrary& content)
{
    Result r;
    r.scene = name;
    rolo::Logger log;
    log.addSink([&r](const rolo::LogMessage& m) {
        if (m.level >= rolo::LogLevel::Warning) {
            ++r.problems;
            if (r.problems <= 5) {
                std::cerr << "  " << levelName(m.level) << " " << m.category << ": " << m.text << "\n";
            }
        }
    });
    const rolo::AssetRecord* record = assets.findByPath("scenes/" + name + ".lscene");
    if (record == nullptr) {
        std::cerr << "No scene scenes/" << name << ".lscene in " << o.project.string() << "\n";
        return std::nullopt;
    }
    rolo::SceneLoadResult loaded = rolo::loadSceneFile(assets.absolutePath(*record), registry);
    if (!loaded.ok()) {
        std::cerr << "Could not read scenes/" << name << ".lscene\n";
        return std::nullopt;
    }
    r.objects = loaded.scene->size();

    rolo::GameSetup setup;
    setup.assets = &assets;
    setup.content = &content;
    rolo::Game game(log, std::move(setup));
    game.start(*loaded.scene, record->id);
    constexpr float kStep = 1.0f / 60.0f;
    for (int i = 0; i < o.warmup; ++i) {
        game.update(kStep);
    }
    r.frame.reserve(static_cast<std::size_t>(o.frames));
    const std::uint64_t allocatedBefore = game.scriptMemoryAllocated();
    for (int i = 0; i < o.frames; ++i) {
        const auto before = std::chrono::steady_clock::now();
        game.update(kStep);
        const auto after = std::chrono::steady_clock::now();
        r.frame.push_back(std::chrono::duration<double, std::milli>(after - before).count());
        const rolo::FrameTimings& t = game.lastFrameTimings();
        r.behaviours += t.behaviours;
        r.physics += t.physics;
        r.other += t.other;
        r.peakMemoryMb = std::max(r.peakMemoryMb, static_cast<double>(game.scriptMemory()) / (1024.0 * 1024.0));
    }
    const auto n = static_cast<double>(std::max(o.frames, 1));
    r.behaviours /= n;
    r.physics /= n;
    r.other /= n;
    r.memoryMb = static_cast<double>(game.scriptMemory()) / (1024.0 * 1024.0);
    r.allocatedKbPerFrame = static_cast<double>(game.scriptMemoryAllocated() - allocatedBefore) / 1024.0 / n;
    return r;
}

void usage()
{
    std::cout << "rolo_stress: measures gameplay frame times in the stress-test scenes.\n\n"
                 "  --project DIR   the stress project (default: examples/stress/project)\n"
                 "  --scene NAME    only this scene (scripts, garbage, tidy, vectors, bullets, physics, sprites, busy);\n"
                 "                  may be given more than once\n"
                 "  --frames N      frames measured per scene (default 600: ten seconds)\n"
                 "  --warmup N      frames run first, not measured (default 120)\n"
                 "  --csv FILE      also write every frame's time to FILE\n";
}

} // namespace

int main(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool hasValue = i + 1 < argc;
        if (a == "--help" || a == "-h") {
            usage();
            return 0;
        }
        if (a == "--project" && hasValue) {
            o.project = argv[++i];
        } else if (a == "--scene" && hasValue) {
            o.scenes.emplace_back(argv[++i]);
        } else if ((a == "--frames" || a == "--warmup") && hasValue) {
            const int v = std::atoi(argv[++i]);
            if (v < (a == "--frames" ? 1 : 0)) {
                std::cerr << a << " needs a number" << (a == "--frames" ? " of at least 1" : "") << "\n";
                return 2;
            }
            (a == "--frames" ? o.frames : o.warmup) = v;
        } else if (a == "--csv" && hasValue) {
            o.csv = argv[++i];
        } else {
            std::cerr << "Unknown option " << a << " (see --help)\n";
            return 2;
        }
    }
    if (o.scenes.empty()) {
        o.scenes.assign(std::begin(kScenes), std::end(kScenes));
    }

    rolo::ComponentRegistry registry = rolo::ComponentRegistry::withBuiltins();
    rolo::registerGameplayComponents(registry);
    rolo::AssetDatabase assets(o.project, rolo::AssetDatabase::Access::ReadOnly);
    assets.refresh();
    const rolo::ContentLibrary content = rolo::ContentLibrary::load(assets, nullptr);

    std::vector<Result> results;
    for (const std::string& scene : o.scenes) {
        std::cout << "Running " << scene << "..." << std::flush;
        auto r = run(o, scene, assets, registry, content);
        if (!r) {
            return 1;
        }
        std::cout << " done\n";
        results.push_back(std::move(*r));
    }

    std::printf("\nGameplay time per frame, in milliseconds (%d frames per scene, after %d to warm up).\n"
                "A frame at 60 fps has 16.7 ms for everything, drawing included.\n\n",
                o.frames, o.warmup);
    std::printf("%-9s %8s %7s %7s %7s %7s %6s %6s %8s %8s %8s %7s %9s\n", "scene", "objects", "mean", "median", "p99",
                "worst", ">8ms", ">16ms", "scripts", "physics", "other", "Lua MB", "KB/frame");
    int problems = 0;
    for (const Result& r : results) {
        std::printf("%-9s %8zu %7.2f %7.2f %7.2f %7.2f %6d %6d %8.2f %8.2f %8.2f %7.1f %9.0f\n", r.scene.c_str(),
                    r.objects, mean(r.frame), percentile(r.frame, 0.5), percentile(r.frame, 0.99),
                    *std::max_element(r.frame.begin(), r.frame.end()), countOver(r.frame, 8.0),
                    countOver(r.frame, 1000.0 / 60.0), r.behaviours, r.physics, r.other, r.peakMemoryMb,
                    r.allocatedKbPerFrame);
        problems += r.problems;
    }
    std::printf("\nscripts = behaviours and Lua scripts; Lua MB = the most the scripts held at a frame's end;\n"
                "KB/frame = memory the scripts asked for per frame, nearly all of it garbage.\n");

    if (!o.csv.empty()) {
        std::ofstream out(o.csv);
        out << "scene,frame,ms\n";
        for (const Result& r : results) {
            for (std::size_t i = 0; i < r.frame.size(); ++i) {
                out << r.scene << "," << i << "," << r.frame[i] << "\n";
            }
        }
        std::cout << "Every frame's time: " << o.csv.string() << "\n";
    }
    if (problems > 0) {
        std::cerr << problems << " warnings or errors were logged: the scenes did not run as meant.\n";
        return 1;
    }
    return 0;
}
