#include "ironphoenix/geometry.hpp"
#include "ironphoenix/history.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/search.hpp"
#include "ironphoenix/tuning.hpp"
#include <iostream>
#include <memory>
#include <sstream>
using namespace ironphoenix;
namespace {
int failures = 0;
void expect(bool ok, const char* s) { if (!ok) { ++failures; std::cerr << "FAIL: " << s << '\n'; } }
Square sq(char f, int r) { return Geometry::square(f - 'a', r - 1); }
Position setup() {
    Position p;
    p.clear();
    p.placePiece(sq('g',1),makePiece(RED,KING));
    p.placePiece(sq('a',7),makePiece(BLUE,KING));
    p.placePiece(sq('g',14),makePiece(YELLOW,KING));
    p.placePiece(sq('n',7),makePiece(GREEN,KING));
    p.placePiece(sq('f',2),makePiece(RED,PAWN));
    p.placePiece(sq('b',6),makePiece(BLUE,PAWN));
    p.placePiece(sq('h',13),makePiece(YELLOW,PAWN));
    p.placePiece(sq('m',8),makePiece(GREEN,PAWN));
    p.finalizeSetup();
    return p;
}
void valid(const Position& p) {
    expect(p.pawnKey() == p.recomputePawnKey(), "pawn hash matches recomputation");
    expect(p.verify(), "full Position remains valid");
}
}
int main() {
    Geometry::init();
    Tuning::resetAll();
    Position p = setup();
    const Key base = p.pawnKey();
    const Key full = p.key();
    valid(p);
    p.setSideToMove(BLUE);
    expect(p.pawnKey() == base, "turn rotation does not affect pawn key");
    p.setSideToMove(RED);
    p.setCastlingRights(1u);
    p.setEnPassant(RED,sq('f',3));
    expect(p.pawnKey() == base, "castling/en passant flags excluded");
    p.setEnPassant(RED,SQ_NONE);
    p.setCastlingRights(0);
    expect(p.key() == full, "non-pawn flags fully restored");

    p.placePiece(sq('h',4),makePiece(RED,KNIGHT));
    p.finalizeSetup();
    const Key beforeKnight = p.pawnKey();
    StateInfo knightState;
    const Move knightMove(sq('h',4),sq('i',6));
    p.makeMove(knightMove,knightState);
    expect(p.pawnKey() == beforeKnight, "knight movement preserves pawn signature");
    valid(p);
    p.undoMove(knightMove,knightState);
    expect(p.pawnKey() == beforeKnight, "knight undo restores signature");
    p.erasePiece(sq('h',4));
    p.finalizeSetup();

    const Key beforePush = p.pawnKey();
    StateInfo pushState;
    const Move push(sq('f',2),sq('f',3));
    p.makeMove(push,pushState);
    expect(p.pawnKey() != beforePush, "pawn push changes structure");
    valid(p);
    p.undoMove(push,pushState);
    expect(p.pawnKey() == beforePush, "pawn undo restores structure");

    p.placePiece(sq('e',3),makePiece(BLUE,PAWN));
    p.finalizeSetup();
    const Key beforeCapture = p.pawnKey();
    StateInfo captureState;
    const Move capture(sq('f',2),sq('e',3),makePiece(BLUE,PAWN));
    p.makeMove(capture,captureState);
    expect(p.pawnKey() != beforeCapture, "pawn capture changes structure");
    valid(p);
    p.undoMove(capture,captureState);
    expect(p.pawnKey() == beforeCapture, "capture undo restores structure");
    p.erasePiece(sq('e',3));

    p.erasePiece(sq('f',2));
    p.placePiece(sq('g',10),makePiece(RED,PAWN));
    p.finalizeSetup();
    const Key beforePromotion = p.pawnKey();
    StateInfo promotionState;
    const Move promotion(sq('g',10),sq('g',11),NO_PIECE,PROMO_QUEEN);
    p.makeMove(promotion,promotionState);
    expect(p.pawnKey() != beforePromotion, "promotion removes pawn hash entry");
    valid(p);
    p.undoMove(promotion,promotionState);
    expect(p.pawnKey() == beforePromotion, "promotion undo restores pawn");
    StateInfo nullState;
    const Key beforeNull = p.pawnKey();
    p.makeNullMove(nullState);
    expect(p.pawnKey() == beforeNull, "null move preserves signature");
    p.undoNullMove(nullState);
    expect(p.pawnKey() == beforeNull, "null undo preserves signature");

    p.setAliveMask(0xDu);
    expect(p.pawnKey() != beforeNull, "alive mask changes pawn context");
    valid(p);
    p.setAliveMask(0xFu);
    expect(p.pawnKey() == beforeNull, "alive mask restores pawn context");
    p.setRulesetId(3);
    expect(p.pawnKey() != beforeNull, "ruleset changes pawn context");
    p.setRulesetId(0);
    expect(p.pawnKey() == beforeNull, "ruleset restored");

    HistoryTables history;
    const Piece knight = makePiece(RED,KNIGHT);
    const Square dst = sq('h',6);
    expect(history.pawnScore(beforeNull,knight,dst) == 0, "new history zero");
    history.updatePawn(beforeNull,knight,dst,600);
    expect(history.pawnScore(beforeNull,knight,dst) > 0, "quiet cutoff rewarded");
    expect(history.pawnScore(beforeNull,makePiece(YELLOW,KNIGHT),dst) == 0, "partner separately indexed");
    expect(history.pawnScore(beforeNull,knight,sq('i',6)) == 0, "destination isolated");
    expect(history.pawnScore(beforeNull ^ 1u,knight,dst) == 0, "different pawn context isolated");
    history.updatePawn(beforeNull,knight,dst,-1000);
    expect(history.pawnScore(beforeNull,knight,dst) < 0, "failed quiet penalized");
    for (int i=0;i<50;++i) history.updatePawn(beforeNull,knight,dst,30000);
    expect(history.pawnScore(beforeNull,knight,dst) <= HistoryTables::MAX_HISTORY, "history bounded");
    history.clear();
    expect(history.pawnScore(beforeNull,knight,dst) == 0, "new game clears pawn history");

    expect(Tuning::search().pawnHistoryWeight == 50, "default 50 pct");
    expect(Tuning::setSearchParameter("SearchPawnHistoryWeight",0), "0 accepted");
    expect(Tuning::setSearchParameter("SearchPawnHistoryWeight",200), "200 accepted");
    expect(!Tuning::setSearchParameter("SearchPawnHistoryWeight",-1), "-1 rejected");
    expect(!Tuning::setSearchParameter("SearchPawnHistoryWeight",201), "201 rejected");
    auto engine = std::make_unique<SearchEngine>();
    std::ostringstream output;
    expect(Tuning::handleSetOption("name SearchPawnHistoryWeight value 75",*engine,output), "UCI recognizes option");
    expect(Tuning::search().pawnHistoryWeight == 75, "UCI stores weight");
    Tuning::printUciOptions(output);
    Tuning::printCurrent(output);
    expect(output.str().find("option name SearchPawnHistoryWeight type spin default 50 min 0 max 200") != std::string::npos, "option advertised");
    expect(output.str().find("SearchPawnHistoryWeight=75") != std::string::npos, "current tuning prints setting");
    Tuning::resetSearch();
    expect(Tuning::search().pawnHistoryWeight == 50, "reset restores default");

    if (!failures) std::cout << "4PC pawn-structure history: PASS\n";
    return failures ? 1 : 0;
}
