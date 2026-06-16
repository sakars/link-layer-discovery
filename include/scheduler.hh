#ifndef SCHEDULER_HH
#define SCHEDULER_HH

#include <chrono>
#include <functional>
#include <map>

namespace ndisc
{

    class Scheduler;

    enum TaskId : uint8_t
    {
        TASKID_UNSPEC = 0,
        TASKID_DEVICE_FETCH,
        TASKID_ADDRESS_FETCH,
        TASKID_HEARTBEAT,
        TASKID_ADVERTISE,
        TASKID_NEIGHBOUR_DISCOVERY,

    };

    struct Task
    {
        TaskId id;
        std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds> trigger;
        std::function<void(Scheduler &)> task;

        bool operator<(Task &other) const { return trigger < other.trigger; }
        bool operator>(Task &other) const { return trigger > other.trigger; }
    };

    class Scheduler
    {
        std::vector<Task> task_heap_;
        std::map<TaskId, unsigned int> task_counts_;

    public:
        void Tick();

        void AddTask(Task);

        bool Empty() const;

        std::optional<std::chrono::milliseconds> TimeToWait() const;

        bool HasTask(TaskId task_id) const;
    };
} // namespace ndisc

#endif