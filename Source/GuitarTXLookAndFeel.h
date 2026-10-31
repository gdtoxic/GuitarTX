#pragma once

#include <JuceHeader.h>
#include "Icons.h"

// Оформление GuitarTX: ручки в стиле педалей, кнопки / списки / переключатели / полосы-слайдеры
// в одном стиле со шапкой, общие цвета и карточки разделов (drawSection).
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
    static inline const juce::Colour buttonFill { 0xff2f3a3c };
    static inline const juce::Colour border     { 0xff4f5e60 };
    static inline const juce::Colour text       { 0xffe6e6e6 };
    static inline const juce::Colour mutedText  { 0xff9aa0a6 };
    static inline const juce::Colour card       { 0xff15171a };
    static inline const juce::Colour cardLine   { 0xff3b4045 };
    static inline const juce::Colour recordRed  { 0xffe04545 };

    // Карточка раздела: тёмный фон, рамка, заголовок мелкими прописными сверху слева.
    // Возвращает область содержимого (под заголовком)
    static juce::Rectangle<int> drawSection(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                            juce::Colour titleColour = mutedText)
    {
        const auto bounds = area.toFloat();
        g.setColour(card);
        g.fillRoundedRectangle(bounds, 10.0f);
        g.setColour(cardLine);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);

        auto content = area.reduced(12, 8);

        if (title.isNotEmpty())
        {
            g.setColour(titleColour);
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(title.toUpperCase(), content.removeFromTop(14), juce::Justification::centredLeft, false);
            content.removeFromTop(4);
        }

        return content;
    }

    // То же без рисования: где окажется содержимое карточки
    static juce::Rectangle<int> sectionContent(juce::Rectangle<int> area, bool hasTitle = true)
    {
        auto content = area.reduced(12, 8);

        if (hasTitle)
            content.removeFromTop(18);

        return content;
    }

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

        setColour(juce::TextButton::buttonColourId,          buttonFill);
        setColour(juce::TextButton::buttonOnColourId,        accent);
        setColour(juce::TextButton::textColourOffId,         text);
        setColour(juce::TextButton::textColourOnId,          juce::Colours::white);
        setColour(juce::ComboBox::backgroundColourId,        textBox);
        setColour(juce::ComboBox::outlineColourId,           border);
        setColour(juce::ComboBox::textColourId,              text);
        setColour(juce::ComboBox::arrowColourId,             mutedText);
        setColour(juce::ToggleButton::textColourId,          text);
        setColour(juce::ToggleButton::tickColourId,          background);
        setColour(juce::ToggleButton::tickDisabledColourId,  mutedText);
        setColour(juce::ListBox::backgroundColourId,         textBox);
        setColour(juce::PopupMenu::backgroundColourId,       juce::Colour(0xff22262a));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.85f));
        setColour(juce::TooltipWindow::backgroundColourId,   juce::Colour(0xff22262a));
        setColour(juce::TooltipWindow::outlineColourId,      border);
    }

    //==============================================================================
    // Кнопки: скругление 6, рамка; включённая - заливка buttonOnColourId
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const float radius = 6.0f;
        const bool on = button.getToggleState();

        auto fill = backgroundColour;

        if (highlighted && ! on) fill = fill.brighter(0.12f);
        if (down)                fill = fill.brighter(0.2f);
        if (! button.isEnabled()) fill = fill.withMultipliedAlpha(0.45f);

        const bool flatLeft  = button.isConnectedOnLeft();
        const bool flatRight = button.isConnectedOnRight();

        juce::Path shape;
        shape.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                                  radius, radius, ! flatLeft, ! flatRight, ! flatLeft, ! flatRight);

        g.setColour(fill);
        g.fillPath(shape);
        g.setColour((on ? fill.brighter(0.25f) : border).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
        g.strokePath(shape, juce::PathStrokeType(1.0f));
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return juce::Font(juce::FontOptions(juce::jmin(14.0f, (float) buttonHeight * 0.55f)));
    }

    // Выпадающий список: тёмное поле, рамка, стрелка-шеврон
    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height).reduced(0.5f);
        const bool hover = box.isMouseOver(true);

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId).brighter(hover ? 0.06f : 0.0f));
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(box.hasKeyboardFocus(true) ? accent.withAlpha(0.8f) : box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

        const auto arrow = juce::Rectangle<float>((float) width - 22.0f, 0.0f, 14.0f, (float) height);
        Icons::draw(g, Icons::Id::more, arrow.withSizeKeepingCentre(13.0f, 13.0f),
                    box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.4f), 1.6f);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(4, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

    // Флажок: скруглённый квадрат, включённый - оранжевый с галочкой
    void drawTickBox(juce::Graphics& g, juce::Component& component, float x, float y, float w, float h,
                     bool ticked, bool isEnabled, bool highlighted, bool) override
    {
        const auto box = juce::Rectangle<float>(x, y, w, h).reduced(1.0f);
        const float alpha = isEnabled ? 1.0f : 0.45f;

        g.setColour((ticked ? accent : textBox.brighter(highlighted ? 0.1f : 0.0f)).withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour((ticked ? accent : border).withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(box, 4.0f, 1.0f);

        if (ticked)
        {
            juce::Path tick;
            tick.startNewSubPath(box.getX() + box.getWidth() * 0.24f, box.getCentreY());
            tick.lineTo(box.getX() + box.getWidth() * 0.43f, box.getBottom() - box.getHeight() * 0.27f);
            tick.lineTo(box.getRight() - box.getWidth() * 0.22f, box.getY() + box.getHeight() * 0.28f);
            g.setColour(component.findColour(juce::ToggleButton::tickColourId));
            g.strokePath(tick, { 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }
    }

    // Полоса-слайдер (LinearBar): тёмное поле, заливка значения акцентом
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearBar)
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(0.5f);
        const bool hover = slider.isMouseOverOrDragging();

        g.setColour(textBox);
        g.fillRoundedRectangle(bounds, 6.0f);

        const float filled = juce::jlimit(0.0f, bounds.getWidth(), sliderPos - bounds.getX());
        g.setColour(accent.withAlpha(hover ? 0.42f : 0.32f).withMultipliedAlpha(slider.isEnabled() ? 1.0f : 0.4f));
        g.fillRoundedRectangle(bounds.withWidth(filled), 6.0f);

        g.setColour(hover ? accent.withAlpha(0.7f) : border);
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
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

        // Полоса рисуется сама (drawLinearSlider): у текста нет своей рамки и фона
        if (slider.getSliderStyle() == juce::Slider::LinearBar)
        {
            label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
            label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
            label->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        }
        return label;
    }
};
