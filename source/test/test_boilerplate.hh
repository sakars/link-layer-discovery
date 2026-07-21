#ifndef TEST_BOILERPLATE_HH
#define TEST_BOILERPLATE_HH

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#define TEST(fn) test(fn, #fn)

void test(std::function<int()> &&testable, const std::string &name)
{
    if (testable() != 0)
    {
        std::cerr << "Test " << name << " failed\n";
    }
    else
    {
        std::cout << "Test " << name << " passed\n";
    }
}

class Tracker
{
    static int tracker_id;

public:
    static std::ostringstream log;

    struct Stats
    {
        int copies = 0;
        int moves = 0;
        int allocations = 0;
        int deallocations = 0;
        int id = 0;
    };

    std::shared_ptr<Stats> stats;

    Tracker()
    {
        stats = std::make_shared<Stats>();
        stats->id = tracker_id++;
        stats->allocations++;
        log << stats->id << " allocated.\n";
    }

    ~Tracker()
    {
        stats->deallocations++;
        log << stats->id << " deallocated.\n";
    }

    Tracker(const Tracker &other) : stats(other.stats)
    {
        stats->copies++;
        log << stats->id << " copied.\n";
    }

    Tracker(Tracker &&other) : stats(other.stats)
    {
        stats->moves++;
        log << stats->id << " moved.\n";
    }

    Tracker &operator=(const Tracker &other)
    {
        stats = other.stats;
        log << stats->id << " copy-assigned.\n";
        stats->copies++;
        return *this;
    }

    Tracker &operator=(Tracker &&other)
    {
        stats = other.stats;
        log << stats->id << " copy-assigned.\n";
        stats->moves++;
        return *this;
    }

    static void ClearLog()
    {
        tracker_id = 0;
        log.str("");
    }
};

int Tracker::tracker_id = 0;
std::ostringstream Tracker::log{};

#endif // TEST_BOILERPLATE_HH