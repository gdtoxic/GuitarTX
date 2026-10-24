#pragma once

#include <JuceHeader.h>

// Общий темп приложения: драм-машина, Glitch в режиме Sync, Tap tempo.
// Один на процесс; читается из аудиопотока, меняется из message thread.
class GlobalTempo
{
public:
    static constexpr double minBpm = 40.0;
    static constexpr double maxBpm = 300.0;

    static GlobalTempo& get() noexcept
    {
        static GlobalTempo instance;
        return instance;
    }

    double getBpm() const noexcept { return bpm.load(std::memory_order_relaxed); }
    void   setBpm(double newBpm) noexcept { bpm.store(juce::jlimit(minBpm, maxBpm, newBpm), std::memory_order_relaxed); }

    // Длина четверти в сэмплах
    double getBeatSamples(double sampleRate) const noexcept { return 60.0 / getBpm() * sampleRate; }

private:
    GlobalTempo() = default;

    std::atomic<double> bpm { 120.0 };
};
