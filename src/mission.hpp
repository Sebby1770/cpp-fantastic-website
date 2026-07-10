#pragma once

#include "util.hpp"

#include <algorithm>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace aster {

struct Palette {
    std::string id;
    std::string name;
    std::vector<std::string> colors;
};

inline const std::vector<Palette>& all_palettes() {
    static const std::vector<Palette> palettes = {
        {"ember", "Ember Desk", {"#171717", "#f7f2e8", "#247c76", "#d85d4c", "#c79a34", "#6e62a6"}},
        {"harbor", "Harbor Studio", {"#222226", "#f4efe3", "#2f6f9f", "#cf5b39", "#8fa34a", "#7b4f87"}},
        {"aurora", "Aurora Field", {"#161616", "#fbfaf6", "#18706a", "#b94f5f", "#d0a13f", "#476b9b"}},
        {"midnight", "Midnight Forge", {"#0f1115", "#e8eef7", "#3d7ea6", "#e07a5f", "#f2cc8f", "#81b29a"}},
        {"orchid", "Orchid Signal", {"#1a1423", "#f6f0ff", "#7b5ea7", "#e07a9a", "#f4c95f", "#4ecdc4"}},
    };
    return palettes;
}

inline std::string build_palettes_json() {
    std::ostringstream json;
    json << "{\"palettes\":[";
    const auto& palettes = all_palettes();
    for (std::size_t i = 0; i < palettes.size(); ++i) {
        if (i) json << ",";
        json << "{";
        json << "\"id\":\"" << json_escape(palettes[i].id) << "\",";
        json << "\"name\":\"" << json_escape(palettes[i].name) << "\",";
        json << "\"colors\":[";
        for (std::size_t c = 0; c < palettes[i].colors.size(); ++c) {
            if (c) json << ",";
            json << "\"" << palettes[i].colors[c] << "\"";
        }
        json << "]}";
    }
    json << "]}";
    return json.str();
}

inline std::string build_constellation_json(const std::map<std::string, std::string>& query) {
    const int seed = int_param(query, "seed", 42, 0, 1000000);
    const int points = int_param(query, "points", 24, 4, 128);

    std::mt19937 rng(static_cast<std::uint32_t>(seed) * 2654435761u + 97u);
    std::uniform_real_distribution<double> pos(0.05, 0.95);
    std::uniform_real_distribution<double> size_dist(1.5, 5.5);
    std::uniform_real_distribution<double> bright(0.35, 1.0);

    std::ostringstream json;
    json << std::fixed << std::setprecision(4);
    json << "{";
    json << "\"seed\":" << seed << ",";
    json << "\"points\":" << points << ",";
    json << "\"stars\":[";
    for (int i = 0; i < points; ++i) {
        if (i) json << ",";
        json << "{\"x\":" << pos(rng)
             << ",\"y\":" << pos(rng)
             << ",\"size\":" << size_dist(rng)
             << ",\"brightness\":" << bright(rng) << "}";
    }
    json << "],\"links\":[";
    for (int i = 0; i < points - 1; ++i) {
        if (i) json << ",";
        const int jump = 1 + static_cast<int>(rng() % std::min(4, points - 1));
        json << "[" << i << "," << ((i + jump) % points) << "]";
    }
    json << "]}";
    return json.str();
}

inline std::string build_mission_json(const std::map<std::string, std::string>& query) {
    const std::string seed_text = string_param(query, "seed", "sebby");
    const std::string mode = string_param(query, "mode", "pulse");
    const int intensity = int_param(query, "intensity", 68, 1, 100);
    const int tempo = int_param(query, "tempo", 42, 1, 100);
    const std::string palette_id = string_param(query, "palette", "");

    std::mt19937 rng(stable_seed(seed_text + ":" + mode + ":" + std::to_string(intensity) + ":" +
                                  std::to_string(tempo) + ":" + palette_id));
    auto pick = [&](const std::vector<std::string>& values) -> std::string {
        std::uniform_int_distribution<std::size_t> dist(0, values.size() - 1);
        return values[dist(rng)];
    };
    auto metric = [&](int base) {
        std::uniform_int_distribution<int> dist(-9, 14);
        return std::clamp(base + dist(rng), 1, 99);
    };

    const std::vector<std::string> prefixes = {"Aurora", "Vector", "Signal", "Lumen", "Civic",
                                               "Keystone", "Nova", "Harbor"};
    const std::vector<std::string> nouns = {"Circuit", "Studio", "Atlas", "Engine", "Desk",
                                            "Forge", "Field", "Pulse"};
    const std::vector<std::string> taglines = {
        "Turn rough sparks into a focused launch board.",
        "Shape a crisp interface around messy momentum.",
        "Make the next move visible, measurable, and satisfying.",
        "Blend strategy, rhythm, and craft into one working surface."
    };
    std::vector<std::string> priorities = {
        "Prototype the most useful interaction first",
        "Name the one metric that proves traction",
        "Polish the path from idea to visible result",
        "Keep the dashboard dense, calm, and quick to scan",
        "Ship a tiny loop that feels complete",
        "Use motion only where it clarifies state"
    };
    const std::vector<std::string> stages = {"Map", "Focus", "Build", "Tune", "Launch", "Learn"};

    const auto& palettes = all_palettes();
    std::size_t palette_index = static_cast<std::size_t>(stable_seed(mode) % palettes.size());
    if (!palette_id.empty()) {
        for (std::size_t i = 0; i < palettes.size(); ++i) {
            if (palettes[i].id == palette_id) {
                palette_index = i;
                break;
            }
        }
    }
    const Palette& palette = palettes[palette_index];

    std::ostringstream json;
    json << "{";
    json << "\"app\":\"AsterForge\",";
    json << "\"version\":\"" << "1.1.0" << "\",";
    json << "\"seed\":\"" << json_escape(seed_text) << "\",";
    json << "\"mode\":\"" << json_escape(mode) << "\",";
    json << "\"intensity\":" << intensity << ",";
    json << "\"tempo\":" << tempo << ",";
    json << "\"updatedAt\":\"" << current_time_iso() << "\",";
    json << "\"missionName\":\"" << pick(prefixes) << " " << pick(nouns) << "\",";
    json << "\"tagline\":\"" << json_escape(pick(taglines)) << "\",";
    json << "\"paletteId\":\"" << json_escape(palette.id) << "\",";
    json << "\"paletteName\":\"" << json_escape(palette.name) << "\",";

    json << "\"metrics\":[";
    const std::vector<std::pair<std::string, int>> metrics = {
        {"Momentum", metric(58 + intensity / 3)},
        {"Clarity", metric(54 + tempo / 4)},
        {"Delight", metric(62 + (intensity + tempo) / 8)},
        {"Risk", metric(34 + (100 - tempo) / 5)}
    };
    for (std::size_t i = 0; i < metrics.size(); ++i) {
        if (i) json << ",";
        json << "{\"label\":\"" << metrics[i].first << "\",\"value\":" << metrics[i].second
             << ",\"unit\":\"%\"}";
    }
    json << "],";

    json << "\"priorities\":[";
    std::shuffle(priorities.begin(), priorities.end(), rng);
    for (int i = 0; i < 4; ++i) {
        if (i) json << ",";
        json << "\"" << json_escape(priorities[static_cast<std::size_t>(i)]) << "\"";
    }
    json << "],";

    json << "\"waypoints\":[";
    for (std::size_t i = 0; i < stages.size(); ++i) {
        if (i) json << ",";
        const int score = metric(48 + static_cast<int>(i) * 6 + intensity / 8);
        const int minutes = 12 + static_cast<int>(i) * 7 + tempo / 9;
        json << "{\"label\":\"" << stages[i] << "\",\"minutes\":" << minutes << ",\"score\":" << score
             << "}";
    }
    json << "],";

    json << "\"palette\":[";
    for (std::size_t i = 0; i < palette.colors.size(); ++i) {
        if (i) json << ",";
        json << "\"" << palette.colors[i] << "\"";
    }
    json << "],";

    json << "\"nodes\":[";
    std::uniform_real_distribution<double> pos(0.08, 0.92);
    std::uniform_int_distribution<int> energy(28, 99);
    const int node_count = 18 + intensity / 8;
    for (int i = 0; i < node_count; ++i) {
        if (i) json << ",";
        json << std::fixed << std::setprecision(4);
        json << "{\"x\":" << pos(rng) << ",\"y\":" << pos(rng) << ",\"size\":" << (3 + energy(rng) % 8)
             << ",\"energy\":" << energy(rng) << "}";
    }
    json << "],";

    json << "\"links\":[";
    for (int i = 0; i < node_count - 1; ++i) {
        if (i) json << ",";
        const int jump = 1 + static_cast<int>(rng() % 4);
        json << "[" << i << "," << ((i + jump) % node_count) << "]";
    }
    json << "]";
    json << "}";
    return json.str();
}

}  // namespace aster
