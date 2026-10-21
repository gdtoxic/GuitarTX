#pragma once

#include "EffectBase.h"
#include "Biquad.h"

// Reverb: сеть задержек с обратной связью (FDN, 8 линий, матрица Адамара).
//
//   вход -> Low Cut -> pre-delay -> диффузия (4 allpass) -> FDN [damping в каждой линии] -> стерео-выход
//        -> Duck (тише, пока играешь) -> mix с сухим
//
//  mode     - Room:  короткие линии, плотное раннее отражение
//             Plate: очень плотный, яркий, быстро набирается
//             Hall:  длинные линии, большой объём
//  decay    - время затухания до -60 дБ (RT60, секунды)
//  predelay - пауза перед хвостом (мс): гитара остаётся разборчивой
//  size     - размер помещения (%) - масштаб длин линий
//  damping  - срез верха в хвосте: выше = ярче
//  lowcut   - срез низа на входе ревербератора: хвост не гудит
//  mod      - медленная модуляция длин: без "металлического" звона
//  mix      - громкость хвоста относительно сухого (%)
//  duck     - на сколько приглушать хвост, пока звучит гитара (дБ)
class ReverbProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "reverb";

    ReverbProcessor();

    const juce::String getName() const override { return "Reverb"; }
    juce::String getTypeId() const override     { return typeId; }

    double getTailLengthSeconds() const override { return 20.0; }

protected:
    bool hasSpillover() const override { return true; }   // хвост доигрывает после выключения

    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    enum class Mode { room = 0, plate = 1, hall = 2 };

    static constexpr int numLines     = 8;
    static constexpr int numDiffusers = 4;

    static Layout createLayout();

    void updateTargets(bool jump);

    struct Line
    {
        std::vector<float> data;
        int    mask        = 0;
        double length      = 1000.0;   // текущая длина (сэмплы), плавно идёт к targetLength
        double targetLength = 1000.0;
        float  damped      = 0.0f;     // состояние ФНЧ damping
        float  gain        = 0.9f;     // затухание за один проход (из RT60)
        double lfoPhase    = 0.0;
        double lfoStep     = 0.0;
    };

    struct Allpass
    {
        std::vector<float> data;
        int   length = 1, index = 0;
        float feedback = 0.7f;

        float process(float x) noexcept
        {
            const float delayed = data[(size_t) index];
            const float v = x + feedback * delayed;
            data[(size_t) index] = v;
            index = (index + 1 == length) ? 0 : index + 1;
            return delayed - feedback * v;
        }
    };

    std::atomic<float>& modeChoice;
    std::atomic<float>& decaySeconds;
    std::atomic<float>& predelayMs;
    std::atomic<float>& sizePercent;
    std::atomic<float>& dampingHz;
    std::atomic<float>& lowCutHz;
    std::atomic<float>& modPercent;
    std::atomic<float>& mixPercent;
    std::atomic<float>& duckDb;

    std::array<Line, numLines> lines;
    std::uint32_t writeCounter = 0;
    std::array<Allpass, numDiffusers> diffusers;

    std::vector<float> predelayBuffer;
    int    predelayMask = 0, predelayWrite = 0;
    double predelaySamples = 0.0, predelayTarget = 0.0;

    Biquad lowCut;
    float  lastLowCut = -1.0f;
    int    lastMode   = -1;

    float dampingCoef  = 0.5f;
    float modDepth     = 0.0f;    // сэмплы
    double lengthSmoothing = 0.0005;

    float duckEnvelope = 0.0f, duckAttack = 0.0f, duckRelease = 0.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;

    double sampleRate = 48000.0;
};
