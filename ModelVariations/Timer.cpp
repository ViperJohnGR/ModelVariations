#include "Timer.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

std::unordered_map<Timer::TimerID, Timer::TimerData> Timer::s_Timers;
Timer::TimerID Timer::s_NextID = 1;


Timer::TimerID Timer::Add(
    Duration interval,
    Callback callback,
    LoopCount loopCount
)
{
    if (interval.count() <= 0)
        throw std::invalid_argument("Timer interval must be greater than 0.");

    if (!callback)
        throw std::invalid_argument("Timer callback cannot be empty.");

    if (loopCount != InfiniteLoops && loopCount <= 0)
        throw std::invalid_argument("Timer loop count must be positive or -1 for infinite.");

    const TimerID id = s_NextID++;

    TimerData timer;

    timer.interval = interval;
    timer.elapsed = Duration::zero();
    timer.loopCount = loopCount;
    timer.remainingLoops = loopCount;
    timer.callback = std::move(callback);
    timer.state = State::Stopped;
    timer.lastUpdate = std::chrono::steady_clock::now();

    s_Timers.emplace(id, std::move(timer));

    return id;
}


bool Timer::Start(TimerID id, bool runInstantly)
{
    auto it = s_Timers.find(id);

    if (it == s_Timers.end())
        return false;

    TimerData& timer = it->second;

    timer.elapsed = Duration::zero();
    timer.remainingLoops = timer.loopCount;
    timer.state = State::Running;
    timer.lastUpdate = std::chrono::steady_clock::now();

    if (runInstantly)
        Fire(id);

    return true;
}


bool Timer::Pause(TimerID id)
{
    auto it = s_Timers.find(id);

    if (it == s_Timers.end())
        return false;

    TimerData& timer = it->second;

    if (timer.state != State::Running)
        return false;

    const auto now = std::chrono::steady_clock::now();

    timer.elapsed += now - timer.lastUpdate;

    timer.lastUpdate = now;
    timer.state = State::Paused;

    return true;
}


bool Timer::Resume(TimerID id)
{
    auto it = s_Timers.find(id);

    if (it == s_Timers.end())
        return false;

    TimerData& timer = it->second;

    if (timer.state != State::Paused)
        return false;

    timer.lastUpdate = std::chrono::steady_clock::now();
    timer.state = State::Running;

    return true;
}


bool Timer::Stop(TimerID id)
{
    auto it = s_Timers.find(id);

    if (it == s_Timers.end())
        return false;

    TimerData& timer = it->second;

    timer.elapsed = Duration::zero();
    timer.remainingLoops = timer.loopCount;
    timer.state = State::Stopped;
    timer.lastUpdate = std::chrono::steady_clock::now();

    return true;
}


void Timer::Fire(TimerID id)
{
    auto it = s_Timers.find(id);

    if (it == s_Timers.end())
        return;

    TimerData& timer = it->second;

    if (timer.state != State::Running || !timer.callback)
        return;

    if (timer.remainingLoops > 0)
    {
        --timer.remainingLoops;

        if (timer.remainingLoops == 0)
        {
            // Stop before invoking the final callback so that any action taken
            // by that callback (such as restarting the timer) is preserved.
            timer.elapsed = Duration::zero();
            timer.state = State::Stopped;
        }
    }

    timer.callback();
}


void Timer::Process()
{
    const auto now = std::chrono::steady_clock::now();

    // Callbacks are executed after iterating over the map.
    //
    // This is important because a callback is allowed to call things like:
    //
    // Timer::Add(...)
    // Timer::Stop(...)
    // Timer::Pause(...)
    //
    // which could otherwise invalidate our unordered_map iterator.
    std::vector<TimerID> timersToFire;

    timersToFire.reserve(s_Timers.size());

    for (auto& [id, timer] : s_Timers)
    {
        if (timer.state != State::Running)
            continue;

        timer.elapsed += now - timer.lastUpdate;

        timer.lastUpdate = now;

        if (timer.elapsed >= timer.interval)
        {
            // Subtract rather than reset to zero.
            //
            // This prevents timer drift if Process() is a little late.
            timer.elapsed -= timer.interval;

            timersToFire.push_back(id);
        }
    }

    for (TimerID id : timersToFire)
    {
        auto it = s_Timers.find(id);

        if (it == s_Timers.end())
            continue;

        // Another callback may have stopped/paused this timer.
        if (it->second.state != State::Running)
            continue;

        Fire(id);
    }
}
