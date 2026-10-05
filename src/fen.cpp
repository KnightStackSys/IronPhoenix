#include "ironphoenix/fen.hpp"

#include <charconv>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace ironphoenix {
    namespace {

        std::string_view trim(std::string_view s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
                s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
                s.remove_suffix(1);
            return s;
        }

        std::vector<std::string_view> splitSimple(std::string_view s, char delim) {
            std::vector<std::string_view> out;
            std::size_t begin = 0;
            while (begin <= s.size()) {
                const std::size_t end = s.find(delim, begin);
                if (end == std::string_view::npos) {
                    out.push_back(s.substr(begin));
                    break;
                }
                out.push_back(s.substr(begin, end - begin));
                begin = end + 1;
            }
            return out;
        }

        std::vector<std::string_view> splitTopLevelFen(std::string_view s) {
            std::vector<std::string_view> out;
            std::size_t begin = 0;
            int braceDepth = 0;
            char quote = 0;
            bool escape = false;

            for (std::size_t i = 0; i < s.size(); ++i) {
                const char ch = s[i];
                if (quote) {
                    if (escape) {
                        escape = false;
                    }
                    else if (ch == '\\') {
                        escape = true;
                    }
                    else if (ch == quote) {
                        quote = 0;
                    }
                    continue;
                }

                if (ch == '\'' || ch == '"') {
                    quote = ch;
                    continue;
                }
                if (ch == '{') {
                    ++braceDepth;
                    continue;
                }
                if (ch == '}') {
                    --braceDepth;
                    continue;
                }
                if (ch == '-' && braceDepth == 0) {
                    out.push_back(s.substr(begin, i - begin));
                    begin = i + 1;
                }
            }
            out.push_back(s.substr(begin));
            return out;
        }

        bool parseInt(std::string_view s, int& value) {
            s = trim(s);
            if (s.empty())
                return false;
            const char* first = s.data();
            const char* last = s.data() + s.size();
            const auto [ptr, ec] = std::from_chars(first, last, value);
            return ec == std::errc{} && ptr == last;
        }

        bool parseFourInts(std::string_view s, std::array<int, 4>& values) {
            const auto parts = splitSimple(s, ',');
            if (parts.size() != 4)
                return false;
            for (unsigned i = 0; i < 4; ++i)
                if (!parseInt(parts[i], values[i]))
                    return false;
            return true;
        }

        Color colorFromFen(char c, bool& ok) {
            ok = true;
            switch (static_cast<char>(std::toupper(static_cast<unsigned char>(c)))) {
            case 'R': return RED;
            case 'B': return BLUE;
            case 'Y': return YELLOW;
            case 'G': return GREEN;
            default: ok = false; return RED;
            }
        }

        char colorToFen(Color c) {
            static constexpr char chars[4] = { 'R', 'B', 'Y', 'G' };
            return chars[static_cast<unsigned>(c)];
        }

        PieceType pieceTypeFromFen(char c) {
            switch (static_cast<char>(std::toupper(static_cast<unsigned char>(c)))) {
            case 'P': return PAWN;
            case 'N': return KNIGHT;
            case 'B': return BISHOP;
            case 'R': return ROOK;
            case 'Q': return QUEEN;
            case 'K': return KING;
            default: return PT_NONE;
            }
        }

        char pieceTypeToFen(PieceType pt) {
            switch (pt) {
            case PAWN: return 'P';
            case KNIGHT: return 'N';
            case BISHOP: return 'B';
            case ROOK: return 'R';
            case QUEEN: return 'Q';
            case KING: return 'K';
            default: return '?';
            }
        }

        char pieceColorToFen(Color c) {
            static constexpr char chars[4] = { 'r', 'b', 'y', 'g' };
            return chars[static_cast<unsigned>(c)];
        }

    }

    std::string squareToString(Square sq) {
        if (sq == SQ_NONE || sq >= SQUARE_NB)
            return "--";
        std::string out;
        out.push_back(static_cast<char>('a' + Geometry::fileOf(sq)));
        out += std::to_string(static_cast<unsigned>(Geometry::rankOf(sq)) + 1u);
        return out;
    }

    bool stringToSquare(std::string_view text, Square& sq, std::size_t& consumed) {
        consumed = 0;
        sq = SQ_NONE;
        if (text.size() < 2)
            return false;

        const char fch = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
        if (fch < 'a' || fch > 'n')
            return false;

        std::size_t i = 1;
        unsigned rank = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) && i <= 2) {
            rank = rank * 10u + static_cast<unsigned>(text[i] - '0');
            ++i;
        }
        if (i == 1 || rank < 1 || rank > 14)
            return false;

        const int file = fch - 'a';
        const int r = static_cast<int>(rank) - 1;
        if (!Geometry::validXY(file, r))
            return false;

        sq = Geometry::square(file, r);
        consumed = i;
        return sq != SQ_NONE;
    }

    bool setFromFen4(Position& pos,
        std::string_view fen,
        Fen4State& state,
        std::string& error) {
        error.clear();
        fen = trim(fen);
        if (fen.empty()) {
            error = "empty FEN4";
            return false;
        }
        if (fen == "4PC") {
            error = "FEN4 alias '4PC' requires an explicitly configured start position";
            return false;
        }

        const auto fields = splitTopLevelFen(fen);
        if (fields.size() != 7 && fields.size() != 8) {
            error = "FEN4 must contain 7 core fields, or 8 fields with one metadata object";
            return false;
        }

        const bool hasMetadata = fields.size() == 8;
        const std::string_view boardField = fields[hasMetadata ? 7 : 6];

        const auto turn = trim(fields[0]);
        bool colorOk = false;
        if (turn.size() != 1) {
            error = "invalid FEN4 turn field";
            return false;
        }
        const Color stm = colorFromFen(turn[0], colorOk);
        if (!colorOk) {
            error = "turn must be R, B, Y, or G";
            return false;
        }

        std::array<int, 4> eliminated{};
        std::array<int, 4> ks{};
        std::array<int, 4> qs{};
        std::array<int, 4> points{};
        if (!parseFourInts(fields[1], eliminated)) {
            error = "eliminated field must contain four comma-separated 0/1 values";
            return false;
        }
        if (!parseFourInts(fields[2], ks) || !parseFourInts(fields[3], qs)) {
            error = "castling fields must each contain four comma-separated 0/1 values";
            return false;
        }
        if (!parseFourInts(fields[4], points)) {
            error = "points field must contain four comma-separated integers";
            return false;
        }
        for (unsigned c = 0; c < 4; ++c) {
            if ((eliminated[c] != 0 && eliminated[c] != 1)
                || (ks[c] != 0 && ks[c] != 1)
                || (qs[c] != 0 && qs[c] != 1)) {
                error = "elimination and castling flags must be 0 or 1";
                return false;
            }
        }

        int halfmove = 0;
        if (!parseInt(fields[5], halfmove) || halfmove < 0) {
            error = "halfmove clock must be a non-negative integer";
            return false;
        }

        const auto ranks = splitSimple(boardField, '/');
        if (ranks.size() != 14) {
            error = "board field must contain exactly 14 ranks";
            return false;
        }

        Position temp = pos; // preserves setup-specific castle lane configuration
        const std::uint8_t ruleset = pos.rulesetId();
        temp.clear();
        temp.setRulesetId(ruleset);

        for (unsigned fenRank = 0; fenRank < 14; ++fenRank) {
            const int rank = 13 - static_cast<int>(fenRank);
            const auto tokens = splitSimple(ranks[fenRank], ',');
            int file = 0;

            for (std::string_view raw : tokens) {
                const std::string_view token = trim(raw);
                if (token.empty()) {
                    error = "empty board token in rank " + std::to_string(rank + 1);
                    return false;
                }

                int run = 0;
                if (parseInt(token, run)) {
                    if (run <= 0 || file + run > 14) {
                        error = "invalid empty run in rank " + std::to_string(rank + 1);
                        return false;
                    }
                    file += run;
                    continue;
                }

                if (token == "x" || token == "X") {
                    if (file >= 14 || Geometry::validXY(file, rank)) {
                        error = "x/X may only appear on an invalid corner square";
                        return false;
                    }
                    ++file;
                    continue;
                }

                if (token.size() != 2 || file >= 14) {
                    error = "invalid piece token in rank " + std::to_string(rank + 1);
                    return false;
                }

                bool pieceColorOk = false;
                const Color c = colorFromFen(token[0], pieceColorOk);
                const PieceType pt = pieceTypeFromFen(token[1]);
                if (!pieceColorOk || pt == PT_NONE) {
                    error = "unsupported piece token '" + std::string(token) + "'";
                    return false;
                }
                if (!Geometry::validXY(file, rank)) {
                    error = "piece placed on invalid corner square";
                    return false;
                }

                temp.placePiece(Geometry::square(file, rank), makePiece(c, pt));
                ++file;
            }

            if (file != 14) {
                error = "rank " + std::to_string(rank + 1) + " expands to "
                    + std::to_string(file) + " cells instead of 14";
                return false;
            }
        }

        std::uint8_t alive = 0;
        std::uint8_t rights = 0;
        for (unsigned c = 0; c < 4; ++c) {
            if (!eliminated[c])
                alive |= static_cast<std::uint8_t>(1u << c);
            if (ks[c])
                rights |= static_cast<std::uint8_t>(1u << (2u * c));
            if (qs[c])
                rights |= static_cast<std::uint8_t>(1u << (2u * c + 1u));
        }

        temp.setAliveMask(alive);
        temp.setCastlingRights(rights);
        temp.setSideToMove(stm);
        for (unsigned c = 0; c < 4; ++c)
            temp.setEnPassant(static_cast<Color>(c), SQ_NONE);
        temp.finalizeSetup();

        if (!temp.verify()) {
            error = "internal position verification failed after FEN4 parse";
            return false;
        }

        Fen4State parsed;
        parsed.points = points;
        parsed.halfmoveClock = static_cast<std::uint32_t>(halfmove);
        if (hasMetadata) {
            const auto md = trim(fields[6]);
            if (md.empty() || md.front() != '{' || md.back() != '}') {
                error = "optional FEN4 metadata field must be a {...} object";
                return false;
            }
            parsed.metadata.assign(md);
        }

        pos = std::move(temp);
        state = std::move(parsed);
        return true;
    }

    std::string toFen4(const Position& pos, const Fen4State& state) {
        std::ostringstream out;
        out << colorToFen(pos.sideToMove()) << '-';

        for (unsigned c = 0; c < 4; ++c) {
            if (c) out << ',';
            out << (pos.isAlive(static_cast<Color>(c)) ? 0 : 1);
        }
        out << '-';

        const std::uint8_t rights = pos.castlingRights();
        for (unsigned c = 0; c < 4; ++c) {
            if (c) out << ',';
            out << ((rights >> (2u * c)) & 1u);
        }
        out << '-';
        for (unsigned c = 0; c < 4; ++c) {
            if (c) out << ',';
            out << ((rights >> (2u * c + 1u)) & 1u);
        }
        out << '-';

        for (unsigned c = 0; c < 4; ++c) {
            if (c) out << ',';
            out << state.points[c];
        }
        out << '-' << state.halfmoveClock << '-';

        if (!state.metadata.empty())
            out << state.metadata << '-';

        for (int rank = 13; rank >= 0; --rank) {
            if (rank != 13)
                out << '/';

            bool firstToken = true;
            int emptyRun = 0;
            auto flushEmpty = [&]() {
                if (!emptyRun)
                    return;
                if (!firstToken) out << ',';
                out << emptyRun;
                firstToken = false;
                emptyRun = 0;
                };

            for (int file = 0; file < 14; ++file) {
                if (!Geometry::validXY(file, rank)) {
                    flushEmpty();
                    if (!firstToken) out << ',';
                    out << 'x';
                    firstToken = false;
                    continue;
                }

                const Square sq = Geometry::square(file, rank);
                const Piece p = pos.pieceAt(sq);
                if (p == NO_PIECE) {
                    ++emptyRun;
                    continue;
                }

                flushEmpty();
                if (!firstToken) out << ',';
                out << pieceColorToFen(colorOf(p)) << pieceTypeToFen(typeOf(p));
                firstToken = false;
            }
            flushEmpty();
        }

        return out.str();
    }

}
