#include "ironphoenix/perft.hpp"

#include "ironphoenix/fen.hpp"

#include <chrono>
#include <iostream>

namespace ironphoenix {
    namespace {

        std::uint64_t perftImpl(Position& pos, int depth) {
            if (depth <= 0)
                return 1;

            MoveList pseudo;
            generatePseudoLegalMoves(pos, pseudo);
            const Color us = pos.sideToMove();
            std::uint64_t nodes = 0;

            for (Move m : pseudo) {
                if (isTerminalKingCapture(pos, m)) {
                    ++nodes;
                    continue;
                }

                StateInfo st;
                pos.makeMove(m, st);
                if (!pos.inCheck(us))
                    nodes += depth == 1 ? 1 : perftImpl(pos, depth - 1);
                pos.undoMove(m, st);
            }
            return nodes;
        }

        PerftResult detailedImpl(Position& pos, int depth) {
            PerftResult total{};
            if (depth <= 0) {
                total.nodes = 1;
                return total;
            }

            MoveList pseudo;
            generatePseudoLegalMoves(pos, pseudo);
            const Color us = pos.sideToMove();

            for (Move m : pseudo) {
                const bool kingCap = isTerminalKingCapture(pos, m);
                if (kingCap) {
                    ++total.nodes;
                    ++total.captures;
                    ++total.kingCaptures;
                    continue;
                }

                const bool gives = pos.givesCheck(m);
                StateInfo st;
                pos.makeMove(m, st);
                if (!pos.inCheck(us)) {
                    if (depth == 1) {
                        ++total.nodes;
                        total.captures += m.isCapture();
                        total.enPassants += m.isEnPassant();
                        total.castles += m.isCastle();
                        total.promotions += m.isPromotion();
                        total.checks += gives;
                    }
                    else {
                        const PerftResult child = detailedImpl(pos, depth - 1);
                        total.nodes += child.nodes;
                        total.captures += child.captures;
                        total.enPassants += child.enPassants;
                        total.castles += child.castles;
                        total.promotions += child.promotions;
                        total.checks += child.checks;
                        total.kingCaptures += child.kingCaptures;
                    }
                }
                pos.undoMove(m, st);
            }
            return total;
        }

        Position fourKingsPosition() {
            Position p;
            p.clear();
            p.placePiece(Geometry::square(6, 0), makePiece(RED, KING));
            p.placePiece(Geometry::square(0, 6), makePiece(BLUE, KING));
            p.placePiece(Geometry::square(6, 13), makePiece(YELLOW, KING));
            p.placePiece(Geometry::square(13, 6), makePiece(GREEN, KING));
            p.setSideToMove(RED);
            p.finalizeSetup();
            return p;
        }

    }

    std::uint64_t perft(Position& pos, int depth) {
        if (depth < 0)
            return 0;
        return perftImpl(pos, depth);
    }

    PerftResult perftDetailed(Position& pos, int depth) {
        return detailedImpl(pos, depth);
    }

    std::uint64_t perftDivide(Position& pos, int depth, std::ostream& out) {
        if (depth <= 0) {
            out << "nodes 1\n";
            return 1;
        }

        MoveList pseudo;
        generatePseudoLegalMoves(pos, pseudo);
        const Color us = pos.sideToMove();
        std::uint64_t total = 0;

        for (Move m : pseudo) {
            std::uint64_t count = 0;
            if (isTerminalKingCapture(pos, m)) {
                count = 1;
            }
            else {
                StateInfo st;
                pos.makeMove(m, st);
                if (!pos.inCheck(us))
                    count = depth == 1 ? 1 : perftImpl(pos, depth - 1);
                pos.undoMove(m, st);
            }

            if (count) {
                out << moveToString(m) << ": " << count << '\n';
                total += count;
            }
        }

        out << "Total: " << total << '\n';
        return total;
    }

    bool runPerftSelfTests(std::ostream& out) {
        bool ok = true;

        {
            Position p = fourKingsPosition();
            constexpr std::uint64_t expected[] = { 1, 5, 25, 125, 625 };
            for (int d = 0; d <= 4; ++d) {
                const auto got = perft(p, d);
                const bool pass = got == expected[d];
                out << "four-kings depth " << d << ": " << got
                    << " expected " << expected[d] << (pass ? " PASS" : " FAIL") << '\n';
                ok &= pass;
            }
        }

        {
            Position p = fourKingsPosition();
            p.placePiece(Geometry::square(6, 9), makePiece(RED, PAWN));
            p.finalizeSetup();
            const auto got = perft(p, 1);
            const bool pass = got == 9;
            out << "promotion-root depth 1: " << got << " expected 9"
                << (pass ? " PASS" : " FAIL") << '\n';
            ok &= pass;
        }

        {
            Position p = fourKingsPosition();
            p.placePiece(Geometry::square(1, 6), makePiece(BLUE, PAWN));
            p.placePiece(Geometry::square(1, 5), makePiece(RED, PAWN));
            p.setSideToMove(BLUE);
            p.finalizeSetup();

            StateInfo blueState;
            const Move bluePush(Geometry::square(1, 6), Geometry::square(3, 6),
                NO_PIECE, PROMO_NONE, MF_DOUBLE_PUSH);
            p.makeMove(bluePush, blueState);
            p.setSideToMove(RED);
            p.finalizeSetup();

            Move ep;
            std::string error;
            const bool found = parseLegalMove(p, "b6c7", ep, error) && ep.isEnPassant()
                && ep.hasEpOwner() && ep.epOwner() == BLUE
                && p.capturedSquare(ep, RED) == Geometry::square(3, 6);
            bool pass = found;
            if (found) {
                StateInfo st;
                p.makeMove(ep, st);
                pass = p.pieceAt(Geometry::square(3, 6)) == NO_PIECE
                    && p.pieceAt(Geometry::square(2, 6)) == makePiece(RED, PAWN)
                    && p.verify();
                p.undoMove(ep, st);
            }
            out << "perpendicular EP: " << (pass ? "PASS" : "FAIL");
            if (!found && !error.empty()) out << " (" << error << ')';
            out << '\n';
            ok &= pass;
        }

        {
            Position p = fourKingsPosition();
            Fen4State state;
            state.points = { 1, 2, 3, 4 };
            state.halfmoveClock = 17;
            const std::string fen = toFen4(p, state);
            Position q;
            Fen4State parsed;
            std::string error;
            const bool parsedOk = setFromFen4(q, fen, parsed, error);
            const bool pass = parsedOk && q.key() == p.key()
                && parsed.points == state.points
                && parsed.halfmoveClock == state.halfmoveClock;
            out << "FEN4 round-trip: " << (pass ? "PASS" : "FAIL");
            if (!parsedOk) out << " (" << error << ')';
            out << '\n';
            ok &= pass;
        }

        out << (ok ? "perfttest: PASS\n" : "perfttest: FAIL\n");
        return ok;
    }

}
