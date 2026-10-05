#include "ironphoenix/uci.hpp"

#include <iostream>

int main() {
    std::cerr
        << "IronPhoenix | Built by love by Nick!\n";

    ironphoenix::UciShell shell;
    return shell.run(std::cin, std::cout);
}
