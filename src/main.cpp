#include "ironphoenix/nnue.hpp"
#include "ironphoenix/uci.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main() {
    std::cerr << "IronPhoenix | Built by love by Nick!\n";

    const char* envPath = std::getenv("IRONPHOENIX_NNUE");
    const std::string networkPath = envPath && *envPath
        ? std::string(envPath)
        : std::string("ironphoenix.nnue");

    if (ironphoenix::NNUE::loadNetwork(networkPath))
        std::cerr << "PhoenixNet loaded: " << networkPath << '\n';
    else
        std::cerr << "PhoenixNet not loaded; using HCE fallback\n";

    ironphoenix::UciShell shell;
    return shell.run(std::cin, std::cout);
}
