#pragma once

#include "geometry.hpp"
#include "move.hpp"
#include "zobrist.hpp"

#include <array>
#include <cstdint>

namespace ironphoenix {

struct CastleLane {
    Square kingFrom = SQ_NONE;
    Square rookFrom = SQ_NONE;
    Square kingTo   = SQ_NONE;
    Square rookTo   = SQ_NONE;
    std::uint8_t rightBit = 0xFF;

    IRONPHOENIX_FORCE_INLINE constexpr bool valid() const noexcept {
        return kingFrom != SQ_NONE && rookFrom != SQ_NONE
            && kingTo != SQ_NONE && rookTo != SQ_NONE && rightBit < 8;
    }
};

struct StateInfo {
    Key key = 0;
    Bitboard checkers{};
    std::array<Square, COLOR_NB> enPassant{SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE};
    std::array<Square, COLOR_NB> kingSq{SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE};
    std::uint8_t castlingRights = 0;
    std::uint8_t aliveMask = 0xF;
    std::uint8_t side = RED;
    Piece moved = NO_PIECE;
    Piece captured = NO_PIECE;
    Square capturedSq = SQ_NONE;
};

class Position {
public:
    Position();

    void clear();

    void placePiece(Square sq, Piece p);
    void erasePiece(Square sq);
    void setSideToMove(Color c);
    void setCastlingRights(std::uint8_t rights);
    void setEnPassant(Color owner, Square sq);
    void setAliveMask(std::uint8_t mask);
    void setRulesetId(std::uint8_t id);
    void finalizeSetup();

    void configureCastle(Color c, unsigned lane, CastleLane def);

    [[nodiscard]] IRONPHOENIX_FORCE_INLINE Piece pieceAt(Square sq) const noexcept { return board_[sq]; }
    [[nodiscard]] IRONPHOENIX_FORCE_INLINE Bitboard pieces(Color c, PieceType pt) const noexcept {
        return pieceBB_[static_cast<unsigned>(c)][static_cast<unsigned>(pt)];
    }
    [[nodiscard]] IRONPHOENIX_FORCE_INLINE Bitboard occupancy(Color c) const noexcept {
        return colorOcc_[static_cast<unsigned>(c)];
    }
    [[nodiscard]] IRONPHOENIX_FORCE_INLINE Bitboard occupancy() const noexcept { return allOcc_; }
    [[nodiscard]] IRONPHOENIX_FORCE_INLINE Square kingSquare(Color c) const noexcept { return kingSq_[c]; }
    [[nodiscard]] IRONPHOENIX_FORCE_INLINE Color sideToMove() const noexcept { return sideToMove_; }
    [[nodiscard]] IRONPHOENIX_FORCE_INLINE Key key() const noexcept { return key_; }
    [[nodiscard]] IRONPHOENIX_FORCE_INLINE std::uint8_t castlingRights() const noexcept { return castlingRights_; }
    [[nodiscard]] IRONPHOENIX_FORCE_INLINE Square enPassant(Color c) const noexcept { return enPassant_[c]; }
    [[nodiscard]] IRONPHOENIX_FORCE_INLINE std::uint8_t aliveMask() const noexcept { return aliveMask_; }
    [[nodiscard]] IRONPHOENIX_FORCE_INLINE bool isAlive(Color c) const noexcept {
        return (aliveMask_ & (1u << static_cast<unsigned>(c))) != 0;
    }

    [[nodiscard]] Bitboard attackersTo(Square target, Color by, Bitboard occ) const noexcept;
    [[nodiscard]] Bitboard attackersToEnemy(Square target, Color victim, Bitboard occ) const noexcept;
    [[nodiscard]] bool isAttackedBy(Square target, Color by, Bitboard occ) const noexcept;
    [[nodiscard]] bool isAttackedByEnemy(Square target, Color victim, Bitboard occ) const noexcept;

    [[nodiscard]] IRONPHOENIX_FORCE_INLINE Bitboard checkers() const noexcept { return checkers_; }
    [[nodiscard]] IRONPHOENIX_FORCE_INLINE bool inCheck() const noexcept { return static_cast<bool>(checkers_); }
    [[nodiscard]] bool inCheck(Color c) const noexcept;
    [[nodiscard]] bool givesCheck(Move m) const noexcept;

    [[nodiscard]] Square capturedSquare(Move m, Color mover) const noexcept;
    [[nodiscard]] const CastleLane* castleLane(Color c, Move m) const noexcept;

    void makeMove(Move m, StateInfo& st);
    void undoMove(Move m, const StateInfo& st);

    [[nodiscard]] Key recomputeKey() const noexcept;
    [[nodiscard]] bool verify() const noexcept;

private:
    std::array<Piece, SQUARE_NB> board_{};
    std::array<std::array<Bitboard, PIECE_TYPE_NB>, COLOR_NB> pieceBB_{};
    std::array<Bitboard, COLOR_NB> colorOcc_{};
    Bitboard allOcc_{};
    std::array<Square, COLOR_NB> kingSq_{SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE};
    std::array<Square, COLOR_NB> enPassant_{SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE};
    std::array<std::array<CastleLane, 2>, COLOR_NB> castle_{};

    Color sideToMove_ = RED;
    std::uint8_t castlingRights_ = 0;
    std::uint8_t aliveMask_ = 0xF;
    std::uint8_t rulesetId_ = 0;
    Key key_ = 0;
    Bitboard checkers_{};

    void rawPut(Square sq, Piece p) noexcept;
    Piece rawRemove(Square sq) noexcept;
    void refreshCheckers() noexcept;
    void updateCastlingRightsForMove(Piece moving, Square from, Piece captured, Square capturedSq) noexcept;
};

}
