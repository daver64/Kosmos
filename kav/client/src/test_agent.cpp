// Plays a fake game against a running KosmosAV server: takes a job, "flies" it (heartbeats at the base, then at the destination) and checks the
// Agent completes the delivery by itself.   test_agent SERVER_URL API_KEY FROM_ID TO_ID SHIP_ID
#include "agent.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    struct FakeGame : kav::GameSource
    {
        kav::GameState state;
        std::string notified;
        kav::GameState read() override { return state; }
        void notify(const std::string &t) override { notified = t; }
    };

    int failures = 0;
    void check(bool ok, const char *what)
    {
        std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok)
            ++failures;
    }
}

int main(int argc, char **argv)
{
    if (argc < 6)
    {
        std::fprintf(stderr, "usage: test_agent SERVER KEY FROM TO SHIP\n");
        return 2;
    }
    std::string from = argv[3], to = argv[4], ship = argv[5];
    FakeGame game;
    kav::Agent agent({argv[1], argv[2]}, &game);
    check(agent.load_registry(), "registry loads");
    kav::Me me;
    check(agent.fetch_me(me), "me loads");
    check(me.location == from, "pilot starts at the origin");
    std::string ship_name = agent.ship_name(ship);

    std::vector<kav::Job> jobs;
    check(agent.fetch_jobs(from, jobs) && !jobs.empty(), "job board has jobs");
    long id = 0, lightest = 1L << 30;
    for (auto &j : jobs)
        if (j.to == to && j.kind == "cargo" && j.qty < lightest)
            id = j.id, lightest = j.qty;
    check(id != 0, "a job to the destination exists");
    check(agent.accept(id, ship).empty(), "job accepted");
    check(agent.fetch_me(me) && me.jobs.size() == 1, "job is listed as mine");

    long now = 1000;
    game.state = {true, ship_name, true, from};
    agent.tick(now);
    check(agent.server_ok(), "heartbeat at the origin accepted");

    // flying: no location reported; the job is not complete
    game.state = {true, ship_name, false, ""};
    agent.tick(now += 5);

    // landed at the destination straight away: the server refuses (too quick) and the agent stays quiet
    game.state = {true, ship_name, true, to};
    auto notes = agent.tick(now += 5);
    check(notes.empty(), "too-quick completion is not reported as an error");
    std::printf("      waiting 21 s for the server's minimum flight time...\n");
    std::fflush(stdout);
    for (int i = 0; i < 25 && notes.empty(); ++i)
    {
        std::system("sleep 1");
        notes = agent.tick(now += 11);
    }
    check(notes.size() == 1 && notes[0].find("Delivered job") == 0, "delivery reported by the agent");
    check(!game.notified.empty(), "the game was told");
    check(agent.fetch_me(me) && me.jobs.empty() && me.location == to, "pilot moved and job cleared");
    return failures ? 1 : 0;
}
