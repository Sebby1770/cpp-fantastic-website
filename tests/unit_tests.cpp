#include "util.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect_eq(const std::string& name, const std::string& got, const std::string& want) {
    if (got != want) {
        std::cerr << "FAIL " << name << ": got \"" << got << "\" want \"" << want << "\"\n";
        ++failures;
    } else {
        std::cout << "ok   " << name << "\n";
    }
}

void expect_true(const std::string& name, bool cond) {
    if (!cond) {
        std::cerr << "FAIL " << name << "\n";
        ++failures;
    } else {
        std::cout << "ok   " << name << "\n";
    }
}

}  // namespace

int main() {
    using aster::current_time_iso;
    using aster::int_param;
    using aster::json_escape;
    using aster::parse_query;
    using aster::stable_seed;
    using aster::url_decode;

    expect_eq("url plain", url_decode("hello"), "hello");
    expect_eq("url plus", url_decode("a+b"), "a b");
    expect_eq("url percent", url_decode("seed%2Dtest"), "seed-test");
    expect_eq("url space hex", url_decode("hi%20there"), "hi there");
    expect_eq("url mixed", url_decode("a%2Fb+c"), "a/b c");

    expect_eq("json plain", json_escape("hello"), "hello");
    expect_eq("json quote", json_escape("say \"hi\""), "say \\\"hi\\\"");
    expect_eq("json slash", json_escape("a\\b"), "a\\\\b");
    expect_eq("json newline", json_escape("a\nb"), "a\\nb");
    expect_eq("json tab", json_escape("a\tb"), "a\\tb");

    const auto q = parse_query("seed=alpha&min=1&max=10");
    expect_eq("query seed", q.at("seed"), "alpha");
    expect_eq("query min", q.at("min"), "1");
    expect_true("int clamp high", int_param(q, "max", 0, 0, 5) == 5);
    expect_true("int fallback", int_param(q, "missing", 7, 0, 100) == 7);
    expect_true("stable seed deterministic",
                stable_seed("aster") == stable_seed("aster"));
    expect_true("stable seed differs",
                stable_seed("a") != stable_seed("b"));
    const std::string iso = current_time_iso();
    expect_true("iso ends with Z", !iso.empty() && iso.back() == 'Z');
    expect_true("iso has T", iso.find('T') != std::string::npos);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all unit tests passed\n";
    return 0;
}
