#include "ironphoenix/geometry.hpp"
#include "ironphoenix/king_legality.hpp"
#include "ironphoenix/movegen.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/search.hpp"
#include "ironphoenix/tuning.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

using namespace ironphoenix;
namespace {
int failures=0;
std::uint64_t checked=0, skipped=0;
void expect(bool ok,const char* why) {
    if(!ok){ ++failures; std::cerr<<"FAIL: "<<why<<'\n'; }
}
Square sq(char f,int rank) {return Geometry::square(f-'a',rank-1);}
Position fixture() {
    Position p;
    p.clear();
    p.placePiece(sq('g',1),makePiece(RED,KING));
    p.placePiece(sq('a',7),makePiece(BLUE,KING));
    p.placePiece(sq('g',14),makePiece(YELLOW,KING));
    p.placePiece(sq('n',7),makePiece(GREEN,KING));
    p.placePiece(sq('e',5),makePiece(RED,KNIGHT));
    p.placePiece(sq('i',5),makePiece(RED,BISHOP));
    p.placePiece(sq('h',10),makePiece(YELLOW,ROOK));
    p.placePiece(sq('c',6),makePiece(BLUE,BISHOP));
    p.placePiece(sq('k',9),makePiece(GREEN,QUEEN));
    p.placePiece(sq('f',3),makePiece(RED,PAWN));
    p.placePiece(sq('i',12),makePiece(YELLOW,PAWN));
    p.placePiece(sq('c',7),makePiece(BLUE,PAWN));
    p.placePiece(sq('l',8),makePiece(GREEN,PAWN));
    p.setSideToMove(RED);
    p.finalizeSetup();
    return p;
}

void verifyPosition(Position& pos) {
    const Color mover=pos.sideToMove();
    const bool kingAlreadyChecked=pos.inCheck();
    const Key before=pos.key();
    MoveList moves;
    generatePseudoLegalMoves(pos,moves);
    for (const Move move:moves) {
        if (move.isCapture() && isTerminalKingCapture(pos,move))
            continue;
        const bool needsCheck=kingAlreadyChecked || kingCheckCanChange(pos,move);
        StateInfo st{};
        pos.makeMove(move,st);
        const bool actual=pos.inCheck(mover);
        if (!needsCheck) {
            ++skipped;
            if (actual) expect(false,"unsafe self-check incorrectly skipped");
        }
        ++checked;
        pos.undoMove(move,st);
        if (pos.key()!=before || !pos.verify())
            expect(false,"move/undo preserves position and Zobrist hash");
    }
}

std::pair<std::string,std::string> depth3(int on) {
    Tuning::search().fastKingLegality=on;
    auto engine=std::make_unique<SearchEngine>();
    SearchLimits limits{};
    limits.depth=3;
    std::ostringstream out;
    engine->start(fixture(),limits,out);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while (engine->searching() && std::chrono::steady_clock::now()<until)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    expect(!engine->searching(),"depth3 terminates");
    engine->stopAndWait();
    const auto text=out.str();
    const auto b=text.rfind("bestmove ");
    expect(b!=std::string::npos,"bestmove output present");
    const std::string best=b==std::string::npos?"":text.substr(
        b,text.find('\n',b)-b);
    const auto i=text.rfind("info depth 3 ");
    expect(i!=std::string::npos,"depth3 info present");
    const auto a=i==std::string::npos?i:text.find(" score ",i);
    const auto z=a==std::string::npos?a:text.find(" nodes ",a);
    const std::string score=(a==std::string::npos || z==std::string::npos)
        ?"":text.substr(a,z-a);
    return {best,score};
}

void test() {
    Tuning::resetSearch();
    expect(Tuning::search().fastKingLegality==1,"enabled by default");
    expect(Tuning::setSearchParameter("SearchFastKingLegality",0),"disable accepted");
    expect(Tuning::setSearchParameter("SearchFastKingLegality",1),"enable accepted");
    expect(!Tuning::setSearchParameter("SearchFastKingLegality",2),"invalid setting rejected");
    std::ostringstream options;
    Tuning::printUciOptions(options);
    expect(options.str().find(
        "option name SearchFastKingLegality type spin default 1 min 0 max 1")
        != std::string::npos,"UCI setting advertised");

    Position p=fixture();
    for (unsigned ci=0;ci<COLOR_NB;++ci) {
        p.setSideToMove(static_cast<Color>(ci));
        p.finalizeSetup();
        verifyPosition(p);
    }
    p=fixture();
    for(int ply=0;ply<24;++ply) {
        verifyPosition(p);
        MoveList moves;
        generateLegalMoves(p,moves);
        Move selected{};
        for(std::size_t n=0;n<moves.size;++n) {
            const Move move=moves.moves[(n+static_cast<std::size_t>(ply)*11)%moves.size];
            if (!(move.isCapture() && isTerminalKingCapture(p,move))) {
                selected=move;
                break;
            }
        }
        if(!selected)break;
        StateInfo state{};
        p.makeMove(selected,state);
        expect(p.verify(),"successor position valid");
    }
    expect(skipped>100,"many moves skip redundant king check");
    expect(checked>500,"hundreds of 4PC pseudo-legal moves validated");
    const auto original=depth3(0);
    const auto optimized=depth3(1);
    expect(original==optimized,
        "depth3 bestmove/score unchanged by optimized king legality");
    Tuning::resetSearch();
}
}
int main(){
    Geometry::init();
    test();
    if(!failures)
        std::cout<<"Fast 4PC king legality tests: PASS ("<<skipped
                 <<" of "<<checked<<" candidate checks avoided)\n";
    return failures?1:0;
}
