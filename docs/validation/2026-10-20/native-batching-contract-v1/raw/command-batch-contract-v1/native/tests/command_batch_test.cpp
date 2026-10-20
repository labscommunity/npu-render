#include "../include/command_batch.hpp"
#include <iostream>
#include <random>
using namespace nr::batch;
static void check(bool ok) {
    if (!ok) throw std::runtime_error("command batch test failed");
}
template<class F> static void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument &) { rejected = true; }
    check(rejected);
}
int main() {
    try {
        const Resource texture{1,1,0}, target{2,1,0};
        const Version t1{42,texture,1}, t2{42,texture,2};
        const Version d1{42,target,1}, d2{42,target,2}, d3{42,target,3};
        Ledger ledger(42);
        ledger.import_resource(t1, 16);
        ledger.import_resource(d1, 64);
        // Write texture v2, then explicitly read both the old and new texture.
        Request r{42,1,{t1,d1},{{{},t1,t2,{1}},{{t1},d1,d2,{2}},{{t2,d2},d2,d3,{3}}}};
        auto bad = r;
        bad.commands[0].reads = {t2}; // No reading a future result.
        rejects([&]{ledger.prepare(bad);});
        bad = r; bad.commands[2].before = d1; // Stale overwrite.
        rejects([&]{ledger.prepare(bad);});
        bad = r; bad.commands[0].after.context = 99;
        rejects([&]{ledger.prepare(bad);});
        bad = r; bad.imports.push_back(t1);
        rejects([&]{ledger.prepare(bad);});
        auto ticket = ledger.prepare(r);
        rejects([&]{ledger.prepare(r);});
        check(ledger.retained_bytes(ticket) == 227);
        r.commands[1].state[0] = 99; r.commands.clear();
        check(ledger.request(ticket).commands[1].state[0] == 2);
        check(ledger.committed(target) == d1);
        check(ledger.read_ready(target) == Readiness::still_drawing);
        rejects([&]{ledger.complete(ticket,{t2,d2});}); // Partial completion.
        check(ledger.committed(target) == d1);
        const auto retry = ledger.retry(ticket);
        rejects([&]{ledger.complete(ticket,{t2,d2,d3});}); // Late old attempt.
        check(ledger.request(retry).commands.size() == 3);
        ledger.complete(retry,{t2,d2,d3});
        check(ledger.committed(texture) == t2 && ledger.committed(target) == d3);
        check(ledger.read_ready(target) == Readiness::ready);
        rejects([&]{ledger.complete(retry,{t2,d2,d3});});
        Request pending{42,2,{t2,d3},{{{t2},d3,{42,target,4},{7}}}};
        ticket = ledger.prepare(pending);
        check(ledger.read_ready(texture) == Readiness::ready);
        ledger.retire(texture);
        check(ledger.read_ready(texture) == Readiness::unavailable);
        check(ledger.request(ticket).commands[0].reads[0] == t2);
        ledger.abandon(ticket);
        check(ledger.committed(target) == d3); // Failure publishes nothing.
        rejects([&]{ledger.import_resource(t1,16);}); // Generation reuse.
        ledger.import_resource({42,{1,2,0},1},16); // Explicit new allocation.
        rejects([&]{ledger.prepare(pending);}); // Abandoned sequence consumed.

        Limits limits; limits.retained_bytes=143;
        Ledger bounded(42,limits); bounded.import_resource(d1,64);
        Request too_large{42,1,{d1},{{{},d1,d2,{1}},{{},d2,d3,{2}}}};
        rejects([&]{bounded.prepare(too_large);});
        check(bounded.committed(target) == d1);
        too_large.commands.resize(1);
        ticket=bounded.prepare(too_large); bounded.abandon(ticket);
        Limits tiny; tiny.state_bytes=1;
        Ledger state_limit(42,tiny); state_limit.import_resource(d1,64);
        auto oversized=too_large; oversized.sequence=1;
        oversized.commands[0].state={1,2};
        rejects([&]{state_limit.prepare(oversized);});
        oversized.commands[0].state.clear();
        rejects([&]{state_limit.prepare(oversized);});
        oversized.commands[0].state={1};
        ticket=state_limit.prepare(oversized);
        rejects([&]{state_limit.complete(ticket,{{42,target,3}});});
        state_limit.retire(target);
        state_limit.complete(ticket,{d2});
        check(state_limit.read_ready(target) == Readiness::unavailable);
        rejects([&]{state_limit.committed(target);});
        state_limit.import_resource({42,{2,2,0},1},64);
        rejects([&]{state_limit.prepare({42,2,{d2},{{{},d2,d3,{1}}}});});
        auto overflow=std::numeric_limits<uint64_t>::max();
        Ledger maximum(42); maximum.import_resource({42,target,overflow},64);
        rejects([&]{maximum.prepare({42,1,{{42,target,overflow}},
            {{{},{42,target,overflow},{42,target,0},{1}}}});});

        // Independent integer model: arbitrary success/abandon/retry schedules
        // cannot publish partial state or accept completion from an old attempt.
        Ledger model(19); Resource key{9,1,0}; model.import_resource({19,key,1},4);
        uint64_t expected=1;
        std::mt19937 random(1773);
        for(uint64_t sequence=1;sequence<=10000;++sequence) {
            const uint64_t count=1+random()%8;
            Request work{19,sequence,{{19,key,expected}},{}};
            for(uint64_t i=0;i<count;++i)
                work.commands.push_back({{{19,key,expected+i}},{19,key,expected+i},
                                         {19,key,expected+i+1},{uint8_t(i)}});
            auto current=model.prepare(work);
            if(random()%2) {
                const auto old=current; current=model.retry(current);
                rejects([&]{model.abandon(old);});
            }
            check(model.committed(key).number == expected);
            if(random()%3) {
                std::vector<Version> outputs;
                for(const auto &command:work.commands)outputs.push_back(command.after);
                model.complete(current,outputs); expected+=count;
            } else model.abandon(current);
            check(model.committed(key).number == expected &&
                  model.read_ready(key) == Readiness::ready);
        }
        std::cout << "command batch CPU contract passed; no rendering executed\n";
    } catch(const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
