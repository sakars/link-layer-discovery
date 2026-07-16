

#include "inplace_vector.hh"

#include <iostream>

#include "test_boilerplate.hh"

int pushBackTriviallyCopyableTest()
{
    InplaceVector<int, 5> vec{};
    for (int i = 0; i < 5; i++)
    {
        auto push_back_result = vec.TryPushBack(i);
        if (!push_back_result.has_value())
        {
            std::cerr << "Failed to push back\n";
            return -1;
        }
        if (push_back_result.value() != i)
        {
            std::cerr << "Index unexpected: " << i << " " << push_back_result.value() << "\n";
            return -1;
        }
    }

    for (int i = 0; i < 5; i++)
    {
        if (vec[i] != i)
        {
            std::cerr << "Failed to read item " << i;
            return -1;
        }
    }

    return 0;
}

int pushBackMoreThanCapacity()
{
    InplaceVector<int, 5> vec{};
    for (int i = 0; i < 5; i++)
    {
        auto push_back_result = vec.TryPushBack(i);
        if (!push_back_result.has_value())
        {
            std::cerr << "Failed to push back\n";
            return -1;
        }
        if (push_back_result.value() != i)
        {
            std::cerr << "Index unexpected: " << i << " " << push_back_result.value() << "\n";
            return -1;
        }
    }

    if (vec.TryPushBack(6).has_value())
    {
        std::cerr << "Somehow pushback 6 succeeded\n";
        return -1;
    }

    for (int i = 0; i < 5; i++)
    {
        if (vec[i] != i)
        {
            std::cerr << "Failed to read item " << i;
            return -1;
        }
    }
    return 0;
}

int popBackEmptyVec()
{
    InplaceVector<int, 5> vec{};
    vec.PopBack();
    return 0;
}

int moveEfficiency()
{
    Tracker::ClearLog();
    InplaceVector<Tracker, 5> vec{};
    Tracker tracker;

    vec.TryPushBack(std::move(tracker));

    if (tracker.stats->moves != 1)
    {
        std::cerr << Tracker::log.str();
        return -1;
    }
    return 0;
}

int copyEfficiency()
{
    Tracker::ClearLog();
    InplaceVector<Tracker, 5> vec{};
    Tracker tracker;

    vec.TryPushBack(tracker);

    if (tracker.stats->copies != 1)
    {
        std::cerr << Tracker::log.str();
        return -1;
    }
    return 0;
}

int vectorCopy()
{
    Tracker::ClearLog();
    Tracker a;
    Tracker b;
    InplaceVector<Tracker, 2> vec{};
    vec.TryPushBack(a);
    vec.TryPushBack(std::move(b));

    InplaceVector<Tracker, 2> vec2{vec};

    if (a.stats->copies != 2 || b.stats->copies != 1 || b.stats->moves != 1 || a.stats->deallocations != 0 || b.stats->deallocations != 0)
    {
        std::cerr << Tracker::log.str();
        return -1;
    }
    return 0;
}

int vectorMove()
{
    Tracker::ClearLog();
    Tracker a;
    Tracker b;
    InplaceVector<Tracker, 2> vec{};
    vec.TryPushBack(a);
    vec.TryPushBack(std::move(b));

    InplaceVector<Tracker, 2> vec2{std::move(vec)};

    if (b.stats->moves != 2 || a.stats->copies != 1 || a.stats->moves != 1 || a.stats->deallocations != 0 || b.stats->deallocations != 0)
    {
        std::cerr << Tracker::log.str();
        return -1;
    }
    return 0;
}

int main()
{
    TEST(pushBackTriviallyCopyableTest);
    TEST(pushBackMoreThanCapacity);
    TEST(popBackEmptyVec);
    TEST(moveEfficiency);
    TEST(copyEfficiency);
    TEST(vectorCopy);
    TEST(vectorMove);
}