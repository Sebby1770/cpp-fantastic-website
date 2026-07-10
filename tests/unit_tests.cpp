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

}  // namespace

int main() {
    using aster::json_escape;
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

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All unit tests passed\n";
    return 0;
}
