/*****************************************************************/ /**
                                                                     * \file   TaskScheduler.hpp
                                                                     * \brief  Threading utility for calling and
                                                                     *executing AEGP on the main thread, while using
                                                                     *multiple threads.
                                                                     *
                                                                     * \author tjerf
                                                                     * \date   March 2024
                                                                     *********************************************************************/
#ifndef TASK_SCHEDULER_HPP
#define TASK_SCHEDULER_HPP

#include "AETK/Common/Common.hpp"

/**
 * @class TaskScheduler
 * @brief Manages the scheduling and execution of tasks within an Adobe After Effects plugin.
 *
 * This class implements the singleton design pattern to ensure there is a single, globally accessible
 * instance managing task execution throughout the plugin's lifecycle.
 *
 * TaskScheduler allows for tasks to be scheduled from any thread but ensures they are executed on the main thread,
 * leveraging Adobe After Effects' Idle Hook for task execution. This approach facilitates performing tasks that require
 * the main thread, such as UI updates or AE SDK operations, even when initiated from background threads.
 *
 * @note Important Usage Guidelines:
 * - Avoid using TaskScheduler within AE's Command Hook or other Hooks to schedule tasks with CALLIDLE set to True.
 *   Doing so can lead to issues like unresponsiveness or crashes due to calling Idle Routines while a Hook is still
 * active.
 * - When CALLIDLE is set to False, TaskScheduler can safely be used within any Hook. This defers task execution until
 *   the next idle cycle without explicitly invoking idle routines.
 * - For tasks initiated from separate threads (e.g., a listener thread for external commands), setting CALLIDLE to True
 * is recommended. This allows the plugin to process tasks promptly while maintaining responsiveness.
 *
 * Usage Example:
 * ```
 * // In a listener thread waiting for external commands
 * void externalCommandReceived() {
 *     // Schedule a task for execution on the main thread
 *     TaskScheduler::GetInstance().ScheduleTask([]() {
 *         // Task to be executed on the main thread
 *     }, true); // CALLIDLE is True, enabling immediate idle processing
 * }
 * ```
 *
 * In this example, an external command triggers `externalCommandReceived`, which schedules a task. The TaskScheduler,
 * upon the next idle cycle, executes this task on the main thread, ensuring compatibility with AE SDK requirements and
 * maintaining plugin responsiveness.
 */
namespace ae
{

class TaskScheduler
{
  public:
    /**
     * @brief Gets the singleton instance of the TaskScheduler.
     * @return TaskScheduler& The singleton instance.
     */
    static TaskScheduler &GetInstance()
    {
        static TaskScheduler instance;
        return instance;
    }

    /**
     * @brief Schedules a task with no return value.
     * @param task The task to be scheduled.
     * @param callIdle Flag indicating whether to call idle routines for quicker
     * response.
     */
    inline void ScheduleTask(std::function<void()> task, bool callIdle = true)
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        tasksQueue.push(std::move(task));
        if (callIdle)
        {
            SuiteManager::GetInstance().GetSuiteHandler().UtilitySuite6()->AEGP_CauseIdleRoutinesToBeCalled();
        }
    }

    /**
     * @brief Schedules a task with a return value.
     * @tparam ReturnType The return type of the task.
     * @param task The task to be scheduled.
     * @param callIdle Flag indicating whether to call idle routines for quicker
     * response.
     * @return std::future<ReturnType> A future object representing the result
     * of the task.
     */
    template <typename ReturnType>
    std::future<ReturnType> ScheduleTask(std::function<ReturnType()> task, bool callIdle = true)
    {
        auto promise = std::make_shared<std::promise<ReturnType>>();
        auto future = promise->get_future();

        std::function<void()> taskWrapper = [promise, task]() {
            try
            {
                promise->set_value(task());
            }
            catch (...)
            {
                promise->set_exception(std::current_exception());
            }
        };

        ScheduleTask(taskWrapper, callIdle); // Schedule the wrapped task
        return future;
    }


    /**
     * @brief Executes the next scheduled task.
     */
    inline void ExecuteTask()
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (!tasksQueue.empty())
        {
            auto task = tasksQueue.front();
            tasksQueue.pop();
            task(); // Execute the task
        }
    }

  private:
    std::mutex queueMutex;
    std::queue<std::function<void()>> tasksQueue;
};
} // namespace ae
/**
 * @brief Schedules a task with a return value.
 * @tparam ReturnType The return type of the task.
 * @param task The task to be scheduled.
 * @param callIdle Flag indicating whether to call idle routines for quicker
 * response.
 * @return std::future<ReturnType> A future object representing the result of
 * the task.
 */
template <typename ReturnType>
inline std::future<ReturnType> ScheduleTask(std::function<ReturnType()> task, bool callIdle = true)
{
    return ae::TaskScheduler::GetInstance().ScheduleTask(task, callIdle);
}

/**
 * @brief Schedules a task with no return value.
 * @param task The task to be scheduled.
 * @param callIdle Flag indicating whether to call idle routines for quicker
 * response.
 */
inline void ScheduleTask(std::function<void()> task, bool callIdle = true)
{
    ae::TaskScheduler::GetInstance().ScheduleTask(task, callIdle);
}

namespace ae
{

template <typename Func> auto ScheduleOrExecute(Func &&func)
{
    using ReturnType = typename std::invoke_result<Func>::type;

    auto taskPtr = std::make_shared<std::packaged_task<ReturnType()>>(std::forward<Func>(func));
    auto future = taskPtr->get_future();

#ifdef TK_INTERNAL
    TaskScheduler::GetInstance().ScheduleTask([taskPtr]() {
        try
        {
            (*taskPtr)();
        }
        catch (...)
        {
            // Log the exception here if needed
        }
    });
#else
    try
    {
        (*taskPtr)();
    }
    catch (...)
    {
        // Log or handle the immediate exception if needed
    }
#endif

    return future;
}

} // namespace ae

#endif // TASK_SCHEDULER_HPP