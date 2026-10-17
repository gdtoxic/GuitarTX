#pragma once

#include "EffectBase.h"
#include "SpectrumTools.h"

// Tone Match: подгоняет тембр под эталонную запись.
//
//  1. Load reference - анализ файла-эталона (лучше кусок, где гитара звучит одна)
//  2. Learn my tone  - играешь ~10 с, педаль запоминает спектр СВОЕГО звука на её входе
//  3. Коррекция = эталон - мой звук -> эквалайзер минимальной фазы (без задержки)
//
// Ставится в конец основной цепочки (после кабинета).
// Обе кривые сохраняются в пресете, сам файл-эталон потом не нужен.
//
//  amount    - сила коррекции (%)
//  smoothing - насколько грубо повторять кривую (точно / средне / широко)
//  level     - выходная громкость (дБ)
class ToneMatchProcessor : public EffectBase,
                           private juce::Timer
{
public:
    static constexpr const char* typeId = "toneMatch";
    static constexpr double learnSeconds = 10.0;

    ToneMatchProcessor();
    ~ToneMatchProcessor() override;

    const juce::String getName() const override { return "Tone Match"; }
    juce::String getTypeId() const override     { return typeId; }
    juce::String getStatusText() const override;

    // --- Message thread
    void setReference(const SpectrumTools::Curve& curve, const juce::String& name);
    void startLearning();
    void cancelLearning();
    void clearMatch();

    bool  isLearning() const noexcept       { return captureState.load() == CaptureState::capturing; }
    float getLearnProgress() const noexcept { return learnProgress.load(); }

    const SpectrumTools::Curve& getReferenceCurve() const noexcept  { return referenceCurve; }
    const SpectrumTools::Curve& getMineCurve() const noexcept       { return mineCurve; }
    const SpectrumTools::Curve& getCorrectionCurve() const noexcept { return correctionCurve; }
    juce::String getReferenceName() const { return referenceName; }

    // Растёт при каждом изменении кривых - чтобы UI знал, когда перерисовать
    int getCurveVersion() const noexcept { return curveVersion; }

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;
    void stateRestored() override;

private:
    enum class CaptureState { idle, capturing, finished };

    static Layout createLayout();

    void timerCallback() override;
    void rebuildCorrection();
    void storeCurvesInState();
    float getSmoothingOctaves() const;

    std::atomic<float>& amountPercent;
    std::atomic<float>& smoothingChoice;
    std::atomic<float>& levelDb;

    // Message thread
    SpectrumTools::Curve referenceCurve, mineCurve, correctionCurve;
    juce::String referenceName;
    int   curveVersion   = 0;
    float lastAmount     = -1.0f;
    float lastSmoothing  = -1.0f;
    double irSampleRate  = 48000.0;

    // Обучение: аудиопоток копит спектр входа, message thread забирает результат
    std::atomic<CaptureState> captureState { CaptureState::idle };
    std::atomic<float> learnProgress { 0.0f };
    SpectrumTools::Accumulator learnAccumulator;
    std::vector<float> learnFifo;
    int    learnFifoFill = 0;
    double learnedSeconds = 0.0;

    // Аудиопоток
    juce::dsp::Convolution convolution;
    std::atomic<bool> correctionReady { false };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputLevel;
    double sampleRate   = 48000.0;
    int    maxBlockSize = 512;
};
