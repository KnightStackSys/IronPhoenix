#include "ironphoenix/eval.hpp"
#include "ironphoenix/geometry.hpp"
#include "ironphoenix/movegen.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <iostream>
#include <sstream>
#include <string>

using namespace ironphoenix;
namespace {
int failures=0;
void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
Square sq(char f, int r) { return Geometry::square(f - 'a', r - 1); }
Position fixture() {
    Position p;
    p.clear();
    p.placePiece(sq('g',1), makePiece(RED,KING));
    p.placePiece(sq('a',7), makePiece(BLUE,KING));
    p.placePiece(sq('g',14), makePiece(YELLOW,KING));
    p.placePiece(sq('n',7), makePiece(GREEN,KING));
    p.placePiece(sq('e',5), makePiece(RED, KNIGHT));
    p.placePiece(sq('h',11), makePiece(YELLOW, BISHOP));
    p.placePiece(sq('c',6), makePiece(BLUE, ROOK));
    p.placePiece(sq('k',8), makePiece(GREEN, QUEEN));
    for (char file: {'d','e','f','h','i','j'}) {
        p.placePiece(sq(file,3), makePiece(RED,PAWN));
        p.placePiece(sq(file,12), makePiece(YELLOW,PAWN));
    }
    for (int rank: {4,5,6,8,9,10}) {
        p.placePiece(sq('c',rank), makePiece(BLUE,PAWN));
        p.placePiece(sq('l',rank), makePiece(GREEN,PAWN));
    }
    p.setSideToMove(RED);
    p.finalizeSetup();
    return p;
}
void compare(const Position& pos) {
    Tuning::search().pawnAttackCache=0;
    const int reference=Eval::evaluate(pos);
    Tuning::search().pawnAttackCache=1;
    const int cached=Eval::evaluate(pos);
    check(reference==cached, "cached and raw HCE evaluation exactly equal");
    check(Eval::evaluate(pos)==cached,"second evaluation matches cached result");
}
void test() {
    check(!NNUE::loaded(),"test uses HCE");
    Tuning::resetSearch();
    Tuning::resetEval();
    check(Tuning::search().pawnAttackCache==1,"default enabled");
    check(Tuning::setSearchParameter("SearchPawnAttackCache",0),"disable works");
    check(Tuning::setSearchParameter("SearchPawnAttackCache",1),"enable works");
    check(!Tuning::setSearchParameter("SearchPawnAttackCache",2),"invalid value rejected");
    std::ostringstream options;
    Tuning::printUciOptions(options);
    check(options.str().find("option name SearchPawnAttackCache type spin default 1 min 0 max 1")
        != std::string::npos,"UCI option exposed");

    Position p=fixture();
    for(unsigned ci=0;ci<COLOR_NB;++ci) {
        p.setSideToMove(static_cast<Color>(ci));
        p.finalizeSetup();
        compare(p);
    }
    p=fixture();
    for(int i=0;i<65;++i) {
        compare(p);
        MoveList legal;
        generateLegalMoves(p, legal);
        if(!legal.size) break;
        Move chosen{};
        for (std::size_t j=0;j<legal.size;++j) {
            const Move move=legal.moves[(j+static_cast<std::size_t>(i)*13u)%legal.size];
            if (!(move.isCapture() && isTerminalKingCapture(p,move))) {
                chosen=move;
                break;
            }
        }
        if (!chosen) break;
        StateInfo st{};
        p.makeMove(chosen,st);
        check(p.verify(),"successor valid");
    }
    // Explicitly change a pawn bitboard and test the new cache key.
    p=fixture();
    p.erasePiece(sq('d',3));
    p.finalizeSetup();
    compare(p);
    p.placePiece(sq('d',3), makePiece(RED,PAWN));
    p.finalizeSetup();
    compare(p);
    Tuning::resetSearch();
}
}
int main() {
    Geometry::init();
    test();
    if(!failures)std::cout<<"Pawn attack union cache: PASS\n";
    return failures?1:0;
}
