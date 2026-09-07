#include "json.hpp"
#include "mission.hpp"
#include "util.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool cond, const char* expr, const char* file, int line) {
    if (cond) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::cerr << "FAIL " << file << ":" << line << " " << expr << "\n";
}

}  // namespace

#define CHECK(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

int main() {
    CHECK(aster::url_decode("a+b") == "a b");
    CHECK(aster::url_decode("%2e%2e") == "..");
    CHECK(aster::url_decode("%2Ftmp") == "/tmp");
    CHECK(aster::url_decode("seed%20space") == "seed space");
    CHECK(aster::url_encode("a b") == "a%20b");

    CHECK(aster::json_escape("plain") == "plain");
    CHECK(aster::json_escape("a\"b") == "a\\\"b");
    CHECK(aster::json_escape("line\n") == "line\\n");
    CHECK(aster::json_escape("tab\t") == "tab\\t");

    const auto query = aster::parse_query("seed=sebby&mode=forge&empty=&flag");
    CHECK(query.at("seed") == "sebby");
    CHECK(query.at("mode") == "forge");
    CHECK(query.at("empty") == "");
    CHECK(query.at("flag") == "");
    const auto spaced = aster::parse_query("seed=smoke%20space&mode=bad");
    CHECK(spaced.at("seed") == "smoke space");

    CHECK(aster::stable_seed("alpha") == aster::stable_seed("alpha"));
    CHECK(aster::stable_seed("alpha") != aster::stable_seed("beta"));
    CHECK(aster::fnv1a("aster") == aster::stable_seed("aster"));

    CHECK(aster::clamp_int(5, 1, 3) == 3);
    CHECK(aster::clamp_int(-4, 0, 10) == 0);
    CHECK(aster::clamp_int(7, 1, 10) == 7);
    CHECK(aster::clamp_int(68, 1, 100) == 68);

    const auto whole = aster::parse_byte_range("bytes=0-9", 100);
    CHECK(whole.status == aster::ByteRange::Status::ok);
    CHECK(whole.start == 0);
    CHECK(whole.end == 9);
    const auto open_end = aster::parse_byte_range("bytes=50-", 100);
    CHECK(open_end.status == aster::ByteRange::Status::ok);
    CHECK(open_end.start == 50);
    CHECK(open_end.end == 99);
    const auto suffix = aster::parse_byte_range("bytes=-10", 100);
    CHECK(suffix.status == aster::ByteRange::Status::ok);
    CHECK(suffix.start == 90);
    CHECK(suffix.end == 99);
    const auto unsat = aster::parse_byte_range("bytes=200-300", 100);
    CHECK(unsat.status == aster::ByteRange::Status::unsatisfiable);
    const auto inverted = aster::parse_byte_range("bytes=50-49", 100);
    CHECK(inverted.status == aster::ByteRange::Status::invalid);
    const auto none = aster::parse_byte_range("", 100);
    CHECK(none.status == aster::ByteRange::Status::none);

    const fs::path tmp = fs::temp_directory_path() / "asterforge-contain-test";
    fs::create_directories(tmp / "sub");
    const fs::path inside = tmp / "sub" / "index.html";
    {
        std::ofstream out(inside);
        out << "ok";
    }
    CHECK(aster::path_is_inside(tmp, inside));
    CHECK(aster::path_is_inside(tmp, tmp));
    CHECK(!aster::path_is_inside(tmp, tmp / ".." / "outside.txt"));
    CHECK(aster::path_has_dotdot("/../CMakeLists.txt"));
    CHECK(aster::path_has_dotdot("/foo/../../secret"));
    CHECK(!aster::path_has_dotdot("/styles.css"));
    fs::remove_all(tmp);

    const std::string json = aster::build_mission_json({{"seed", "smoke-seed"}, {"mode", "forge"}});
    CHECK(json.find("\"seed\":\"smoke-seed\"") != std::string::npos);
    CHECK(json.find("\"mode\":\"forge\"") != std::string::npos);
    CHECK(json.find("\"z\":") != std::string::npos);
    CHECK(json.find("AsterForge") != std::string::npos);

    const auto first = aster::generate_mission({{"seed", "alpha"}, {"mode", "orbit"}});
    const auto second = aster::generate_mission({{"seed", "alpha"}, {"mode", "orbit"}});
    CHECK(!first.nodes.empty());
    CHECK(first.nodes.size() == second.nodes.size());
    CHECK(first.nodes[0].x == second.nodes[0].x);
    CHECK(first.nodes[0].y == second.nodes[0].y);
    CHECK(first.nodes[0].z == second.nodes[0].z);
    CHECK(first.nodes[0].z >= 0.0 && first.nodes[0].z <= 1.0);
    CHECK(first.shortId == second.shortId);
    CHECK(first.mode == "orbit");

    const auto bad_mode = aster::generate_mission({{"seed", "x"}, {"mode", "nope"}});
    CHECK(bad_mode.mode == "orbit");

    const std::string presets = aster::build_presets_json();
    CHECK(presets.find("\"orbit\"") != std::string::npos);
    CHECK(presets.find("\"pulse\"") != std::string::npos);
    CHECK(presets.find("\"drift\"") != std::string::npos);

    const auto built = aster::Json::Obj().kv("ok", true).kv("n", 3).done();
    CHECK(built.str().find("\"ok\":true") != std::string::npos);

    CHECK(aster::http_date(0).find("GMT") != std::string::npos);
    CHECK(aster::to_lower("Orbit") == "orbit");

    const std::string share = aster::build_share_json({{"seed", "smoke space"}, {"mode", "bad"}});
    CHECK(share.find("\"mode\":\"orbit\"") != std::string::npos);

    if (g_failed != 0) {
        std::cerr << g_failed << " failed, " << g_passed << " passed\n";
        return 1;
    }
    std::cout << "aster_unit_tests: " << g_passed << " passed\n";
    return 0;
}
