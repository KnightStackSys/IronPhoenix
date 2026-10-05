#include "ironphoenix/movegen.hpp"

#include "ironphoenix/fen.hpp"

#include <algorithm>
#include <cctype>

namespace ironphoenix {
    namespace {

        Bitboard teamOccupancy(const Position& pos, Color c) noexcept {
            return teamOf(c) == 0
                ? (pos.occupancy(RED) | pos.occupancy(YELLOW))
                : (pos.occupancy(BLUE) | pos.occupancy(GREEN));
        }

        Bitboard enemyOccupancy(const Position& pos, Color c) noexcept {
            return teamOf(c) == 0
                ? (pos.occupancy(BLUE) | pos.occupancy(GREEN))
                : (pos.occupancy(RED) | pos.occupancy(YELLOW));
        }

        void pushPromotions(MoveList& out, Square from, Square to, Piece captured, std::uint32_t flags = MF_NONE) {
            out.push(Move(from, to, captured, PROMO_QUEEN, flags));
            out.push(Move(from, to, captured, PROMO_ROOK, flags));
            out.push(Move(from, to, captured, PROMO_BISHOP, flags));
            out.push(Move(from, to, captured, PROMO_KNIGHT, flags));
        }

        void pushPawnMove(Color us, MoveList& out, Square from, Square to, Piece captured = NO_PIECE,
            std::uint32_t flags = MF_NONE) {
            if (Geometry::isPromotionSquare(us, to))
                pushPromotions(out, from, to, captured, flags);
            else
                out.push(Move(from, to, captured, PROMO_NONE, flags));
        }

        bool directionBetween(Square from, Square to, Direction& dir) noexcept {
            const int ff = Geometry::fileOf(from);
            const int fr = Geometry::rankOf(from);
            const int tf = Geometry::fileOf(to);
            const int tr = Geometry::rankOf(to);

            if (ff == tf) {
                dir = tr > fr ? NORTH : SOUTH;
                return tr != fr;
            }
            if (fr == tr) {
                dir = tf > ff ? EAST : WEST;
                return tf != ff;
            }
            return false;
        }

        bool castlePseudoLegal(const Position& pos, Color us, const CastleLane& lane) noexcept {
            if (!lane.valid())
                return false;
            if ((pos.castlingRights() & (1u << lane.rightBit)) == 0)
                return false;
            if (pos.pieceAt(lane.kingFrom) != makePiece(us, KING)
                || pos.pieceAt(lane.rookFrom) != makePiece(us, ROOK))
                return false;

            Direction towardRook{};
            if (!directionBetween(lane.kingFrom, lane.rookFrom, towardRook))
                return false;

            for (Square s = Geometry::step(lane.kingFrom, towardRook);
                s != SQ_NONE && s != lane.rookFrom;
                s = Geometry::step(s, towardRook)) {
                if (pos.pieceAt(s) != NO_PIECE)
                    return false;
            }

            if (lane.kingTo != lane.kingFrom && lane.kingTo != lane.rookFrom
                && pos.pieceAt(lane.kingTo) != NO_PIECE)
                return false;
            if (lane.rookTo != lane.kingFrom && lane.rookTo != lane.rookFrom
                && lane.rookTo != lane.kingTo && pos.pieceAt(lane.rookTo) != NO_PIECE)
                return false;

            if (pos.inCheck(us))
                return false;

            Direction kingDir{};
            if (!directionBetween(lane.kingFrom, lane.kingTo, kingDir))
                return false;

            Bitboard baseOcc = pos.occupancy();
            baseOcc.reset(lane.kingFrom);
            Square s = Geometry::step(lane.kingFrom, kingDir);
            while (s != SQ_NONE) {
                Bitboard occ = baseOcc;
                if (s == lane.kingTo) {
                    occ.reset(lane.rookFrom);
                    occ.set(lane.rookTo);
                }
                occ.set(s);
                if (pos.isAttackedByEnemy(s, us, occ))
                    return false;
                if (s == lane.kingTo)
                    break;
                s = Geometry::step(s, kingDir);
            }
            return s == lane.kingTo;
        }

        char promoChar(Promotion p) noexcept {
            switch (p) {
            case PROMO_QUEEN: return 'q';
            case PROMO_ROOK: return 'r';
            case PROMO_BISHOP: return 'b';
            case PROMO_KNIGHT: return 'n';
            default: return 0;
            }
        }

        std::string lowerCopy(std::string_view s) {
            std::string out(s);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
                });
            return out;
        }

    }

    void generatePseudoLegalMoves(const Position& pos, MoveList& out) {
        out.clear();
        const Color us = pos.sideToMove();
        if (!pos.isAlive(us))
            return;

        const Bitboard all = pos.occupancy();
        const Bitboard friendlyTeam = teamOccupancy(pos, us);
        const Bitboard enemies = enemyOccupancy(pos, us);

        Bitboard pawns = pos.pieces(us, PAWN);
        while (pawns) {
            const Square from = popLsb(pawns);

            const Square one = Geometry::step(from, Geometry::pawnForward(us));
            if (one != SQ_NONE && pos.pieceAt(one) == NO_PIECE) {
                pushPawnMove(us, out, from, one);

                if (Geometry::isPawnHomeSquare(us, from)) {
                    const Square two = Geometry::step(one, Geometry::pawnForward(us));
                    if (two != SQ_NONE && pos.pieceAt(two) == NO_PIECE)
                        out.push(Move(from, two, NO_PIECE, PROMO_NONE, MF_DOUBLE_PUSH));
                }
            }

            Bitboard caps = Geometry::PawnAttacks[us][from] & enemies;
            while (caps) {
                const Square to = popLsb(caps);
                pushPawnMove(us, out, from, to, pos.pieceAt(to));
            }

            for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                const Color owner = static_cast<Color>(ci);
                if (!enemy(us, owner))
                    continue;
                const Square ep = pos.enPassant(owner);
                if (ep == SQ_NONE || !Geometry::PawnAttacks[us][from].test(ep))
                    continue;

                const Square victimSq = Geometry::step(ep, Geometry::pawnForward(owner));
                if (victimSq == SQ_NONE)
                    continue;
                const Piece victim = pos.pieceAt(victimSq);
                if (victim != makePiece(owner, PAWN))
                    continue;

                out.push(makeEnPassantMove(from, ep, victim, owner));
            }
        }

        Bitboard bb = pos.pieces(us, KNIGHT);
        while (bb) {
            const Square from = popLsb(bb);
            Bitboard targets = Geometry::KnightAttacks[from] & ~friendlyTeam;
            while (targets) {
                const Square to = popLsb(targets);
                out.push(Move(from, to, pos.pieceAt(to)));
            }
        }

        bb = pos.pieces(us, BISHOP);
        while (bb) {
            const Square from = popLsb(bb);
            Bitboard targets = Geometry::bishopAttacks(from, all) & ~friendlyTeam;
            while (targets) {
                const Square to = popLsb(targets);
                out.push(Move(from, to, pos.pieceAt(to)));
            }
        }

        bb = pos.pieces(us, ROOK);
        while (bb) {
            const Square from = popLsb(bb);
            Bitboard targets = Geometry::rookAttacks(from, all) & ~friendlyTeam;
            while (targets) {
                const Square to = popLsb(targets);
                out.push(Move(from, to, pos.pieceAt(to)));
            }
        }

        bb = pos.pieces(us, QUEEN);
        while (bb) {
            const Square from = popLsb(bb);
            Bitboard targets = Geometry::queenAttacks(from, all) & ~friendlyTeam;
            while (targets) {
                const Square to = popLsb(targets);
                out.push(Move(from, to, pos.pieceAt(to)));
            }
        }

        bb = pos.pieces(us, KING);
        while (bb) {
            const Square from = popLsb(bb);
            Bitboard targets = Geometry::KingAttacks[from] & ~friendlyTeam;
            while (targets) {
                const Square to = popLsb(targets);
                out.push(Move(from, to, pos.pieceAt(to)));
            }
        }

        for (unsigned laneIndex = 0; laneIndex < 2; ++laneIndex) {
            const CastleLane& lane = pos.castleDefinition(us, laneIndex);
            if (castlePseudoLegal(pos, us, lane))
                out.push(Move(lane.kingFrom, lane.kingTo, NO_PIECE, PROMO_NONE, MF_CASTLE));
        }
    }

    bool isTerminalKingCapture(const Position& pos, Move m) noexcept {
        Piece victim = m.captured();
        if (victim == NO_PIECE) {
            const Color us = pos.sideToMove();
            const Square cap = pos.capturedSquare(m, us);
            if (cap != SQ_NONE)
                victim = pos.pieceAt(cap);
        }
        return victim != NO_PIECE
            && typeOf(victim) == KING
            && enemy(pos.sideToMove(), colorOf(victim));
    }

    void generateLegalMoves(Position& pos, MoveList& out) {
        MoveList pseudo;
        generatePseudoLegalMoves(pos, pseudo);
        out.clear();

        const Color us = pos.sideToMove();
        for (Move m : pseudo) {
            if (isTerminalKingCapture(pos, m)) {
                out.push(m);
                continue;
            }

            StateInfo st;
            pos.makeMove(m, st);
            const bool legal = !pos.inCheck(us);
            pos.undoMove(m, st);
            if (legal)
                out.push(m);
        }
    }

    std::string moveToString(Move m) {
        std::string out = squareToString(m.from()) + squareToString(m.to());
        if (const char p = promoChar(m.promotion()))
            out.push_back(p);
        return out;
    }

    bool parseLegalMove(Position& pos, std::string_view text, Move& move, std::string& error) {
        error.clear();
        const std::string wanted = lowerCopy(text);

        MoveList legal;
        generateLegalMoves(pos, legal);
        for (Move m : legal) {
            if (moveToString(m) == wanted) {
                move = m;
                return true;
            }
        }

        error = "illegal or malformed move '" + std::string(text) + "'";
        return false;
    }

}
