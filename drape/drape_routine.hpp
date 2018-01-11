#pragma once

#include "base/assert.hpp"
#include "base/macros.hpp"
#include "base/worker_thread.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dp
{
// This class MUST NOT run OpenGL-related tasks (which invoke OpenGL or contain any
// OpenGL data), use FR/BR threads for that.
class DrapeRoutine
{
  friend class Promise;

public:
  class Result
  {
  public:
    void Wait()
    {
      if (m_isFinished)
        return;

      DrapeRoutine::Instance().Wait(m_id);
    }

  private:
    friend class DrapeRoutine;

    explicit Result(uint64_t id) : m_id(id), m_isFinished(false) {}

    uint64_t Finish()
    {
      m_isFinished = true;
      return m_id;
    }

    uint64_t const m_id;
    std::atomic<bool> m_isFinished;
  };

  using ResultPtr = std::shared_ptr<Result>;

  static void Init()
  {
    Instance();
  }

  static void Shutdown()
  {
    Instance().FinishAll();
  }

  template <typename Task>
  static ResultPtr Run(Task && t)
  {
    ResultPtr result(new Result(Instance().GetNextId()));
    bool const success = Instance().m_workerThread.Push([result, t]() mutable
    {
      t();
      Instance().Notify(result->Finish());
    });

    if (!success)
      return {};

    return result;
  }

private:
  static DrapeRoutine & Instance()
  {
    static DrapeRoutine instance;
    return instance;
  }

  DrapeRoutine() : m_workerThread(4 /* threads count */) {}

  uint64_t GetNextId()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_counter++;
  }

  void Notify(uint64_t id)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_finishedIds.insert(id);
    m_condition.notify_all();
  }

  void Wait(uint64_t id)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_finished)
      return;
    m_condition.wait(lock, [this, id]()
    {
      return m_finished || m_finishedIds.find(id) != m_finishedIds.end();
    });
    m_finishedIds.erase(id);
  }

  void FinishAll()
  {
    m_workerThread.ShutdownAndJoin();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_finished = true;
    m_condition.notify_all();
  }

  std::unordered_set<uint64_t> m_finishedIds;
  uint64_t m_counter = 0;
  bool m_finished = false;
  std::condition_variable m_condition;
  std::mutex m_mutex;
  base::WorkerThread m_workerThread;
};

// This is a helper class, which aggregates logic of waiting for active
// tasks completion. It must be used when we provide tasks completion
// before subsystem shutting down.
template <typename TaskType>
class ActiveTasks
{
  struct ActiveTask
  {
    std::shared_ptr<TaskType> m_task;
    DrapeRoutine::ResultPtr m_result;

    ActiveTask(std::shared_ptr<TaskType> const & task,
               DrapeRoutine::ResultPtr const & result)
      : m_task(task)
      , m_result(result)
    {}
  };

public:
  ~ActiveTasks()
  {
    FinishAll();
  }

  void Add(std::shared_ptr<TaskType> const & task,
           DrapeRoutine::ResultPtr const & result)
  {
    ASSERT(task != nullptr, ());
    ASSERT(result != nullptr, ());
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tasks.emplace_back(task, result);
  }

  void Remove(std::shared_ptr<TaskType> const & task)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tasks.erase(
        std::remove_if(m_tasks.begin(), m_tasks.end(),
                       [task](ActiveTask const & t) { return t.m_task == task; }),
        m_tasks.end());
  }

  void FinishAll()
  {
    // Move tasks to a temporary vector, because m_tasks
    // can be modified during 'Cancel' calls.
    std::vector<ActiveTask> tasks;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      tasks.swap(m_tasks);
    }

    // Cancel all tasks.
    for (auto & t : tasks)
      t.m_task->Cancel();

    // Wait for completion of unfinished tasks.
    for (auto & t : tasks)
      t.m_result->Wait();
  }

private:
  std::vector<ActiveTask> m_tasks;
  std::mutex m_mutex;
};
}  // namespace dp
