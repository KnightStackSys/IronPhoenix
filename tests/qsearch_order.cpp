#include "ironphoenix/geometry.hpp"
#include "ironphoenix/movegen.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/search.hpp"
#include "ironphoenix/tuning.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace ironphoenix;
namespace ironphoenix {
struct QSearchOrderTestAccess {
    static int score(SearchEngine& engine, Position& pos) {
        engine.stopRequested_.store(false, std::memory_order_relaxed);
        engine.limits_ = SearchLimits{};
        return engine.qsearch(pos, -20000, 20000, 1, false);
    }
};
}
namespace {
int failures=0;
void check(bool ok,const char* msg) {
    if(!ok) { ++failures; std::cerr<<"FAIL: "<<msg<<'\n'; }
}
Square sq(char f,int r) {return Geometry::square(f-'a',r-1);}
Position fixture() {
    Position p;
    p.clear();
    p.placePiece(sq('g',1), makePiece(RED,KING));
    p.placePiece(sq('a',7), makePiece(BLUE,KING));
    p.placePiece(sq('g',14), makePiece(YELLOW,KING));
    p.placePiece(sq('n',7), makePiece(GREEN,KING));
    p.placePiece(sq('g',4), makePiece(RED,ROOK));
    p.placePiece(sq('e',5), makePiece(RED,KNIGHT));
    p.placePiece(sq('i',10), makePiece(YELLOW,BISHOP));
    p.placePiece(sq('c',7), makePiece(BLUE,ROOK));
    p.placePiece(sq('k',8), makePiece(GREEN,KNIGHT));
    p.placePiece(sq('g',5), makePiece(BLUE,PAWN));
    p.placePiece(sq('f',6), makePiece(BLUE,PAWN));
    p.placePiece(sq('i',9), makePiece(GREEN,PAWN));
    p.placePiece(sq('h',11), makePiece(YELLOW,PAWN));
    p.setSideToMove(RED);
    p.finalizeSetup();
    return p;
}
void compare(Position p) {
    const Key key=p.key();
    Tuning::search().qsearchLazyOrdering=0;
    auto standard=std::make_unique<SearchEngine>();
    const int baseline=QSearchOrderTestAccess::score(*standard,p);
    Tuning::search().qsearchLazyOrdering=1;
    auto optimized=std::make_unique<SearchEngine>();
    const int actual=QSearchOrderTestAccess::score(*optimized,p);
    check(baseline==actual, "QSearch score unchanged with lazy ordering");
    check(p.key()==key && p.verify(),"qsearch preserves caller position");
}
void test() {
    Tuning::resetSearch();
    check(Tuning::search().qsearchLazyOrdering==1,"default enabled");
    check(Tuning::setSearchParameter("SearchQSearchLazyOrdering",0),"disable accepted");
    check(Tuning::setSearchParameter("SearchQSearchLazyOrdering",1),"enable accepted");
    check(!Tuning::setSearchParameter("SearchQSearchLazyOrdering",2),"invalid value rejected");
    std::ostringstream opts;
    Tuning::printUciOptions(opts);
    check(opts.str().find(
        "option name SearchQSearchLazyOrdering type spin default 1 min 0 max 1")
        != std::string::npos, "UCI option published");
    Position p=fixture();
    compare(p);
    for(unsigned ci=0;ci<COLOR_NB;++ci) {
        p.setSideToMove(static_cast<Color>(ci));
        p.finalizeSetup();
        compare(p);
    }
    // Check on several legal successor positions, not just the original
    // cross-board position. Skip terminal king captures in the fixture.
    p=fixture();
    for(int ply=0;ply<6;++ply) {
        compare(p);
        MoveList moves;
        generateLegalMoves(p,moves);
        Move selected{};
        for(std::size_t i=0;i<moves.size;++i) {
            const Move m=moves.moves[(i+static_cast<std::size_t>(ply)*5u)%moves.size];
            if (!(m.isCapture() && isTerminalKingCapture(p,m))) {
                selected=m;
                break;
            }
        }
        if (!selected) break;
        StateInfo st{};
        p.makeMove(selected,st);
        check(p.verify(),"successor valid");
    }
    Tuning::resetSearch();
}
}
int main() {
    Geometry::init();
    test();
    if(!failures) std::cout<<"QSearch tactical ordering: PASS\n";
    return failures?1:0;
}
