#include "ironphoenix/uci.hpp"

#include "ironphoenix/movegen.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/perft.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <limits>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>

namespace ironphoenix {
    namespace {

        std::string_view trim(std::string_view s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
                s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
                s.remove_suffix(1);
            return s;
        }

        bool startsWith(std::string_view s, std::string_view prefix) {
            return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
        }

        bool iequals(std::string_view a, std::string_view b) noexcept {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i) {
                const auto ac = static_cast<unsigned char>(a[i]);
                const auto bc = static_cast<unsigned char>(b[i]);
                if (std::tolower(ac) != std::tolower(bc))
                    return false;
            }
            return true;
        }

        const char* setupName(SetupType setup) noexcept {
            switch (setup) {
            case SetupType::Modern:  return "Modern";
            case SetupType::Classic: return "Classic";
            case SetupType::BY:      return "BY";
            case SetupType::BYG:     return "BYG";
            case SetupType::RG:      return "RG";
            case SetupType::Custom:  return "Custom";
            }
            return "Modern";
        }

        bool parseSetup(std::string_view text, SetupType& setup) noexcept {
            text = trim(text);
            if (iequals(text, "Modern")) { setup = SetupType::Modern;  return true; }
            if (iequals(text, "Classic")) { setup = SetupType::Classic; return true; }
            if (iequals(text, "BY")) { setup = SetupType::BY;      return true; }
            if (iequals(text, "BYG")) { setup = SetupType::BYG;     return true; }
            if (iequals(text, "RG")) { setup = SetupType::RG;      return true; }
            if (iequals(text, "Custom")) { setup = SetupType::Custom;  return true; }
            return false;
        }

        constexpr std::uint8_t setupRulesetId(SetupType setup) noexcept {
            return static_cast<std::uint8_t>(setup) + 1u;
        }

        constexpr std::string_view MODERN_START_FEN =
            "R-0,0,0,0-1,1,1,1-1,1,1,1-0,0,0,0-0-"
            "x,x,x,yR,yN,yB,yK,yQ,yB,yN,yR,x,x,x/"
            "x,x,x,yP,yP,yP,yP,yP,yP,yP,yP,x,x,x/"
            "x,x,x,8,x,x,x/"
            "bR,bP,10,gP,gR/"
            "bN,bP,10,gP,gN/"
            "bB,bP,10,gP,gB/"
            "bQ,bP,10,gP,gK/"
            "bK,bP,10,gP,gQ/"
            "bB,bP,10,gP,gB/"
            "bN,bP,10,gP,gN/"
            "bR,bP,10,gP,gR/"
            "x,x,x,8,x,x,x/"
            "x,x,x,rP,rP,rP,rP,rP,rP,rP,rP,x,x,x/"
            "x,x,x,rR,rN,rB,rQ,rK,rB,rN,rR,x,x,x";

        bool parseDepth(std::string_view s, int& depth) {
            s = trim(s);
            if (s.empty()) return false;
            try {
                std::size_t used = 0;
                depth = std::stoi(std::string(s), &used);
                return used == s.size() && depth >= 0 && depth <= 64;
            }
            catch (...) {
                return false;
            }
        }

        bool parseNonNegative64(std::string_view s, std::int64_t& value) {
            s = trim(s);
            if (s.empty()) return false;
            try {
                std::size_t used = 0;
                const long long parsed = std::stoll(std::string(s), &used);
                if (used != s.size() || parsed < 0)
                    return false;
                value = static_cast<std::int64_t>(parsed);
                return true;
            }
            catch (...) {
                return false;
            }
        }

        Direction kingSideDirection(Color c) noexcept {
            switch (c) {
            case RED:    return EAST;
            case BLUE:   return SOUTH;
            case YELLOW: return WEST;
            case GREEN:  return NORTH;
            }
            return EAST;
        }

        Square findCastlingRook(const Position& pos, Color c, Square king, Direction dir) noexcept {
            for (Square s = Geometry::step(king, dir); s != SQ_NONE; s = Geometry::step(s, dir)) {
                if (pos.pieceAt(s) == makePiece(c, ROOK))
                    return s;
            }
            return SQ_NONE;
        }

        bool configureCastleLaneFromBoard(Position& pos, Color c, unsigned laneIndex) noexcept {
            const Square king = pos.kingSquare(c);
            if (king == SQ_NONE)
                return false;

            Direction dir = kingSideDirection(c);
            if (laneIndex == 1)
                dir = Geometry::opposite(dir);

            const Square rook = findCastlingRook(pos, c, king, dir);
            const Square rookTo = Geometry::step(king, dir);
            const Square kingTo = rookTo == SQ_NONE ? SQ_NONE : Geometry::step(rookTo, dir);

            CastleLane def{};
            if (rook != SQ_NONE && rookTo != SQ_NONE && kingTo != SQ_NONE && kingTo != rook) {
                def.kingFrom = king;
                def.rookFrom = rook;
                def.kingTo = kingTo;
                def.rookTo = rookTo;
                def.rightBit = static_cast<std::uint8_t>(2u * static_cast<unsigned>(c) + laneIndex);
            }

            pos.configureCastle(c, laneIndex, def);
            return def.valid();
        }

        void clearCastlingGeometry(Position& pos) noexcept {
            for (unsigned c = 0; c < COLOR_NB; ++c)
                for (unsigned lane = 0; lane < 2; ++lane)
                    pos.configureCastle(static_cast<Color>(c), lane, CastleLane{});
        }

        void configureStandard4PCCastling(Position& pos) noexcept {
            for (unsigned c = 0; c < COLOR_NB; ++c) {
                configureCastleLaneFromBoard(pos, static_cast<Color>(c), 0);
                configureCastleLaneFromBoard(pos, static_cast<Color>(c), 1);
            }
        }

        bool missingCastlingGeometry(const Position& pos) noexcept {
            const std::uint8_t rights = pos.castlingRights();
            for (unsigned c = 0; c < COLOR_NB; ++c) {
                for (unsigned lane = 0; lane < 2; ++lane) {
                    const unsigned bit = 2u * c + lane;
                    if ((rights & (1u << bit)) && !pos.castleDefinition(static_cast<Color>(c), lane).valid())
                        return true;
                }
            }
            return false;
        }

        std::string pieceLabel(Piece p) {
            if (p == NO_PIECE) return ".";
            static constexpr char colors[4] = { 'r', 'b', 'y', 'g' };
            static constexpr char types[7] = { '.', 'P', 'N', 'B', 'R', 'Q', 'K' };
            std::string s;
            s.push_back(colors[colorOf(p)]);
            s.push_back(types[typeOf(p)]);
            return s;
        }

        std::size_t findMovesDelimiter(std::string_view s) {
            int braces = 0;
            char quote = 0;
            bool escape = false;
            for (std::size_t i = 0; i + 7 <= s.size(); ++i) {
                const char ch = s[i];
                if (quote) {
                    if (escape) escape = false;
                    else if (ch == '\\') escape = true;
                    else if (ch == quote) quote = 0;
                    continue;
                }
                if (ch == '\'' || ch == '"') { quote = ch; continue; }
                if (ch == '{') { ++braces; continue; }
                if (ch == '}') { --braces; continue; }
                if (braces == 0 && s.substr(i, 7) == " moves ")
                    return i;
            }
            return std::string_view::npos;
        }

    }

    UciShell::UciShell()
        : startFen_(MODERN_START_FEN), setup_(SetupType::Modern) {
        if (NNUE::loaded()) {
            nnueFile_ = NNUE::loadedPath();
            useNNUE_ = true;
        }

        pos_.setRulesetId(setupRulesetId(setup_));

        std::string error;
        if (setFromFen4(pos_, startFen_, fenState_, error))
            configureStandard4PCCastling(pos_);
        else
            pos_.clear();
    }

    void UciShell::printHelp(std::ostream& out) const {
        out <<
            "IronPhoenix commands:\n"
            "  uci                         - UCI identification/options\n"
            "  isready                     - prints readyok\n"
            "  ucinewgame                  - reload current setup start position\n"
            "  setoption name Setup value <Modern|Classic|BY|BYG|RG|Custom>\n"
            "  setoption name StartFEN value <FEN4>\n"
            "  setoption name MultiPV value <1..32>\n"
            "  setoption name EvalFile value <path-to-.nnue>\n"
            "  setoption name UseNNUE value <true|false>\n"
            "  setoption name ReloadNNUE    - reload the current EvalFile\n"
            "  position fen <FEN4> [moves <m1> <m2> ...]\n"
            "  position startpos [moves ...]   (Modern is built in by default)\n"
            "  d                           - display board\n"
            "  fen                         - print normalized FEN4\n"
            "  moves                       - list legal moves\n"
            "  perft <depth>               - count legal leaf nodes\n"
            "  perftdetail <depth>         - nodes/captures/EP/castles/promos/checks\n"
            "  divide <depth>              - per-root-move perft breakdown\n"
            "  go perft <depth>            - UCI-style perft command\n"
            "  go depth <n>                - PVS/negamax to fixed depth\n"
            "  go movetime <ms>            - search for a fixed time\n"
            "  go nodes <n>                - search until node limit\n"
            "  go infinite                 - search until stop (infinate also accepted)\n"
            "  go time <r> <b> <y> <g> increments <r> <b> <y> <g>\n"
            "          delays <r> <b> <y> <g>   - four-player clocks in milliseconds\n"
            "  stop                        - stop the active search\n"
            "  perfttest                   - built-in movegen/FEN smoke suite\n"
            "  key                         - print Zobrist key\n"
            "  quit                        - exit\n";
    }

    void UciShell::printBoard(std::ostream& out) const {
        out << "\n";
        for (int rank = 13; rank >= 0; --rank) {
            out << std::setw(2) << (rank + 1) << "  ";
            for (int file = 0; file < 14; ++file) {
                if (!Geometry::validXY(file, rank)) {
                    out << "## ";
                    continue;
                }
                out << std::setw(2) << pieceLabel(pos_.pieceAt(Geometry::square(file, rank))) << ' ';
            }
            out << '\n';
        }
        out << "    a  b  c  d  e  f  g  h  i  j  k  l  m  n\n";
        out << "side: " << "RBYG"[pos_.sideToMove()]
            << "  check: " << (pos_.inCheck() ? "yes" : "no")
            << "  key: 0x" << std::hex << pos_.key() << std::dec << "\n\n";
    }

    bool UciShell::handleSetOption(std::string_view args, std::ostream& out) {
        search_.stopAndWait();
        args = trim(args);

        constexpr std::string_view setupPrefix = "name Setup value ";
        if (startsWith(args, setupPrefix)) {
            SetupType requested{};
            const std::string_view value = trim(args.substr(setupPrefix.size()));
            if (!parseSetup(value, requested)) {
                out << "info string invalid Setup: expected Modern, Classic, BY, BYG, RG, or Custom\n";
                return false;
            }

            setup_ = requested;
            pos_.setRulesetId(setupRulesetId(setup_));
            clearCastlingGeometry(pos_);

            if (setup_ == SetupType::Modern)
                startFen_ = MODERN_START_FEN;
            else
                startFen_.clear();

            out << "info string Setup set to " << setupName(setup_) << '\n';
            return true;
        }

        constexpr std::string_view fenPrefix = "name StartFEN value ";
        if (startsWith(args, fenPrefix)) {
            const std::string candidate(trim(args.substr(fenPrefix.size())));
            Position test = pos_;
            test.setRulesetId(setupRulesetId(setup_));

            Fen4State testState;
            std::string error;
            if (!setFromFen4(test, candidate, testState, error)) {
                out << "info string invalid StartFEN: " << error << '\n';
                return false;
            }
            startFen_ = candidate;
            out << "info string StartFEN configured for Setup " << setupName(setup_) << '\n';
            return true;
        }

        constexpr std::string_view hashPrefix = "name Hash value ";
        if (startsWith(args, hashPrefix)) {
            std::int64_t mb = 0;
            if (!parseNonNegative64(trim(args.substr(hashPrefix.size())), mb) || mb < 1 || mb > 4096) {
                out << "info string Hash must be between 1 and 4096 MB\n";
                return false;
            }
            if (!search_.setHashSizeMB(static_cast<std::size_t>(mb))) {
                out << "info string unable to allocate Hash\n";
                return false;
            }
            out << "info string Hash set to " << search_.hashSizeMB() << " MB\n";
            return true;
        }

        constexpr std::string_view multiPvPrefix = "name MultiPV value ";
        if (startsWith(args, multiPvPrefix)) {
            std::int64_t value = 0;
            if (!parseNonNegative64(trim(args.substr(multiPvPrefix.size())), value)
                || value < 1 || value > SearchEngine::MAX_MULTI_PV) {
                out << "info string MultiPV must be between 1 and "
                    << SearchEngine::MAX_MULTI_PV << '\n';
                return false;
            }
            multiPV_ = static_cast<int>(value);
            out << "info string MultiPV set to " << multiPV_ << '\n';
            return true;
        }

        constexpr std::string_view evalFilePrefix = "name EvalFile value ";
        if (startsWith(args, evalFilePrefix)) {
            const std::string candidate(trim(args.substr(evalFilePrefix.size())));
            if (candidate.empty()) {
                out << "info string EvalFile requires a PhoenixNet .nnue path\n";
                return false;
            }

            if (!NNUE::loadNetwork(candidate)) {
                out << "info string failed to load PhoenixNet: " << candidate << '\n';
                return false;
            }

            nnueFile_ = candidate;
            useNNUE_ = true;
            search_.clearHash();
            out << "info string PhoenixNet loaded: " << NNUE::loadedPath() << '\n';
            return true;
        }

        constexpr std::string_view useNnuePrefix = "name UseNNUE value ";
        if (startsWith(args, useNnuePrefix)) {
            const std::string_view value = trim(args.substr(useNnuePrefix.size()));

            if (iequals(value, "true") || iequals(value, "on") || value == "1") {
                if (!NNUE::loaded() && !NNUE::loadNetwork(nnueFile_)) {
                    useNNUE_ = false;
                    out << "info string failed to load PhoenixNet: " << nnueFile_ << '\n';
                    return false;
                }

                useNNUE_ = true;
                search_.clearHash();
                out << "info string NNUE enabled: " << NNUE::loadedPath() << '\n';
                return true;
            }

            if (iequals(value, "false") || iequals(value, "off") || value == "0") {
                NNUE::unloadNetwork();
                useNNUE_ = false;
                search_.clearHash();
                out << "info string NNUE disabled; using HCE fallback\n";
                return true;
            }

            out << "info string UseNNUE must be true or false\n";
            return false;
        }

        if (args == "name ReloadNNUE") {
            if (!NNUE::loadNetwork(nnueFile_)) {
                out << "info string failed to reload PhoenixNet: " << nnueFile_ << '\n';
                return false;
            }

            useNNUE_ = true;
            search_.clearHash();
            out << "info string PhoenixNet reloaded: " << NNUE::loadedPath() << '\n';
            return true;
        }

        if (args == "name Clear Hash") {
            search_.clearHash();
            out << "info string Hash cleared\n";
            return true;
        }

        out << "info string unsupported option\n";
        return false;
    }

    bool UciShell::handlePosition(std::string_view args, std::ostream& out) {
        search_.stopAndWait();
        args = trim(args);

        std::string_view positionSpec = args;
        std::string_view moveText;
        const std::size_t movesPos = findMovesDelimiter(args);
        if (movesPos != std::string_view::npos) {
            positionSpec = trim(args.substr(0, movesPos));
            moveText = trim(args.substr(movesPos + 7));
        }

        Position candidate = pos_;
        candidate.setRulesetId(setupRulesetId(setup_));
        clearCastlingGeometry(candidate);
        Fen4State candidateState;
        std::string error;

        if (positionSpec == "startpos") {
            if (startFen_.empty()) {
                out << "info string startpos is not hard-coded because 4PC setup geometry varies; "
                    "set StartFEN or use position fen <FEN4>\n";
                return false;
            }
            if (!setFromFen4(candidate, startFen_, candidateState, error)) {
                out << "info string StartFEN failed: " << error << '\n';
                return false;
            }
        }
        else if (startsWith(positionSpec, "fen ")) {
            std::string_view fen = trim(positionSpec.substr(4));
            if (fen == "4PC") {
                if (startFen_.empty()) {
                    out << "info string FEN alias 4PC requires StartFEN to be configured\n";
                    return false;
                }
                fen = startFen_;
            }
            if (!setFromFen4(candidate, fen, candidateState, error)) {
                out << "info string FEN4 error: " << error << '\n';
                return false;
            }
        }
        else {
            out << "info string expected: position fen <FEN4> [moves ...] or position startpos\n";
            return false;
        }

        configureStandard4PCCastling(candidate);

        if (!moveText.empty()) {
            std::istringstream moves{ std::string(moveText) };
            std::string token;
            while (moves >> token) {
                Move m;
                if (!parseLegalMove(candidate, token, m, error)) {
                    out << "info string move list error: " << error << '\n';
                    return false;
                }

                const Piece moving = candidate.pieceAt(m.from());
                const bool terminal = isTerminalKingCapture(candidate, m);
                StateInfo st;
                candidate.makeMove(m, st);
                if (typeOf(moving) == PAWN || m.isCapture())
                    candidateState.halfmoveClock = 0;
                else
                    ++candidateState.halfmoveClock;

                if (terminal) {
                    std::string extra;
                    if (moves >> extra) {
                        out << "info string moves supplied after terminal king capture\n";
                        return false;
                    }
                    break;
                }
            }
        }

        if (missingCastlingGeometry(candidate))
            out << "info string warning: could not derive castling geometry for one or more enabled rights "
            << "in Setup " << setupName(setup_) << '\n';

        pos_ = std::move(candidate);
        fenState_ = std::move(candidateState);
        return true;
    }

    bool UciShell::handleGo(std::string_view args, std::ostream& out) {
        args = trim(args);

        if (missingCastlingGeometry(pos_)) {
            out << "info string search refused: castling rights exist but CastleLane geometry is not configured\n";
            return false;
        }

        SearchLimits limits;
        std::istringstream input{ std::string(args) };
        std::string token;

        auto readI64 = [&](std::int64_t& value) -> bool {
            std::string text;
            if (!(input >> text))
                return false;
            return parseNonNegative64(text, value);
            };

        auto readClockArray = [&](std::array<std::int64_t, COLOR_NB>& values) -> bool {
            for (unsigned i = 0; i < COLOR_NB; ++i)
                if (!readI64(values[i]))
                    return false;
            return true;
            };

        while (input >> token) {
            if (token == "depth") {
                std::int64_t value = 0;
                if (!readI64(value) || value < 1 || value > 127) {
                    out << "info string go depth must be between 1 and 127\n";
                    return false;
                }
                limits.depth = static_cast<int>(value);
            }
            else if (token == "movetime") {
                if (!readI64(limits.movetimeMs)) {
                    out << "info string invalid go movetime\n";
                    return false;
                }
            }
            else if (token == "nodes") {
                std::int64_t value = 0;
                if (!readI64(value)) {
                    out << "info string invalid go nodes\n";
                    return false;
                }
                limits.nodes = static_cast<std::uint64_t>(value);
            }
            else if (token == "infinite" || token == "infinate") {
                limits.infinite = true;
            }
            else if (token == "time") {
                if (!readClockArray(limits.timeMs)) {
                    out << "info string go time requires: rtime btime ytime gtime\n";
                    return false;
                }
                limits.hasClock = true;
            }
            else if (token == "increments") {
                if (!readClockArray(limits.incrementMs)) {
                    out << "info string increments requires: rinc binc yinc ginc\n";
                    return false;
                }
            }
            else if (token == "delays") {
                if (!readClockArray(limits.delayMs)) {
                    out << "info string delays requires: rdelay bdelay ydelay gdelay\n";
                    return false;
                }
            }
            else {
                out << "info string unknown go token: " << token << '\n';
                return false;
            }
        }

        if (multiPV_ > 1)
            search_.startMultiPV(pos_, limits, multiPV_, out);
        else
            search_.start(pos_, limits, out);
        return true;
    }

    bool UciShell::handleLine(const std::string& rawLine, std::ostream& out) {
        const std::string_view line = trim(rawLine);
        if (line.empty())
            return true;

        if (line == "uci") {
            out << "id name IronPhoenix\n"
                << "id author Nick\n"
                << "option name Setup type combo default Modern var Modern var Classic var BY var BYG var RG var Custom\n"
                << "option name StartFEN type string default <none>\n"
                << "option name Hash type spin default 64 min 1 max 4096\n"
                << "option name MultiPV type spin default 1 min 1 max " << SearchEngine::MAX_MULTI_PV << "\n"
                << "option name EvalFile type string default " << nnueFile_ << "\n"
                << "option name UseNNUE type check default " << (useNNUE_ ? "true" : "false") << "\n"
                << "option name ReloadNNUE type button\n"
                << "option name Clear Hash type button\n"
                << "uciok\n";
        }
        else if (line == "isready") {
            out << "readyok\n";
        }
        else if (line == "help") {
            printHelp(out);
        }
        else if (line == "ucinewgame") {
            search_.newGame();
            pos_.setRulesetId(setupRulesetId(setup_));
            if (!startFen_.empty()) {
                std::string error;
                if (!setFromFen4(pos_, startFen_, fenState_, error))
                    out << "info string StartFEN reload failed: " << error << '\n';
                else
                    configureStandard4PCCastling(pos_);
            }
        }
        else if (startsWith(line, "setoption ")) {
            handleSetOption(line.substr(10), out);
        }
        else if (startsWith(line, "position ")) {
            handlePosition(line.substr(9), out);
        }
        else if (startsWith(line, "go ") && !startsWith(line, "go perft ")) {
            handleGo(line.substr(3), out);
        }
        else if (line == "go") {
            handleGo({}, out);
        }
        else if (line == "d") {
            printBoard(out);
        }
        else if (line == "fen") {
            out << toFen4(pos_, fenState_) << '\n';
        }
        else if (line == "moves") {
            if (missingCastlingGeometry(pos_))
                out << "info string warning: legal move list omits castling until CastleLane geometry is configured\n";
            MoveList legal;
            generateLegalMoves(pos_, legal);
            out << "legal moves (" << legal.size << "):";
            for (Move m : legal)
                out << ' ' << moveToString(m);
            out << '\n';
        }
        else if (line == "key") {
            out << "0x" << std::hex << pos_.key() << std::dec << '\n';
        }
        else if (line == "perfttest") {
            const bool passed = runPerftSelfTests(out);
            (void)passed;
        }
        else if (startsWith(line, "perftdetail ")) {
            int depth = 0;
            if (!parseDepth(line.substr(12), depth)) {
                out << "info string invalid perft depth\n";
            }
            else if (pos_.aliveMask() != 0xF) {
                out << "info string perft currently targets Teams positions with all four players alive\n";
            }
            else if (missingCastlingGeometry(pos_)) {
                out << "info string perft refused: castling rights exist but CastleLane geometry is not configured\n";
            }
            else {
                const auto t0 = std::chrono::steady_clock::now();
                const PerftResult r = perftDetailed(pos_, depth);
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                const std::uint64_t nps = ms > 0 ? r.nodes * 1000ull / static_cast<std::uint64_t>(ms) : 0;
                out << "nodes " << r.nodes
                    << " captures " << r.captures
                    << " ep " << r.enPassants
                    << " castles " << r.castles
                    << " promotions " << r.promotions
                    << " checks " << r.checks
                    << " kingcaptures " << r.kingCaptures
                    << " time " << ms << " nps " << nps << '\n';
            }
        }
        else if (startsWith(line, "perft ") || startsWith(line, "go perft ")) {
            const std::string_view arg = startsWith(line, "go perft ") ? line.substr(9) : line.substr(6);
            int depth = 0;
            if (!parseDepth(arg, depth)) {
                out << "info string invalid perft depth\n";
            }
            else if (pos_.aliveMask() != 0xF) {
                out << "info string perft currently targets Teams positions with all four players alive\n";
            }
            else if (missingCastlingGeometry(pos_)) {
                out << "info string perft refused: castling rights exist but CastleLane geometry is not configured\n";
            }
            else {
                const auto t0 = std::chrono::steady_clock::now();
                const auto nodes = perft(pos_, depth);
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                const std::uint64_t nps = ms > 0 ? nodes * 1000ull / static_cast<std::uint64_t>(ms) : 0;
                out << "info string perft depth " << depth
                    << " nodes " << nodes << " time " << ms << " nps " << nps << '\n';
            }
        }
        else if (startsWith(line, "divide ") || startsWith(line, "perftdiv ")) {
            const std::string_view arg = startsWith(line, "divide ") ? line.substr(7) : line.substr(9);
            int depth = 0;
            if (!parseDepth(arg, depth) || depth < 1) {
                out << "info string divide depth must be >= 1\n";
            }
            else if (pos_.aliveMask() != 0xF) {
                out << "info string perft currently targets Teams positions with all four players alive\n";
            }
            else if (missingCastlingGeometry(pos_)) {
                out << "info string perft refused: castling rights exist but CastleLane geometry is not configured\n";
            }
            else {
                const auto t0 = std::chrono::steady_clock::now();
                const auto nodes = perftDivide(pos_, depth, out);
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                out << "Time: " << ms << " ms";
                if (ms > 0)
                    out << "  NPS: " << nodes * 1000ull / static_cast<std::uint64_t>(ms);
                out << '\n';
            }
        }
        else if (line == "stop") {
            search_.stop();
        }
        else if (line == "quit") {
            search_.stopAndWait();
            return false;
        }
        else {
            out << "info string unknown command: " << line << '\n';
        }

        out.flush();
        return true;
    }

    int UciShell::run(std::istream& in, std::ostream& out) {
        std::string line;
        while (std::getline(in, line))
            if (!handleLine(line, out))
                break;
        return 0;
    }

}
