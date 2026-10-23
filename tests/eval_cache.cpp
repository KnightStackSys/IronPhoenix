#include "ironphoenix/eval.hpp"
#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/search.hpp"
#include "ironphoenix/tuning.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace ironphoenix;
namespace ironphoenix {
struct EvalCacheTestAccess {
    static int eval(SearchEngine& engine, const Position& p) {
        return engine.evaluate(p);
    }
    static std::uint64_t hits(const SearchEngine& engine) {
        return engine.evalCacheHits_;
    }
    static void newSearch(SearchEngine& engine) {
        ++engine.evalCacheEpoch_;
        engine.evalCacheActive_ = true;
        engine.evalCacheHits_ = 0;
    }
};
}
namespace {
int failures=0;
void expect(bool okay, const char* label) {
    if (!okay) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
Square sq(char f, int r) { return Geometry::square(f-'a',r-1); }
Position setup() {
    Position p;
    p.clear();
    p.placePiece(sq('g',1), makePiece(RED,KING));
    p.placePiece(sq('a',7), makePiece(BLUE,KING));
    p.placePiece(sq('g',14), makePiece(YELLOW,KING));
    p.placePiece(sq('n',7), makePiece(GREEN,KING));
    p.placePiece(sq('e',5), makePiece(RED,KNIGHT));
    p.placePiece(sq('d',8), makePiece(BLUE,BISHOP));
    p.placePiece(sq('f',10), makePiece(YELLOW,ROOK));
    p.placePiece(sq('j',8), makePiece(GREEN,QUEEN));
    p.placePiece(sq('f',3), makePiece(RED,PAWN));
    p.setSideToMove(RED);
    p.finalizeSetup();
    return p;
}
void test() {
    expect(!NNUE::loaded(), "test exact HCE scores");
    Tuning::resetSearch();
    Tuning::resetEval();
    auto engine=std::make_unique<SearchEngine>();
    auto p=setup();
    const int baseline=Eval::evaluate(p);

    expect(Tuning::search().evalCacheEnabled==1, "default enabled");
    expect(Tuning::setSearchParameter("SearchEvalCache",0),"UCI accepts disable");
    expect(Tuning::setSearchParameter("SearchEvalCache",1),"UCI accepts enable");
    expect(!Tuning::setSearchParameter("SearchEvalCache",2),"UCI rejects invalid option");
    std::ostringstream options;
    Tuning::printUciOptions(options);
    expect(options.str().find("option name SearchEvalCache type spin default 1 min 0 max 1")
        != std::string::npos, "UCI option advertised");

    // Per-worker entries start inactive; the first call cannot hit stale data.
    expect(EvalCacheTestAccess::eval(*engine,p)==baseline,"inactive cache exact");
    expect(EvalCacheTestAccess::hits(*engine)==0, "inactive cache no hits");
    EvalCacheTestAccess::newSearch(*engine);
    expect(EvalCacheTestAccess::eval(*engine,p)==baseline,"first lookup full eval");
    expect(EvalCacheTestAccess::hits(*engine)==0, "first lookup misses");
    expect(EvalCacheTestAccess::eval(*engine,p)==baseline,"second lookup exact cache");
    expect(EvalCacheTestAccess::hits(*engine)==1, "repeated key hits");

    p.setSideToMove(BLUE);
    p.finalizeSetup();
    expect(EvalCacheTestAccess::eval(*engine,p)==Eval::evaluate(p),
        "side-to-move key changes cached score");

    p.setSideToMove(RED);
    p.finalizeSetup();
    const auto original = Tuning::eval();
    Tuning::eval().knightMobility += 15;
    const int changed = Eval::evaluate(p);
    expect(changed != baseline, "changing eval weight affects fixture score");
    // Options are only modified after stop; the next search bumps epoch.
    EvalCacheTestAccess::newSearch(*engine);
    expect(EvalCacheTestAccess::eval(*engine,p)==changed,
        "new search never reuses stale evaluation across tuning changes");

    Tuning::search().evalCacheEnabled=0;
    const int raw=EvalCacheTestAccess::eval(*engine,p);
    expect(raw==Eval::evaluate(p),"disabled cache returns exact evaluation");
    expect(EvalCacheTestAccess::hits(*engine)==0,
        "disabled cache never returns hits");

    Tuning::eval()=original;
    Tuning::resetSearch();
}
}
int main() {
    Geometry::init();
    test();
    if(!failures) std::cout<<"Exact search-local eval cache tests: PASS\n";
    return failures ? 1 : 0;
}
