#pragma once

#include "fen.hpp"
#include "search.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

namespace ironphoenix {

    enum class SetupType : std::uint8_t {
        Modern = 0,
        Classic,
        BY,
        BYG,
        RG,
        Custom
    };

    class UciShell {
    public:
        UciShell();
        int run(std::istream& in, std::ostream& out);

    private:
        Position pos_{};
        Fen4State fenState_{};
        std::string startFen_;
        std::string nnueFile_ = "ironphoenix.nnue";
        SetupType setup_ = SetupType::Modern;
        int multiPV_ = 1;
        bool useNNUE_ = false;
        SearchEngine search_{};

        bool handleLine(const std::string& line, std::ostream& out);
        bool handlePosition(std::string_view args, std::ostream& out);
        bool handleSetOption(std::string_view args, std::ostream& out);
        bool handleGo(std::string_view args, std::ostream& out);
        void printBoard(std::ostream& out) const;
        void printHelp(std::ostream& out) const;
    };

}
