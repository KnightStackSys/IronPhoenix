#include "ironphoenix/geometry.hpp"
#include "ironphoenix/movegen.hpp"
#include "ironphoenix/search.hpp"
#include "ironphoenix/tt.hpp"
#include "ironphoenix/uci.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace ironphoenix;
namespace {

int failures = 0;
void check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << '\n'; ++failures; }
}
Square sq(char f, int r) { return Geometry::square(f-'a', r-1); }

Position position() {
    Position p;
    p.clear();
    p.placePiece(sq('g',1), makePiece(RED, KING));
    p.placePiece(sq('a',7), makePiece(BLUE, KING));
    p.placePiece(sq('g',14), makePiece(YELLOW, KING));
    p.placePiece(sq('n',7), makePiece(GREEN, KING));
    p.placePiece(sq('e',4), makePiece(RED, BISHOP));
    p.placePiece(sq('h',11), makePiece(YELLOW, BISHOP));
    p.placePiece(sq('c',6), makePiece(BLUE, ROOK));
    p.placePiece(sq('l',9), makePiece(GREEN, ROOK));
    p.placePiece(sq('f',3), makePiece(RED, PAWN));
    p.placePiece(sq('i',12), makePiece(YELLOW, PAWN));
    p.placePiece(sq('c',5), makePiece(BLUE, PAWN));
    p.placePiece(sq('l',10), makePiece(GREEN, PAWN));
    p.setSideToMove(RED);
    p.finalizeSetup();
    return p;
}

bool waitDone(SearchEngine& engine, int attempts=700) {
    for(int i=0; i<attempts; ++i) {
        if (!engine.searching()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return !engine.searching();
}

int count(const std::string& value, const std::string& pattern) {
    int result=0;
    for(std::size_t at=0;(at=value.find(pattern,at))!=std::string::npos;at+=pattern.size())
        ++result;
    return result;
}

void testSearch(int threads, bool multiPV) {
    auto engine=std::make_unique<SearchEngine>();
    check(engine->setThreads(threads), "valid Threads accepted");
    check(engine->threads()==threads, "Threads stores value");
    const Position pos=position();
    const auto key=pos.key();
    SearchLimits limits;
    limits.nodes=12000;
    std::ostringstream out;
    if(multiPV) engine->startMultiPV(pos, limits, 2, out);
    else engine->start(pos, limits, out);
    const bool finished=waitDone(*engine);
    engine->stopAndWait();
    check(finished, "SMP search finishes with node budget");
    check(count(out.str(), "bestmove ")==1, "one and only one bestmove");
    check(out.str().find("info depth ")!=std::string::npos, "primary emits depth info");
    if(multiPV) check(out.str().find("multipv 1")!=std::string::npos,"MultiPV primary line");
    check(pos.key()==key && pos.verify(), "caller position unchanged");
}

void testStop() {
    auto engine=std::make_unique<SearchEngine>();
    engine->setThreads(3);
    SearchLimits limits;
    limits.infinite=true;
    std::ostringstream out;
    engine->start(position(),limits,out);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    engine->stopAndWait();
    check(!engine->searching(), "stop joins all helpers");
    check(count(out.str(),"bestmove ")==1,"stop produces one bestmove");
    engine->newGame();
    check(engine->setThreads(2),"Threads can change after stop");
    check(engine->setHashSizeMB(16),"Hash safely resizes when stopped");
}

void testTT() {
    TranspositionTable tt(1);
    tt.setConcurrent(true);
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    for(int worker=0;worker<4;++worker){
        threads.emplace_back([&tt,&start,worker]{
            while(!start.load(std::memory_order_acquire)) std::this_thread::yield();
            for(int i=0;i<8000;++i){
                const Key key=static_cast<Key>(1+(i*71+worker*47)%8192);
                tt.store(key,i%24,100+(i%250),TTBound::Lower,Move{},false,1);
                TTProbe found{};
                if(tt.probe(key,1,found)) {
                    if(found.bound!=TTBound::Lower || found.depth<0)
                        std::terminate();
                }
            }
        });
    }
    start.store(true,std::memory_order_release);
    for(auto& t:threads)t.join();
    check(tt.hashfull()>=0 && tt.hashfull()<=1000, "concurrent TT hashfull valid");
    tt.setConcurrent(false);
    tt.clear();
    TTProbe found{};
    check(!tt.probe(123,1,found),"TT clear after workers joined");
}

void testUci() {
    auto shell=std::make_unique<UciShell>();
    std::istringstream input("uci\nsetoption name Threads value 3\nquit\n");
    std::ostringstream output;
    shell->run(input,output);
    check(output.str().find("option name Threads type spin default 1 min 1 max 64")!=std::string::npos,
          "UCI advertises Threads");
    check(output.str().find("info string Threads set to 3")!=std::string::npos,
          "UCI accepts Threads");
}

} // namespace
int main() {
    Geometry::init();
    testUci();
    testTT();
    testSearch(1,false);
    testSearch(3,false);
    testSearch(2,true);
    testStop();
    if(!failures)std::cout<<"Lazy SMP smoke/concurrency: PASS\n";
    return failures?1:0;
}
