#include "scheduler.hh"

namespace ndisc
{

    void Scheduler::Tick()
    {
        std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
        while (!task_heap_.empty() && task_heap_.front().trigger < now)
        {
            std::pop_heap(task_heap_.begin(), task_heap_.end(), std::greater<>());
            Task triggered_task = std::move(task_heap_.back());
            task_heap_.pop_back();
            if (!task_counts_.contains(triggered_task.id) || task_counts_[triggered_task.id] == 0)
            {
                task_counts_[triggered_task.id] = 1;
            }
            task_counts_[triggered_task.id]--;
            triggered_task.task(*this);
        }
    }

    void Scheduler::AddTask(Task task)
    {
        task_counts_[task.id]++;
        task_heap_.emplace_back(std::move(task));
        std::push_heap(task_heap_.begin(), task_heap_.end(), std::greater<>());
    }

    bool Scheduler::Empty() const
    {
        return task_heap_.empty();
    }

    std::optional<std::chrono::milliseconds> Scheduler::TimeToWait() const
    {
        if (task_heap_.empty())
        {
            return std::nullopt;
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(task_heap_.front().trigger - std::chrono::steady_clock::now());
    }

    bool Scheduler::HasTask(TaskId task_id) const
    {
        return task_counts_.contains(task_id) && task_counts_.at(task_id) > 0;
    }
} // namespace ndisc