#pragma once

#include <JuceHeader.h>

// Оформление GuitarTX: ручки в стиле педалей и общие цвета.
//
// Ручка:
//  - шкала-дуга с рисками;
//  - оранжевая дуга значения (у симметричных диапазонов, например EQ -12..+12,
//    дуга идёт от центра);
//  - объёмный корпус с обводкой и яркий указатель.
class GuitarTXLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour accent     { 0xffff7828 };
    static inline const juce::Colour background { 0xff191b1e };
    static inline const juce::Colour panel      { 0xff2c3036 };
    static inline const juce::Colour track      { 0xff3d424a };
    static inline const juce::Colour textBox    { 0xff121417 };

    GuitarTXLookAndFeel()
    {
        setColour(juce::Slider::rotarySliderFillColourId,    accent);
        setColour(juce::Slider::rotarySliderOutlineColourId, track);
        setColour(juce::Slider::thumbColourId,               accent);
        setColour(juce::Slider::textBoxTextColourId,         juce::Colours::white);
        setColour(juce::Slider::textBoxBackgroundColourId,   textBox);
        setColour(juce::Slider::textBoxOutlineColourId,      track);
        setColour(juce::Slider::textBoxHighlightColourId,    accent.withAlpha(0.4f));
        setColour(juce::Label::textColourId,                 juce::Colours::white.withAlpha(0.9f));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto  centre = bounds.getCentre();

        const bool  enabled = slider.isEnabled();
        const bool  hover   = slider.isMouseOverOrDragging();
        const float alpha   = enabled ? 1.0f : 0.4f;

        const float angle   = startAngle + sliderPos * (endAngle - startAngle);
        const float arcW    = juce::jmax(3.0f, radius * 0.13f);
        const float arcR    = radius - arcW * 0.5f;

        // Шкала (фон дуги)
        juce::Path trackArc;
        trackArc.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);

        g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId).withMultipliedAlpha(alpha));
        g.strokePath(trackArc, { arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // Дуга значения: от минимума или от центра для симметричных диапазонов
        const bool bipolar = slider.getMinimum() < 0.0
                          && juce::approximatelyEqual(slider.getMinimum(), -slider.getMaximum());

        const float zeroAngle = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;

        if (! juce::approximatelyEqual(angle, zeroAngle))
        {
            juce::Path valueArc;
            valueArc.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f,
                                   juce::jmin(zeroAngle, angle), juce::jmax(zeroAngle, angle), true);

            auto fill = slider.findColour(juce::Slider::rotarySliderFillColourId);

            if (hover)
                fill = fill.brighter(0.25f);

            g.setColour(fill.withMultipliedAlpha(alpha));
            g.strokePath(valueArc, { arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        // Риски: 11 штук по шкале
        const float bodyR = radius - arcW * 2.2f;

        g.setColour(juce::Colours::white.withAlpha(0.25f * alpha));

        for (int i = 0; i <= 10; ++i)
        {
            const float a  = startAngle + (float) i / 10.0f * (endAngle - startAngle);
            const auto  p1 = centre.getPointOnCircumference(bodyR + arcW * 0.35f, a);
            const auto  p2 = centre.getPointOnCircumference(bodyR + arcW * (i % 5 == 0 ? 1.1f : 0.8f), a);
            g.drawLine({ p1, p2 }, i % 5 == 0 ? 1.6f : 1.0f);
        }

        // Корпус: тень, градиент, обводка
        const auto body = juce::Rectangle<float>(bodyR * 2.0f, bodyR * 2.0f).withCentre(centre);

        g.setColour(juce::Colours::black.withAlpha(0.45f * alpha));
        g.fillEllipse(body.translated(0.0f, bodyR * 0.08f).expanded(1.5f));

        juce::ColourGradient gradient(juce::Colour(0xff5a6069).withMultipliedAlpha(alpha),
                                      body.getX() + bodyR * 0.6f, body.getY() + bodyR * 0.4f,
                                      juce::Colour(0xff1c1f23).withMultipliedAlpha(alpha),
                                      body.getRight(), body.getBottom(), true);
        g.setGradientFill(gradient);
        g.fillEllipse(body);

        g.setColour(juce::Colour(0xff0b0c0e).withMultipliedAlpha(alpha));
        g.drawEllipse(body, 1.6f);

        g.setColour(juce::Colours::white.withAlpha((hover ? 0.28f : 0.14f) * alpha));
        g.drawEllipse(body.reduced(2.0f), 1.0f);

        // Указатель
        const auto tip  = centre.getPointOnCircumference(bodyR * 0.82f, angle);
        const auto base = centre.getPointOnCircumference(bodyR * 0.25f, angle);

        g.setColour(juce::Colours::black.withAlpha(0.5f * alpha));
        g.drawLine({ base.translated(0.0f, 1.0f), tip.translated(0.0f, 1.0f) }, juce::jmax(2.5f, bodyR * 0.14f));

        g.setColour(juce::Colours::white.withAlpha(alpha));
        g.drawLine({ base, tip }, juce::jmax(2.0f, bodyR * 0.11f));

        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId).withMultipliedAlpha(alpha));
        g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre(tip));
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = LookAndFeel_V4::createSliderTextBox(slider);
        label->setFont(juce::FontOptions(13.0f, juce::Font::bold));
        return label;
    }
};
