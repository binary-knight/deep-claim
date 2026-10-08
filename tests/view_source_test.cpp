// Guards the hidden/observable split: the view implementation must never
// mention the hidden state at all, so a future edit cannot leak deposits.
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

TEST_CASE("view.cpp never references hidden state", "[view]") {
    std::ifstream file(std::string(DC_SOURCE_DIR) + "/sim/src/view.cpp");
    REQUIRE(file.good());
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string source = buffer.str();
    CHECK(source.find("hidden") == std::string::npos);
    CHECK(source.find("Hidden") == std::string::npos);
    CHECK(source.find("DepositTruth") == std::string::npos);
}
