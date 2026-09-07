#pragma once

#include "json.hpp"
#include "util.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace aster {

struct Preset {
    std::string id;
    std::string label;
    std::string description;
    std::string seed;
    int intensity;
    int tempo;
    int density;
};

struct MissionNode {
    double x = 0.5;
    double y = 0.5;
    double z = 0.5;
    double size = 4;
    int energy = 50;
    double phase = 0;
    int kind = 0;
};

struct MissionMetric {
    std::string label;
    int value = 0;
    std::string unit = "%";
};

struct MissionWaypoint {
    std::string label;
    int minutes = 0;
    int score = 0;
};

struct MissionRing {
    double x = 0.5;
    double y = 0.5;
    double z = 0.5;
    double radius = 0.2;
    double speed = 0.2;
    int color = 2;
};

struct MissionPacket {
    std::string app = kService;
    std::string shortId;
    std::string seed;
    std::string mode;
    int intensity = 68;
    int tempo = 54;
    int density = 72;
    std::string updatedAt;
    std::string missionName;
    std::string tagline;
    std::string weather;
    std::string paletteName;
    std::string signature;
    std::vector<MissionMetric> metrics;
    std::vector<std::string> priorities;
    std::vector<MissionWaypoint> waypoints;
    std::vector<std::string> notes;
    std::vector<std::string> palette;
    std::vector<MissionNode> nodes;
    std::vector<std::array<int, 3>> links;
    std::vector<MissionRing> rings;
};

inline const std::vector<Preset>& presets() {
    static const std::vector<Preset> values = {
        {"orbit", "Orbit", "Balanced routes with readable movement.", "sebby-orbit", 68, 54, 72},
        {"bloom", "Bloom", "Dense color, high energy, and wider clusters.", "bloom-signal", 82, 48,
         88},
        {"forge", "Forge", "Sharper tension with faster route pressure.", "forge-line", 76, 74, 66},
        {"night", "Night", "Slower motion and sparse late-session focus.", "night-map", 46, 34, 42},
        {"pulse", "Pulse", "Rhythmic bursts with tight clusters and high tempo.", "pulse-core", 88,
         81, 58},
        {"drift", "Drift", "Slow lateral wander with sparse luminous fields.", "drift-field", 38, 22,
         54},
    };
    return values;
}

inline bool is_supported_mode(const std::string& mode) {
    const auto& values = presets();
    return std::any_of(values.begin(), values.end(),
                       [&](const Preset& preset) { return preset.id == mode; });
}

inline std::string normalize_mode(const std::string& value) {
    const std::string mode = to_lower(value);
    return is_supported_mode(mode) ? mode : "orbit";
}

inline std::string short_id(const std::string& value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setw(8) << std::setfill('0') << stable_seed(value);
    return out.str();
}

inline const std::vector<std::pair<std::string, std::vector<std::string>>>& named_palettes() {
    static const std::vector<std::pair<std::string, std::vector<std::string>>> values = {
        {"emberglass",
         {"#080908", "#f7f0df", "#10b8a6", "#ef5e4d", "#f2b544", "#8c6cf5", "#77c66e"}},
        {"tidewire",
         {"#071013", "#edf7f2", "#1f8fb3", "#ff6b57", "#d9b847", "#4bbf83", "#d46fb0"}},
        {"citrus-noir",
         {"#0b0b10", "#fff4ce", "#95d839", "#ff5d35", "#49a7ff", "#b877ff", "#f1c232"}},
        {"violet-oxide",
         {"#100c13", "#f3efe7", "#b888ff", "#d85f7d", "#43c6a8", "#f0aa3b", "#6ea8fe"}},
        {"nova-pulse",
         {"#12060c", "#f6e6ef", "#ff3d81", "#39d0ff", "#ffe16a", "#b388ff", "#4dffc0"}},
        {"driftwood",
         {"#0a1014", "#e4eef2", "#6aa7c8", "#c48b5a", "#9ad0b1", "#7a8cff", "#d2c4a8"}},
    };
    return values;
}

inline std::string mission_key(const std::string& seed, const std::string& mode, int intensity,
                               int tempo, int density) {
    return seed + ":" + mode + ":" + std::to_string(intensity) + ":" + std::to_string(tempo) + ":" +
           std::to_string(density);
}

inline MissionPacket generate_mission(const std::map<std::string, std::string>& query) {
    MissionPacket packet;
    packet.seed = string_param(query, "seed", "sebby");
    packet.mode = normalize_mode(string_param(query, "mode", "orbit"));
    packet.intensity = int_param(query, "intensity", 68, 1, 100);
    packet.tempo = int_param(query, "tempo", 54, 1, 100);
    packet.density = int_param(query, "density", 72, 1, 100);
    const std::string key =
        mission_key(packet.seed, packet.mode, packet.intensity, packet.tempo, packet.density);
    packet.shortId = short_id(key);
    packet.updatedAt = current_time_iso();

    std::mt19937 rng(stable_seed(key));
    auto pick = [&](const std::vector<std::string>& values) -> std::string {
        std::uniform_int_distribution<std::size_t> dist(0, values.size() - 1);
        return values[dist(rng)];
    };
    auto metric = [&](int base) {
        std::uniform_int_distribution<int> dist(-9, 14);
        return clamp_int(base + dist(rng), 1, 99);
    };

    const std::vector<std::string> prefixes = {"Velvet",  "Copper",  "Solar",   "Nocturne",
                                               "Meridian", "Echo",    "Glass",   "Kinetic",
                                               "Civic",    "Prism",   "Nimbus",  "Helix"};
    const std::vector<std::string> nouns = {"Observatory", "Relay",  "Garden", "Cartograph",
                                            "Engine",      "Harbor", "Foundry", "Signal",
                                            "Atelier",     "Beacon", "Vault",   "Array"};
    const std::vector<std::string> taglines = {
        "A living map for turning scattered sparks into a launchable shape.",
        "A quiet command surface for seeing momentum before it becomes obvious.",
        "A cinematic forge for tracing the route from instinct to shipped work.",
        "A signal garden where ideas, risks, and next moves move in one rhythm.",
        "A native C++ atlas that makes the invisible structure of a project glow.",
        "A 3.0 observatory for watching a constellation assemble itself in real time."};
    std::vector<std::string> priorities = {
        "Prototype the interaction that makes the whole idea feel inevitable",
        "Name the one metric that proves the signal is real",
        "Polish the path from first touch to visible result",
        "Keep the surface dense, calm, and quick to scan",
        "Ship a tiny loop that feels complete in the hand",
        "Use motion only where it clarifies state",
        "Turn the riskiest assumption into a small live test",
        "Make the strongest visual moment carry useful information"};
    std::vector<std::string> notes = {
        "The center cluster is strong enough to become the first demo moment.",
        "The route map favors one decisive release over a wide feature spread.",
        "Momentum rises when the interface gives immediate visual feedback.",
        "The quietest risk is discoverability; make the first action unmistakable.",
        "There is enough visual identity here to support a memorable product name.",
        "The densest nodes should become the first three build tickets."};
    const std::vector<std::string> stages = {"Spark", "Shape", "Wire", "Stress",
                                             "Reveal", "Launch", "Echo"};
    const std::vector<std::string> weather = {"clear signal", "charged air", "useful tension",
                                              "clean pressure", "late-night focus", "bright friction",
                                              "pulse weather", "slow drift"};

    packet.missionName = pick(prefixes) + " " + pick(nouns);
    packet.tagline = pick(taglines);
    packet.weather = pick(weather);

    packet.metrics = {
        {"Velocity", metric(44 + packet.intensity / 2), "%"},
        {"Clarity", metric(50 + packet.tempo / 3), "%"},
        {"Wonder", metric(48 + (packet.intensity + packet.density) / 5), "%"},
        {"Tension", metric(30 + (100 - packet.tempo) / 4), "%"},
        {"Finish", metric(38 + (packet.tempo + packet.density) / 5), "%"},
    };

    std::shuffle(priorities.begin(), priorities.end(), rng);
    packet.priorities.assign(priorities.begin(), priorities.begin() + 5);

    for (std::size_t i = 0; i < stages.size(); ++i) {
        MissionWaypoint waypoint;
        waypoint.label = stages[i];
        waypoint.score = metric(42 + static_cast<int>(i) * 7 + packet.intensity / 9);
        waypoint.minutes = 9 + static_cast<int>(i) * 6 + packet.tempo / 10;
        packet.waypoints.push_back(waypoint);
    }

    std::shuffle(notes.begin(), notes.end(), rng);
    packet.notes.assign(notes.begin(), notes.begin() + 3);

    const auto& palettes = named_palettes();
    const int palette_index =
        static_cast<int>(stable_seed(packet.mode + packet.seed) % palettes.size());
    packet.paletteName = palettes[static_cast<std::size_t>(palette_index)].first;
    packet.palette = palettes[static_cast<std::size_t>(palette_index)].second;

    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_real_distribution<double> phase(0.0, 6.2832);
    std::uniform_int_distribution<int> energy(28, 99);
    std::uniform_int_distribution<int> node_type(0, 3);
    const int node_count = 20 + packet.density / 5 + packet.intensity / 12;
    packet.nodes.reserve(static_cast<std::size_t>(node_count));

    for (int i = 0; i < node_count; ++i) {
        MissionNode node;
        const double t = static_cast<double>(i) / std::max(1, node_count - 1);
        double x = 0.08 + unit(rng) * 0.84;
        double y = 0.08 + unit(rng) * 0.84;
        double z = 0.08 + unit(rng) * 0.84;
        if (packet.mode == "orbit") {
            const double angle = t * 6.2832 + unit(rng) * 0.35;
            const double radius = 0.18 + 0.28 * unit(rng);
            x = 0.5 + std::cos(angle) * radius;
            y = 0.5 + (unit(rng) - 0.5) * 0.22;
            z = 0.5 + std::sin(angle) * radius;
        } else if (packet.mode == "bloom") {
            const double angle = unit(rng) * 6.2832;
            const double radius = std::pow(unit(rng), 0.55) * 0.42;
            x = 0.5 + std::cos(angle) * radius;
            y = 0.5 + (unit(rng) - 0.5) * 0.55;
            z = 0.5 + std::sin(angle) * radius * 0.85;
        } else if (packet.mode == "forge") {
            x = 0.12 + t * 0.76 + (unit(rng) - 0.5) * 0.08;
            y = 0.5 + std::sin(t * 9.0) * 0.22 + (unit(rng) - 0.5) * 0.08;
            z = 0.5 + std::cos(t * 6.0) * 0.18;
        } else if (packet.mode == "night") {
            x = 0.18 + unit(rng) * 0.28 + (i % 2) * 0.32;
            y = 0.2 + unit(rng) * 0.6;
            z = 0.22 + unit(rng) * 0.4;
        } else if (packet.mode == "pulse") {
            const int shell = i % 4;
            const double angle = t * 12.566 + unit(rng) * 0.4;
            const double radius = 0.12 + shell * 0.1;
            x = 0.5 + std::cos(angle) * radius;
            y = 0.5 + std::sin(t * 8.0) * 0.16;
            z = 0.5 + std::sin(angle) * radius;
        } else if (packet.mode == "drift") {
            x = 0.1 + t * 0.8 + (unit(rng) - 0.5) * 0.06;
            y = 0.18 + (i % 3) * 0.22 + unit(rng) * 0.12;
            z = 0.2 + unit(rng) * 0.6;
        }
        node.x = clamp_value(x, 0.0, 1.0);
        node.y = clamp_value(y, 0.0, 1.0);
        node.z = clamp_value(z, 0.0, 1.0);
        node.size = 3 + energy(rng) % 8;
        node.energy = energy(rng);
        node.phase = phase(rng);
        node.kind = node_type(rng);
        packet.nodes.push_back(node);
    }

    const int link_count = node_count + packet.density / 3;
    packet.links.reserve(static_cast<std::size_t>(link_count));
    for (int i = 0; i < link_count; ++i) {
        const int start = static_cast<int>(rng() % static_cast<unsigned>(node_count));
        const int jump = 1 + static_cast<int>(rng() % 7);
        const int strength = 24 + static_cast<int>(rng() % 76);
        packet.links.push_back({start, (start + jump) % node_count, strength});
    }

    for (int i = 0; i < 5; ++i) {
        MissionRing ring;
        ring.x = 0.08 + unit(rng) * 0.84;
        ring.y = 0.08 + unit(rng) * 0.84;
        ring.z = 0.08 + unit(rng) * 0.84;
        ring.radius = 0.16 + 0.05 * i + (packet.density % 8) / 100.0;
        ring.speed = 0.18 + (rng() % 60) / 100.0;
        ring.color = 2 + i % 5;
        packet.rings.push_back(ring);
    }

    packet.signature = packet.seed + " / " + packet.mode + " / " + packet.weather;
    return packet;
}

inline std::string mission_to_json(const MissionPacket& packet) {
    Json::Arr metrics;
    for (const auto& metric : packet.metrics) {
        Json::Obj row;
        row.kv("label", metric.label);
        row.kv("value", metric.value);
        row.kv("unit", metric.unit);
        metrics.push(row.done());
    }
    Json::Arr priorities;
    for (const auto& item : packet.priorities) {
        priorities.push(item);
    }
    Json::Arr waypoints;
    for (const auto& waypoint : packet.waypoints) {
        Json::Obj row;
        row.kv("label", waypoint.label);
        row.kv("minutes", waypoint.minutes);
        row.kv("score", waypoint.score);
        waypoints.push(row.done());
    }
    Json::Arr notes;
    for (const auto& note : packet.notes) {
        notes.push(note);
    }
    Json::Arr palette;
    for (const auto& color : packet.palette) {
        palette.push(color);
    }
    Json::Arr nodes;
    for (const auto& node : packet.nodes) {
        Json::Obj row;
        row.kv("x", node.x);
        row.kv("y", node.y);
        row.kv("z", node.z);
        row.kv("size", node.size);
        row.kv("energy", node.energy);
        row.kv("phase", node.phase);
        row.kv("kind", node.kind);
        nodes.push(row.done());
    }
    Json::Arr links;
    for (const auto& link : packet.links) {
        Json::Arr row;
        row.push(link[0]);
        row.push(link[1]);
        row.push(link[2]);
        links.push(row.done());
    }
    Json::Arr rings;
    for (const auto& ring : packet.rings) {
        Json::Obj row;
        row.kv("x", ring.x);
        row.kv("y", ring.y);
        row.kv("z", ring.z);
        row.kv("radius", ring.radius);
        row.kv("speed", ring.speed);
        row.kv("color", ring.color);
        rings.push(row.done());
    }

    Json::Obj root;
    root.kv("app", packet.app);
    root.kv("shortId", packet.shortId);
    root.kv("seed", packet.seed);
    root.kv("mode", packet.mode);
    root.kv("intensity", packet.intensity);
    root.kv("tempo", packet.tempo);
    root.kv("density", packet.density);
    root.kv("updatedAt", packet.updatedAt);
    root.kv("missionName", packet.missionName);
    root.kv("tagline", packet.tagline);
    root.kv("weather", packet.weather);
    root.kv("metrics", metrics.done());
    root.kv("priorities", priorities.done());
    root.kv("waypoints", waypoints.done());
    root.kv("notes", notes.done());
    root.kv("palette", palette.done());
    root.kv("paletteName", packet.paletteName);
    root.kv("nodes", nodes.done());
    root.kv("links", links.done());
    root.kv("rings", rings.done());
    root.kv("signature", packet.signature);
    return root.done().str();
}

inline std::string build_mission_json(const std::map<std::string, std::string>& query) {
    return mission_to_json(generate_mission(query));
}

inline std::string build_presets_json() {
    Json::Arr list;
    for (const auto& preset : presets()) {
        Json::Obj row;
        row.kv("id", preset.id);
        row.kv("label", preset.label);
        row.kv("description", preset.description);
        row.kv("seed", preset.seed);
        row.kv("intensity", preset.intensity);
        row.kv("tempo", preset.tempo);
        row.kv("density", preset.density);
        list.push(row.done());
    }
    Json::Obj root;
    root.kv("presets", list.done());
    return root.done().str();
}

inline std::string build_share_json(const std::map<std::string, std::string>& query) {
    const std::string seed_text = string_param(query, "seed", "sebby");
    const std::string mode = normalize_mode(string_param(query, "mode", "orbit"));
    const int intensity = int_param(query, "intensity", 68, 1, 100);
    const int tempo = int_param(query, "tempo", 54, 1, 100);
    const int density = int_param(query, "density", 72, 1, 100);
    const std::string id = short_id(mission_key(seed_text, mode, intensity, tempo, density));

    std::ostringstream path;
    path << "/?seed=" << url_encode(seed_text) << "&mode=" << url_encode(mode)
         << "&intensity=" << intensity << "&tempo=" << tempo << "&density=" << density;

    Json::Obj config;
    config.kv("seed", seed_text);
    config.kv("mode", mode);
    config.kv("intensity", intensity);
    config.kv("tempo", tempo);
    config.kv("density", density);

    Json::Obj root;
    root.kv("shortId", id);
    root.kv("path", path.str());
    root.kv("config", config.done());
    return root.done().str();
}

inline std::string build_sky_json(const std::map<std::string, std::string>& query) {
    const std::string seed_text = string_param(query, "seed", "sebby");
    const int layers_n = int_param(query, "layers", 4, 1, 8);
    std::mt19937 rng(stable_seed("sky:" + seed_text + ":" + std::to_string(layers_n)));
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const auto& palettes = named_palettes();
    const auto& palette = palettes[stable_seed(seed_text) % palettes.size()].second;

    Json::Arr layers;
    for (int i = 0; i < layers_n; ++i) {
        Json::Arr blobs;
        const int blob_count = 4 + static_cast<int>(rng() % 5);
        for (int b = 0; b < blob_count; ++b) {
            Json::Obj blob;
            blob.kv("x", 0.08 + unit(rng) * 0.84);
            blob.kv("y", 0.08 + unit(rng) * 0.84);
            blob.kv("z", 0.08 + unit(rng) * 0.84);
            blob.kv("r", 0.12 + unit(rng) * 0.28);
            blob.kv("e", 0.35 + unit(rng) * 0.65);
            blobs.push(blob.done());
        }
        Json::Obj layer;
        layer.kv("id", i);
        layer.kv("color", palette[2 + (i % 5)]);
        layer.kv("alpha", 0.10 + i * 0.03 + unit(rng) * 0.08);
        layer.kv("scale", 0.9 + unit(rng) * 1.4);
        layer.kv("drift", 0.04 + unit(rng) * 0.18);
        layer.kv("blobs", blobs.done());
        layers.push(layer.done());
    }
    Json::Obj root;
    root.kv("seed", seed_text);
    root.kv("layers", layers.done());
    return root.done().str();
}

inline std::string build_orbit_json(const std::map<std::string, std::string>& query) {
    const std::string seed_text = string_param(query, "seed", "sebby");
    const int planet_n = int_param(query, "planets", 5, 1, 12);
    std::mt19937 rng(stable_seed("orbit:" + seed_text + ":" + std::to_string(planet_n)));
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const auto& palettes = named_palettes();
    const auto& palette = palettes[stable_seed(seed_text + ":orbit") % palettes.size()].second;
    const std::vector<std::string> names = {"Kepler", "Vesper", "Helion", "Nyx",   "Iota",
                                            "Mir",    "Calder", "Quill",  "Sable", "Apex",
                                            "Lumen",  "Rook"};

    Json::Obj star;
    star.kv("color", palette[4]);
    star.kv("radius", 0.07 + unit(rng) * 0.04);

    Json::Arr planets;
    for (int i = 0; i < planet_n; ++i) {
        Json::Arr moons;
        const int moon_n = static_cast<int>(rng() % 3);
        for (int m = 0; m < moon_n; ++m) {
            Json::Obj moon;
            moon.kv("radius", 0.006 + unit(rng) * 0.01);
            moon.kv("orbit", 0.04 + unit(rng) * 0.05);
            moon.kv("period", 2.0 + unit(rng) * 8.0);
            moon.kv("color", palette[1]);
            moons.push(moon.done());
        }
        Json::Obj planet;
        planet.kv("name", names[static_cast<std::size_t>(i) % names.size()]);
        planet.kv("radius", 0.018 + unit(rng) * 0.03);
        planet.kv("orbit", 0.22 + i * 0.12 + unit(rng) * 0.04);
        planet.kv("period", 8.0 + i * 3.5 + unit(rng) * 6.0);
        planet.kv("color", palette[2 + (i % 5)]);
        planet.kv("inclination", (unit(rng) - 0.5) * 0.45);
        planet.kv("moons", moons.done());
        planets.push(planet.done());
    }

    Json::Obj root;
    root.kv("seed", seed_text);
    root.kv("star", star.done());
    root.kv("planets", planets.done());
    return root.done().str();
}

inline std::string build_constellation_json(const std::map<std::string, std::string>& query) {
    const std::string seed_text = string_param(query, "seed", "sebby");
    const int point_n = int_param(query, "points", 24, 8, 80);
    std::mt19937 rng(stable_seed("constellation:" + seed_text + ":" + std::to_string(point_n)));
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const std::vector<std::string> labels = {"Alpha", "Beta",  "Gamma", "Delta", "Epsilon",
                                             "Zeta",  "Eta",   "Theta", "Iota",  "Kappa"};

    Json::Arr points;
    for (int i = 0; i < point_n; ++i) {
        Json::Obj point;
        point.kv("x", 0.06 + unit(rng) * 0.88);
        point.kv("y", 0.06 + unit(rng) * 0.88);
        point.kv("z", 0.06 + unit(rng) * 0.88);
        point.kv("label", labels[static_cast<std::size_t>(i) % labels.size()] +
                              std::to_string(i + 1));
        point.kv("mag", 0.35 + unit(rng) * 0.65);
        points.push(point.done());
    }
    Json::Arr edges;
    const int edge_n = point_n + point_n / 3;
    for (int i = 0; i < edge_n; ++i) {
        const int a = static_cast<int>(rng() % static_cast<unsigned>(point_n));
        const int b = (a + 1 + static_cast<int>(rng() % 5)) % point_n;
        Json::Arr edge;
        edge.push(a);
        edge.push(b);
        edges.push(edge.done());
    }
    Json::Obj root;
    root.kv("seed", seed_text);
    root.kv("points", points.done());
    root.kv("edges", edges.done());
    return root.done().str();
}

inline std::string build_catalog_json() {
    Json::Arr modes;
    for (const auto& preset : presets()) {
        Json::Obj row;
        row.kv("id", preset.id);
        row.kv("label", preset.label);
        row.kv("description", preset.description);
        modes.push(row.done());
    }
    Json::Arr palettes;
    for (const auto& palette : named_palettes()) {
        Json::Arr colors;
        for (const auto& color : palette.second) {
            colors.push(color);
        }
        Json::Obj row;
        row.kv("id", palette.first);
        row.kv("colors", colors.done());
        palettes.push(row.done());
    }
    Json::Obj root;
    root.kv("service", kService);
    root.kv("version", kVersion);
    root.kv("modes", modes.done());
    root.kv("palettes", palettes.done());
    return root.done().str();
}

inline std::string build_version_json() {
    Json::Arr endpoints;
    for (const char* path : {"/api/health", "/api/version", "/api/presets", "/api/mission",
                             "/api/share", "/api/sky", "/api/orbit", "/api/constellation",
                             "/api/catalog", "/api/metrics", "/api/stream", "/api/echo"}) {
        endpoints.push(std::string(path));
    }
    Json::Obj root;
    root.kv("version", kVersion);
    root.kv("service", kService);
    root.kv("language", kLanguage);
    root.kv("endpoints", endpoints.done());
    return root.done().str();
}

inline std::string build_health_json(long long uptime_seconds, unsigned long long request_count) {
    Json::Obj root;
    root.kv("status", "ok");
    root.kv("service", kService);
    root.kv("language", kLanguage);
    root.kv("version", kVersion);
    root.kv("uptime_seconds", uptime_seconds);
    root.kv("request_count", request_count);
    return root.done().str();
}

}  // namespace aster
