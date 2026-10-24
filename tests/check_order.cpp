#include "ironphoenix/geometry.hpp"
#include "ironphoenix/movegen.hpp"
#include "ironphoenix/search.hpp"
#include "ironphoenix/tuning.hpp"

#include <array>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <cstdint>
#include <iostream>
#include <memory>

using namespace ironphoenix;

namespace ironphoenix {
struct CheckOrderTestAccess {
    static void order(SearchEngine& s, const Position& pos, MoveList& moves,
                      std::array<int, MAX_MOVES>& scores,
                      std::array<std::uint64_t, (MAX_MOVES + 63) / 64>& checks) {
        s.orderMoves(pos, moves.begin(), moves.end(), Move{}, 1,
                     scores.data(), checks.data());
    }
};
}

namespace {
int failures=0;
void require(bool ok, const char* reason) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << reason << '\n'; }
}
Square sq(char f, int r) { return Geometry::square(f-'a', r-1); }

Position setup() {
    Position p;
    p.clear();
    p.placePiece(sq('g',1), makePiece(RED, KING));
    p.placePiece(sq('a',7), makePiece(BLUE, KING));
    p.placePiece(sq('g',14), makePiece(YELLOW, KING));
    p.placePiece(sq('n',7), makePiece(GREEN, KING));
    p.placePiece(sq('e',5), makePiece(RED, BISHOP));
    p.placePiece(sq('h',6), makePiece(RED, ROOK));
    p.placePiece(sq('h',4), makePiece(RED, KNIGHT));
    p.placePiece(sq('i',11), makePiece(YELLOW, QUEEN));
    p.placePiece(sq('f',10), makePiece(YELLOW, BISHOP));
    p.placePiece(sq('d',8), makePiece(BLUE, ROOK));
    p.placePiece(sq('c',6), makePiece(BLUE, KNIGHT));
    p.placePiece(sq('k',9), makePiece(GREEN, QUEEN));
    p.placePiece(sq('j',8), makePiece(GREEN, BISHOP));
    p.placePiece(sq('g',3), makePiece(RED, PAWN));
    p.placePiece(sq('g',12), makePiece(YELLOW, PAWN));
    p.placePiece(sq('c',7), makePiece(BLUE, PAWN));
    p.placePiece(sq('l',7), makePiece(GREEN, PAWN));
    p.setSideToMove(RED);
    p.finalizeSetup();
    return p;
}

void compare(const Position& p, SearchEngine& engine) {
    MoveList original;
    generatePseudoLegalMoves(p, original);
    if (!original.size)
        return;
    MoveList slow=original;
    MoveList fast=original;
    std::array<int,MAX_MOVES> scoresSlow{},scoresFast{};
    std::array<std::uint64_t,(MAX_MOVES+63)/64> checkSlow{},checkFast{};

    Tuning::search().fastCheckPrefilter=0;
    CheckOrderTestAccess::order(engine,p,slow,scoresSlow,checkSlow);
    Tuning::search().fastCheckPrefilter=1;
    CheckOrderTestAccess::order(engine,p,fast,scoresFast,checkFast);

    require(fast.size == slow.size, "move count unchanged");
    for (std::size_t i=0;i<slow.size;++i) {
        if (slow.moves[i] != fast.moves[i] ||
            scoresSlow[i] != scoresFast[i] ||
            bool(checkSlow[i/64] & (1ull<<(i%64))) !=
                bool(checkFast[i/64] & (1ull<<(i%64)))) {
            require(false,"fast and slow checking order/metadata must match");
            break;
        }
    }
}

std::pair<std::string,std::string> runFixedDepth(int fast) {
    Tuning::search().fastCheckPrefilter=fast;
    auto engine=std::make_unique<SearchEngine>();
    SearchLimits limits{};
    limits.depth=3;
    std::ostringstream out;
    engine->start(setup(), limits, out);
    const auto start=std::chrono::steady_clock::now();
    while (engine->searching() &&
           std::chrono::steady_clock::now()-start < std::chrono::seconds(12))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    require(!engine->searching(), "depth-3 search completed");
    engine->stopAndWait();
    const std::string result=out.str();
    const std::size_t best=result.rfind("bestmove ");
    require(best!=std::string::npos, "bestmove emitted");
    const std::string finalMove=best==std::string::npos ? "" :
        result.substr(best, result.find('\n',best)-best);

    // Ignore elapsed time/NPS, which are nondeterministic across searches.
    const std::size_t info=result.rfind("info depth 3 ");
    require(info!=std::string::npos,"third-depth info emitted");
    const std::size_t score=info==std::string::npos ? std::string::npos :
        result.find(" score ",info);
    const std::size_t nodes=score==std::string::npos ? score :
        result.find(" nodes ",score);
    const std::string finalScore=(score==std::string::npos || nodes==std::string::npos)
        ? "" : result.substr(score,nodes-score);
    return {finalMove,finalScore};
}

void test() {
    const auto defaults=Tuning::search();
    require(defaults.fastCheckPrefilter==1, "fast check default enabled");
    require(Tuning::setSearchParameter("SearchFastCheckPrefilter",0), "can disable fast check");
    require(Tuning::setSearchParameter("SearchFastCheckPrefilter",1), "can enable fast check");
    require(!Tuning::setSearchParameter("SearchFastCheckPrefilter",2), "invalid fast check rejected");
    auto engine=std::make_unique<SearchEngine>();
    Position p=setup();
    for (unsigned ci=0;ci<COLOR_NB;++ci) {
        p.setSideToMove(static_cast<Color>(ci));
        p.finalizeSetup();
        compare(p,*engine);
    }

    // Already existing rook check must not be falsely filtered out.
    p=setup();
    p.placePiece(sq('c',9), makePiece(RED, ROOK));
    p.setSideToMove(RED);
    p.finalizeSetup();
    compare(p,*engine);

    // Compare multiple legal 4-player successor states.
    p=setup();
    for (int ply=0;ply<18;++ply) {
        compare(p,*engine);
        MoveList legal;
        generateLegalMoves(p, legal);
        Move selected{};
        for(std::size_t k=0;k<legal.size;++k) {
            const Move m=legal.moves[(k+static_cast<std::size_t>(ply)*7u)%legal.size];
            if (!isTerminalKingCapture(p,m)) { selected=m; break; }
        }
        if (!selected) break;
        StateInfo state{};
        p.makeMove(selected,state);
        require(p.verify(), "position valid after selected legal move");
    }
    const auto originalSearch=runFixedDepth(0);
    const auto optimizedSearch=runFixedDepth(1);
    require(originalSearch==optimizedSearch,
            "fast prefilter preserves depth-3 bestmove and score");
    Tuning::search()=defaults;
}
}
int main() {
    Geometry::init();
    test();
    if (!failures) std::cout<<"Check ordering fast prefilter: PASS\n";
    return failures?1:0;
}
