#include "ironphoenix/position.hpp"

#include <cassert>

namespace ironphoenix {

    Position::Position() {
        Geometry::init();
        Zobrist::init();
        clear();
    }

    void Position::clear() {
        board_.fill(NO_PIECE);
        for (auto& byColor : pieceBB_)
            for (auto& bb : byColor)
                bb.clear();
        for (auto& bb : colorOcc_)
            bb.clear();
        allOcc_.clear();
        kingSq_.fill(SQ_NONE);
        enPassant_.fill(SQ_NONE);
        sideToMove_ = RED;
        castlingRights_ = 0;
        aliveMask_ = 0xF;
        rulesetId_ = 0;
        checkers_.clear();
        key_ = recomputeKey();
    }

    void Position::rawPut(Square sq, Piece p) noexcept {
        assert(sq < SQUARE_NB && p != NO_PIECE && board_[sq] == NO_PIECE);
        board_[sq] = p;
        const Color c = colorOf(p);
        const PieceType pt = typeOf(p);
        pieceBB_[c][pt].set(sq);
        colorOcc_[c].set(sq);
        allOcc_.set(sq);
        if (pt == KING)
            kingSq_[c] = sq;
        key_ ^= Zobrist::PieceSquare[p][sq];
    }

    Piece Position::rawRemove(Square sq) noexcept {
        assert(sq < SQUARE_NB);
        const Piece p = board_[sq];
        if (p == NO_PIECE)
            return NO_PIECE;

        const Color c = colorOf(p);
        const PieceType pt = typeOf(p);
        board_[sq] = NO_PIECE;
        pieceBB_[c][pt].reset(sq);
        colorOcc_[c].reset(sq);
        allOcc_.reset(sq);
        if (pt == KING && kingSq_[c] == sq)
            kingSq_[c] = SQ_NONE;
        key_ ^= Zobrist::PieceSquare[p][sq];
        return p;
    }

    void Position::placePiece(Square sq, Piece p) {
        assert(sq < SQUARE_NB);
        if (board_[sq] != NO_PIECE)
            rawRemove(sq);
        if (p != NO_PIECE)
            rawPut(sq, p);
    }

    void Position::erasePiece(Square sq) {
        rawRemove(sq);
    }

    void Position::setSideToMove(Color c) {
        if (sideToMove_ == c)
            return;
        key_ ^= Zobrist::Side[sideToMove_] ^ Zobrist::Side[c];
        sideToMove_ = c;
        refreshCheckers();
    }

    void Position::setCastlingRights(std::uint8_t rights) {
        const std::uint8_t changed = static_cast<std::uint8_t>(castlingRights_ ^ rights);
        for (unsigned bit = 0; bit < 8; ++bit)
            if (changed & (1u << bit))
                key_ ^= Zobrist::CastleRight[bit];
        castlingRights_ = rights;
    }

    void Position::setEnPassant(Color owner, Square sq) {
        Square& current = enPassant_[owner];
        if (current == sq)
            return;
        if (current != SQ_NONE)
            key_ ^= Zobrist::EnPassant[owner][current];
        current = sq;
        if (current != SQ_NONE)
            key_ ^= Zobrist::EnPassant[owner][current];
    }

    void Position::setAliveMask(std::uint8_t mask) {
        mask &= 0xF;
        const std::uint8_t changed = static_cast<std::uint8_t>(aliveMask_ ^ mask);
        for (unsigned c = 0; c < COLOR_NB; ++c)
            if (changed & (1u << c))
                key_ ^= Zobrist::Alive[c];
        aliveMask_ = mask;
    }

    void Position::setRulesetId(std::uint8_t id) {
        id &= 0xF;
        if (rulesetId_ == id)
            return;
        key_ ^= Zobrist::Ruleset[rulesetId_] ^ Zobrist::Ruleset[id];
        rulesetId_ = id;
    }

    void Position::configureCastle(Color c, unsigned lane, CastleLane def) {
        assert(lane < 2);
        if (def.rightBit == 0xFF)
            def.rightBit = static_cast<std::uint8_t>(static_cast<unsigned>(c) * 2u + lane);
        castle_[c][lane] = def;
    }

    void Position::finalizeSetup() {
        key_ = recomputeKey();
        refreshCheckers();
        assert(verify());
    }

    Bitboard Position::attackersTo(Square target, Color by, Bitboard occ) const noexcept {
        if (target == SQ_NONE)
            return {};

        const unsigned c = static_cast<unsigned>(by);
        Bitboard out{};
        out |= Geometry::PawnAttackers[c][target] & pieceBB_[c][PAWN] & occ;
        out |= Geometry::KnightAttacks[target] & pieceBB_[c][KNIGHT] & occ;
        out |= Geometry::KingAttacks[target] & pieceBB_[c][KING] & occ;

        const Bitboard rookers = (pieceBB_[c][ROOK] | pieceBB_[c][QUEEN]) & occ;
        const Bitboard bishops = (pieceBB_[c][BISHOP] | pieceBB_[c][QUEEN]) & occ;
        out |= Geometry::rookAttacks(target, occ) & rookers;
        out |= Geometry::bishopAttacks(target, occ) & bishops;
        return out;
    }

    Bitboard Position::attackersToEnemy(Square target, Color victim, Bitboard occ) const noexcept {
        Bitboard out{};
        if (teamOf(victim) == 0) {
            out |= attackersTo(target, BLUE, occ);
            out |= attackersTo(target, GREEN, occ);
        }
        else {
            out |= attackersTo(target, RED, occ);
            out |= attackersTo(target, YELLOW, occ);
        }
        return out;
    }

    bool Position::isAttackedBy(Square target, Color by, Bitboard occ) const noexcept {
        if (target == SQ_NONE)
            return false;

        const unsigned c = static_cast<unsigned>(by);
        if (Geometry::PawnAttackers[c][target] & pieceBB_[c][PAWN] & occ) return true;
        if (Geometry::KnightAttacks[target] & pieceBB_[c][KNIGHT] & occ) return true;
        if (Geometry::KingAttacks[target] & pieceBB_[c][KING] & occ) return true;

        const Bitboard rookers = (pieceBB_[c][ROOK] | pieceBB_[c][QUEEN]) & occ;
        if (Geometry::rookAttacks(target, occ) & rookers) return true;

        const Bitboard bishops = (pieceBB_[c][BISHOP] | pieceBB_[c][QUEEN]) & occ;
        return static_cast<bool>(Geometry::bishopAttacks(target, occ) & bishops);
    }

    bool Position::isAttackedByEnemy(Square target, Color victim, Bitboard occ) const noexcept {
        if (teamOf(victim) == 0)
            return isAttackedBy(target, BLUE, occ) || isAttackedBy(target, GREEN, occ);
        return isAttackedBy(target, RED, occ) || isAttackedBy(target, YELLOW, occ);
    }

    bool Position::inCheck(Color c) const noexcept {
        const Square k = kingSq_[c];
        return k != SQ_NONE && isAttackedByEnemy(k, c, allOcc_);
    }

    void Position::refreshCheckers() noexcept {
        const Square k = kingSq_[sideToMove_];
        checkers_ = k == SQ_NONE ? Bitboard{} : attackersToEnemy(k, sideToMove_, allOcc_);
    }

    Square Position::capturedSquare(Move m, Color mover) const noexcept {
        if (!m.isEnPassant())
            return m.to();

        if (m.hasEpOwner()) {
            const Color owner = m.epOwner();
            if (enemy(mover, owner) && enPassant_[owner] == m.to())
                return Geometry::step(m.to(), Geometry::pawnForward(owner));
        }

        for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
            const Color owner = static_cast<Color>(ci);
            if (enemy(mover, owner) && enPassant_[owner] == m.to())
                return Geometry::step(m.to(), Geometry::pawnForward(owner));
        }
        return SQ_NONE;
    }

    const CastleLane* Position::castleLane(Color c, Move m) const noexcept {
        if (!m.isCastle())
            return nullptr;
        for (const auto& lane : castle_[c])
            if (lane.valid() && lane.kingFrom == m.from() && lane.kingTo == m.to())
                return &lane;
        return nullptr;
    }

    bool Position::givesCheck(Move m) const noexcept {
        const Square from = m.from();
        const Square to = m.to();
        if (from >= SQUARE_NB || to >= SQUARE_NB)
            return false;

        const Piece moving = board_[from];
        if (moving == NO_PIECE)
            return false;
        const Color us = colorOf(moving);
        const PieceType oldType = typeOf(moving);
        const PieceType newType = m.isPromotion() ? promotedType(m.promotion()) : oldType;

        const Square capSq = capturedSquare(m, us);
        const Piece captured = capSq != SQ_NONE ? board_[capSq] : NO_PIECE;
        if (captured != NO_PIECE && typeOf(captured) == KING && enemy(us, colorOf(captured)))
            return true;

        Bitboard occ = allOcc_;
        occ.reset(from);
        if (captured != NO_PIECE && capSq != SQ_NONE)
            occ.reset(capSq);
        occ.set(to);

        Bitboard bishops = pieceBB_[us][BISHOP];
        Bitboard rooks = pieceBB_[us][ROOK];
        Bitboard queens = pieceBB_[us][QUEEN];

        if (oldType == BISHOP) bishops.reset(from);
        if (oldType == ROOK)   rooks.reset(from);
        if (oldType == QUEEN)  queens.reset(from);
        if (newType == BISHOP) bishops.set(to);
        if (newType == ROOK)   rooks.set(to);
        if (newType == QUEEN)  queens.set(to);

        if (m.isCastle()) {
            if (const CastleLane* lane = castleLane(us, m)) {
                occ.reset(lane->rookFrom);
                occ.set(lane->rookTo);
                rooks.reset(lane->rookFrom);
                rooks.set(lane->rookTo);
            }
        }

        bishops &= occ;
        rooks &= occ;
        queens &= occ;

        auto attacksKing = [&](Square k) noexcept -> bool {
            if (k == SQ_NONE)
                return false;

            if (Geometry::rookAttacks(k, occ) & (rooks | queens))
                return true;
            if (Geometry::bishopAttacks(k, occ) & (bishops | queens))
                return true;

            switch (newType) {
            case PAWN:   return Geometry::PawnAttacks[us][to].test(k);
            case KNIGHT: return Geometry::KnightAttacks[to].test(k);
            case KING:   return Geometry::KingAttacks[to].test(k);
            default:     return false;
            }
            };

        if (teamOf(us) == 0)
            return attacksKing(kingSq_[BLUE]) || attacksKing(kingSq_[GREEN]);
        return attacksKing(kingSq_[RED]) || attacksKing(kingSq_[YELLOW]);
    }

    void Position::updateCastlingRightsForMove(Piece moving,
        Square from,
        Piece captured,
        Square capturedSq) noexcept {
        std::uint8_t rights = castlingRights_;
        if (moving != NO_PIECE) {
            const Color c = colorOf(moving);
            const PieceType pt = typeOf(moving);
            if (pt == KING) {
                rights &= static_cast<std::uint8_t>(~(3u << (2u * static_cast<unsigned>(c))));
            }
            else if (pt == ROOK) {
                for (const auto& lane : castle_[c])
                    if (lane.valid() && from == lane.rookFrom)
                        rights &= static_cast<std::uint8_t>(~(1u << lane.rightBit));
            }
        }

        if (captured != NO_PIECE && typeOf(captured) == ROOK) {
            const Color c = colorOf(captured);
            for (const auto& lane : castle_[c])
                if (lane.valid() && capturedSq == lane.rookFrom)
                    rights &= static_cast<std::uint8_t>(~(1u << lane.rightBit));
        }

        setCastlingRights(rights);
    }

    void Position::makeMove(Move m, StateInfo& st) {
        const Square from = m.from();
        const Square to = m.to();
        assert(from < SQUARE_NB && to < SQUARE_NB);

        const Piece moving = board_[from];
        assert(moving != NO_PIECE);
        const Color us = colorOf(moving);
        assert(us == sideToMove_);

        st.key = key_;
        st.checkers = checkers_;
        st.enPassant = enPassant_;
        st.kingSq = kingSq_;
        st.castlingRights = castlingRights_;
        st.aliveMask = aliveMask_;
        st.side = static_cast<std::uint8_t>(sideToMove_);
        st.moved = moving;
        st.capturedSq = capturedSquare(m, us);
        st.captured = st.capturedSq != SQ_NONE ? board_[st.capturedSq] : NO_PIECE;

#ifndef NDEBUG
        if (m.captured() != NO_PIECE)
            assert(m.captured() == st.captured);
#endif

        setEnPassant(us, SQ_NONE);

        updateCastlingRightsForMove(moving, from, st.captured, st.capturedSq);

        if (st.captured != NO_PIECE) {
            const Color victim = colorOf(st.captured);
            const bool capturedKing = typeOf(st.captured) == KING;

            if (typeOf(st.captured) == PAWN && enPassant_[victim] != SQ_NONE) {
                const Square doublePushPawnSq = Geometry::step(enPassant_[victim], Geometry::pawnForward(victim));
                if (doublePushPawnSq == st.capturedSq)
                    setEnPassant(victim, SQ_NONE);
            }

            rawRemove(st.capturedSq);
            if (capturedKing) {
                setAliveMask(static_cast<std::uint8_t>(aliveMask_ & ~(1u << static_cast<unsigned>(victim))));
                const std::uint8_t victimRights = static_cast<std::uint8_t>(3u << (2u * static_cast<unsigned>(victim)));
                setCastlingRights(static_cast<std::uint8_t>(castlingRights_ & ~victimRights));
            }
        }

        rawRemove(from);

        if (m.isCastle()) {
            const CastleLane* lane = castleLane(us, m);
            assert(lane && lane->valid());
            const Piece rook = rawRemove(lane->rookFrom);
            assert(rook == makePiece(us, ROOK));
            rawPut(lane->rookTo, rook);
        }

        Piece placed = moving;
        if (m.isPromotion()) {
            assert(typeOf(moving) == PAWN);
            const PieceType promo = promotedType(m.promotion());
            assert(promo == KNIGHT || promo == BISHOP || promo == ROOK || promo == QUEEN);
            placed = makePiece(us, promo);
        }
        rawPut(to, placed);

        if (m.isDoublePush()) {
            assert(typeOf(moving) == PAWN);
            const Square ep = Geometry::step(from, Geometry::pawnForward(us));
            assert(ep != SQ_NONE);
            setEnPassant(us, ep);
        }

        const Color them = nextColor(us);
        key_ ^= Zobrist::Side[us] ^ Zobrist::Side[them];
        sideToMove_ = them;
        refreshCheckers();
    }

    void Position::undoMove(Move m, const StateInfo& st) {
        const Color us = static_cast<Color>(st.side);
        const Square from = m.from();
        const Square to = m.to();
        \
        rawRemove(to);

        if (m.isCastle()) {
            const CastleLane* lane = castleLane(us, m);
            assert(lane && lane->valid());
            const Piece rook = rawRemove(lane->rookTo);
            assert(rook == makePiece(us, ROOK));
            rawPut(lane->rookFrom, rook);
        }

        rawPut(from, st.moved);
        if (st.captured != NO_PIECE)
            rawPut(st.capturedSq, st.captured);

        enPassant_ = st.enPassant;
        kingSq_ = st.kingSq;
        castlingRights_ = st.castlingRights;
        aliveMask_ = st.aliveMask;
        sideToMove_ = us;
        checkers_ = st.checkers;
        key_ = st.key;

        assert(verify());
    }

    Key Position::recomputeKey() const noexcept {
        Key k = Zobrist::Side[sideToMove_] ^ Zobrist::Ruleset[rulesetId_];
        for (unsigned sq = 0; sq < SQUARE_NB; ++sq) {
            const Piece p = board_[sq];
            if (p != NO_PIECE)
                k ^= Zobrist::PieceSquare[p][sq];
        }

        for (unsigned c = 0; c < COLOR_NB; ++c) {
            if (enPassant_[c] != SQ_NONE)
                k ^= Zobrist::EnPassant[c][enPassant_[c]];
            if (aliveMask_ & (1u << c))
                k ^= Zobrist::Alive[c];
        }

        for (unsigned bit = 0; bit < 8; ++bit)
            if (castlingRights_ & (1u << bit))
                k ^= Zobrist::CastleRight[bit];

        return k;
    }

    bool Position::verify() const noexcept {
        std::array<std::array<Bitboard, PIECE_TYPE_NB>, COLOR_NB> rebuiltPieces{};
        std::array<Bitboard, COLOR_NB> rebuiltColor{};
        Bitboard rebuiltAll{};
        std::array<Square, COLOR_NB> rebuiltKing{ SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE };

        for (unsigned s = 0; s < SQUARE_NB; ++s) {
            const Piece p = board_[s];
            if (p == NO_PIECE)
                continue;
            const Color c = colorOf(p);
            const PieceType pt = typeOf(p);
            if (pt < PAWN || pt > KING)
                return false;
            rebuiltPieces[c][pt].set(static_cast<Square>(s));
            rebuiltColor[c].set(static_cast<Square>(s));
            rebuiltAll.set(static_cast<Square>(s));
            if (pt == KING) {
                if (rebuiltKing[c] != SQ_NONE)
                    return false;
                rebuiltKing[c] = static_cast<Square>(s);
            }
        }

        if (rebuiltAll != allOcc_ || rebuiltKing != kingSq_)
            return false;
        for (unsigned c = 0; c < COLOR_NB; ++c) {
            if (rebuiltColor[c] != colorOcc_[c])
                return false;
            for (unsigned pt = 0; pt < PIECE_TYPE_NB; ++pt)
                if (rebuiltPieces[c][pt] != pieceBB_[c][pt])
                    return false;
        }
        return recomputeKey() == key_;
    }

}
