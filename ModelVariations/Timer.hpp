#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_map>

class Timer
{
public:
    using TimerID = std::uint32_t;
    using LoopCount = std::int32_t;
    using Callback = std::function<void()>;
    using Duration = std::chrono::milliseconds;

    static constexpr LoopCount InfiniteLoops = -1;

    // Creates a timer but does not start it.
    // loopCount is the number of callback executions; -1 means infinite.
    // Returns the timer's unique ID.
    static TimerID Add(Duration interval, Callback callback, LoopCount loopCount = InfiniteLoops);

    // Starts/restarts the timer from 0.
    //
    // runInstantly == true:
    //      callback runs immediately, then every interval. The immediate
    //      callback counts as one loop.
    //
    // runInstantly == false:
    //      waits one full interval before first callback.
    static bool Start(TimerID id, bool runInstantly = false);

    // Pauses at the current timer position.
    static bool Pause(TimerID id);

    // Continues from the paused position.
    static bool Resume(TimerID id);

    // Stops the timer and resets its position to 0.
    static bool Stop(TimerID id);

    // Call once per main/game/application loop.
    static void Process();

private:
    enum class State
    {
        Stopped,
        Running,
        Paused
    };

    struct TimerData
    {
        Duration interval{ 0 };
        std::chrono::steady_clock::duration elapsed{};

        LoopCount loopCount = InfiniteLoops;
        LoopCount remainingLoops = InfiniteLoops;

        Callback callback;

        State state = State::Stopped;

        std::chrono::steady_clock::time_point lastUpdate;
    };

    static void Fire(TimerID id);

private:
    static std::unordered_map<TimerID, TimerData> s_Timers;
    static TimerID s_NextID;
};
