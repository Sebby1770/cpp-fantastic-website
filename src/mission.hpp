#pragma once

#include "util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
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

inline std::string rgb_hex(int r, int g, int b) {
    std::ostringstream out;
    out << '#' << std::hex << std::nouppercase << std::setfill('0') << std::setw(2)
        << std::clamp(r, 0, 255) << std::setw(2) << std::clamp(g, 0, 255) << std::setw(2)
        << std::clamp(b, 0, 255);
    return out.str();
}

inline std::string hsl_to_hex(double hue, double sat, double light) {
    sat = std::clamp(sat, 0.0, 1.0);
    light = std::clamp(light, 0.0, 1.0);
    const double chroma = (1.0 - std::abs(2.0 * light - 1.0)) * sat;
    double hp = std::fmod(hue, 360.0);
    if (hp < 0.0) {
        hp += 360.0;
    }
    hp /= 60.0;
    const double x = chroma * (1.0 - std::abs(std::fmod(hp, 2.0) - 1.0));
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    if (hp < 1.0) {
        r = chroma;
        g = x;
    } else if (hp < 2.0) {
        r = x;
        g = chroma;
    } else if (hp < 3.0) {
        g = chroma;
        b = x;
    } else if (hp < 4.0) {
        g = x;
        b = chroma;
    } else if (hp < 5.0) {
        r = x;
        b = chroma;
    } else {
        r = chroma;
        b = x;
    }
    const double m = light - chroma / 2.0;
    const auto to_byte = [](double channel) {
        return std::clamp(static_cast<int>(std::lround(channel * 255.0)), 0, 255);
    };
    return rgb_hex(to_byte(r + m), to_byte(g + m), to_byte(b + m));
}

inline std::string build_sky_json(const std::map<std::string, std::string>& query) {
    const std::string seed_text = string_param(query, "seed", "sebby");
    const int layers = int_param(query, "layers", 4, 2, 8);

    std::mt19937 rng(stable_seed(seed_text + ":sky:" + std::to_string(layers)));
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_real_distribution<double> hue_dist(0.0, 360.0);
    std::uniform_real_distribution<double> sat_dist(28.0, 92.0);
    std::uniform_real_distribution<double> light_dist(18.0, 72.0);
    std::uniform_real_distribution<double> pos(0.08, 0.92);
    std::uniform_real_distribution<double> radius_dist(0.16, 0.78);
    std::uniform_real_distribution<double> alpha_dist(0.12, 0.62);
    std::uniform_int_distribution<int> dust_dist(20, 220);

    std::ostringstream json;
    json << std::fixed << std::setprecision(4);
    json << "{";
    json << "\"seed\":\"" << json_escape(seed_text) << "\",";
    json << "\"version\":\"" << kVersion << "\",";
    json << "\"haze\":" << unit(rng) << ",";
    json << "\"layers\":[";
    for (int i = 0; i < layers; ++i) {
        if (i) json << ",";
        json << "{\"hue\":" << hue_dist(rng)
             << ",\"sat\":" << sat_dist(rng)
             << ",\"light\":" << light_dist(rng)
             << ",\"x\":" << pos(rng)
             << ",\"y\":" << pos(rng)
             << ",\"radius\":" << radius_dist(rng)
             << ",\"alpha\":" << alpha_dist(rng) << "}";
    }
    json << "],";
    json << "\"dust\":" << dust_dist(rng) << ",";
    json << "\"aurora\":{";
    json << "\"enabled\":" << (unit(rng) > 0.32 ? "true" : "false") << ",";
    json << "\"hue\":" << hue_dist(rng) << ",";
    json << "\"amplitude\":" << unit(rng) << ",";
    json << "\"speed\":" << unit(rng);
    json << "}";
    json << "}";
    return json.str();
}

inline const std::vector<std::string>& planet_names() {
    static const std::vector<std::string> names = {
        "Aether", "Nyx", "Helios", "Selene", "Atlas", "Lyra", "Vega", "Rigel",
        "Electra", "Thalassa", "Hyperion", "Andromeda", "Cassiopeia", "Orion",
        "Perseus", "Io"};
    return names;
}

inline std::string build_orbit_json(const std::map<std::string, std::string>& query) {
    const int seed = int_param(query, "seed", 42, 0, 1000000);
    const int planet_count = int_param(query, "planets", 6, 3, 10);

    std::mt19937 rng(static_cast<std::uint32_t>(seed) * 2654435761u + 1337u);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_real_distribution<double> radius_dist(0.01, 0.05);
    std::uniform_real_distribution<double> phase_dist(0.0, 6.2832);
    std::uniform_int_distribution<int> moon_dist(0, 3);

    std::vector<std::string> names = planet_names();
    std::shuffle(names.begin(), names.end(), rng);

    const double star_hue = 32.0 + unit(rng) * 28.0;
    const std::string star_color = hsl_to_hex(star_hue, 0.82 + unit(rng) * 0.16, 0.62 + unit(rng) * 0.12);
    const double star_radius = 0.07 + unit(rng) * 0.03;
    const double star_flare = 0.18 + unit(rng) * 0.62;

    std::ostringstream json;
    json << std::fixed << std::setprecision(4);
    json << "{";
    json << "\"seed\":" << seed << ",";
    json << "\"version\":\"" << kVersion << "\",";
    json << "\"star\":{";
    json << "\"color\":\"" << star_color << "\",";
    json << "\"radius\":" << star_radius << ",";
    json << "\"flare\":" << star_flare;
    json << "},";
    json << "\"planets\":[";
    const double span = 0.80;
    const double gap = planet_count > 1 ? span / (planet_count - 1) : 0.0;
    for (int i = 0; i < planet_count; ++i) {
        if (i) json << ",";
        const double jitter = (unit(rng) - 0.5) * gap * 0.22;
        const double orbit = std::clamp(0.12 + gap * static_cast<double>(i) + jitter, 0.12, 0.92);
        const double period = std::clamp(4.0 + 36.0 * ((orbit - 0.12) / span) + (unit(rng) - 0.5) * 3.0,
                                         4.0, 40.0);
        const double hue = unit(rng) * 360.0;
        const std::string color = hsl_to_hex(hue, 0.42 + unit(rng) * 0.45, 0.38 + unit(rng) * 0.28);
        const std::string name = names[static_cast<std::size_t>(i) % names.size()];
        json << "{";
        json << "\"name\":\"" << json_escape(name) << "\",";
        json << "\"color\":\"" << color << "\",";
        json << "\"orbit\":" << orbit << ",";
        json << "\"radius\":" << radius_dist(rng) << ",";
        json << "\"period\":" << period << ",";
        json << "\"phase\":" << phase_dist(rng) << ",";
        json << "\"moons\":" << moon_dist(rng) << ",";
        json << "\"ring\":" << (unit(rng) < 0.22 ? "true" : "false");
        json << "}";
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
    json << "\"version\":\"" << kVersion << "\",";
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
