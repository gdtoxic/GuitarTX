#include "Icons.h"

namespace Icons
{
namespace
{
    struct Shape
    {
        juce::Path stroke;   // контур
        juce::Path fill;     // залитые части
    };

    void line(juce::Path& p, std::initializer_list<juce::Point<float>> points)
    {
        auto it = points.begin();
        p.startNewSubPath(*it);

        for (++it; it != points.end(); ++it)
            p.lineTo(*it);
    }

    Shape build(Id icon)
    {
        using P = juce::Point<float>;
        constexpr float pi = juce::MathConstants<float>::pi;
        Shape s;

        switch (icon)
        {
            case Id::logo:
                line(s.stroke, { P(2, 12), P(6, 12), P(8, 6), P(12, 19), P(15, 9), P(17, 12), P(22, 12) });
                break;

            case Id::tuner:   // камертон
                s.stroke.startNewSubPath(9, 2);
                s.stroke.lineTo(9, 10);
                s.stroke.addCentredArc(12, 10, 3, 3, 0, 1.5f * pi, 0.5f * pi);
                s.stroke.lineTo(15, 2);
                line(s.stroke, { P(12, 13), P(12, 22) });
                break;

            case Id::midi:    // разъём DIN-5
                s.stroke.addEllipse(3, 3, 18, 18);
                for (const auto c : { P(7.5f, 11), P(16.5f, 11), P(12, 7.5f), P(9, 15), P(15, 15) })
                    s.fill.addEllipse(c.x - 1.3f, c.y - 1.3f, 2.6f, 2.6f);
                break;

            case Id::settings:   // микшерные фейдеры
                line(s.stroke, { P(4, 21), P(4, 14) });
                line(s.stroke, { P(4, 10), P(4, 3) });
                line(s.stroke, { P(12, 21), P(12, 12) });
                line(s.stroke, { P(12, 8), P(12, 3) });
                line(s.stroke, { P(20, 21), P(20, 16) });
                line(s.stroke, { P(20, 12), P(20, 3) });
                line(s.stroke, { P(1, 14), P(7, 14) });
                line(s.stroke, { P(9, 8), P(15, 8) });
                line(s.stroke, { P(17, 16), P(23, 16) });
                break;

            case Id::save:
                line(s.stroke, { P(5, 3), P(16, 3), P(19, 6), P(19, 21), P(5, 21) });
                s.stroke.closeSubPath();
                line(s.stroke, { P(8, 3), P(8, 8), P(15, 8) });
                line(s.stroke, { P(8, 21), P(8, 14), P(16, 14), P(16, 21) });
                break;

            case Id::browse:
                line(s.stroke, { P(4, 6), P(20, 6) });
                line(s.stroke, { P(4, 12), P(20, 12) });
                line(s.stroke, { P(4, 18), P(14, 18) });
                break;

            case Id::record:
                s.fill.addEllipse(5, 5, 14, 14);
                break;

            case Id::loop:
                line(s.stroke, { P(17, 2), P(21, 6), P(17, 10) });
                s.stroke.startNewSubPath(3, 11);
                s.stroke.lineTo(3, 9);
                s.stroke.quadraticTo(3, 6, 6, 6);
                s.stroke.lineTo(21, 6);
                line(s.stroke, { P(7, 22), P(3, 18), P(7, 14) });
                s.stroke.startNewSubPath(21, 13);
                s.stroke.lineTo(21, 15);
                s.stroke.quadraticTo(21, 18, 18, 18);
                s.stroke.lineTo(3, 18);
                break;

            case Id::play:
                s.fill.addTriangle(7, 4, 20, 12, 7, 20);
                break;

            case Id::stop:
                s.fill.addRoundedRectangle(6, 6, 12, 12, 1.5f);
                break;

            case Id::drums:   // барабан с палочками
                s.stroke.addEllipse(4, 6, 16, 6);
                line(s.stroke, { P(4, 9), P(4, 16) });
                line(s.stroke, { P(20, 9), P(20, 16) });
                s.stroke.startNewSubPath(4, 16);
                s.stroke.cubicTo(4, 19.5f, 20, 19.5f, 20, 16);
                line(s.stroke, { P(7, 2), P(11, 8) });
                line(s.stroke, { P(17, 2), P(13, 8) });
                break;

            case Id::undo:
                line(s.stroke, { P(9, 14), P(4, 9), P(9, 4) });
                s.stroke.startNewSubPath(4, 9);
                s.stroke.lineTo(14.5f, 9);
                s.stroke.cubicTo(21.5f, 9, 21.5f, 20, 14.5f, 20);
                s.stroke.lineTo(11, 20);
                break;

            case Id::more:
                line(s.stroke, { P(6, 9), P(12, 15), P(18, 9) });
                break;

            case Id::previous:
                line(s.stroke, { P(15, 5), P(8, 12), P(15, 19) });
                break;

            case Id::next:
                line(s.stroke, { P(9, 5), P(16, 12), P(9, 19) });
                break;

            case Id::redo:
                line(s.stroke, { P(15, 14), P(20, 9), P(15, 4) });
                s.stroke.startNewSubPath(20, 9);
                s.stroke.lineTo(9.5f, 9);
                s.stroke.cubicTo(2.5f, 9, 2.5f, 20, 9.5f, 20);
                s.stroke.lineTo(13, 20);
                break;

            case Id::none:
                break;
        }

        return s;
    }
}

void draw(juce::Graphics& g, Id icon, juce::Rectangle<float> area, juce::Colour colour, float strokeWidth)
{
    // Кэш форм: строятся один раз
    static const auto shapes = []
    {
        std::array<Shape, (size_t) numIcons> all;
        for (int i = 0; i < (int) all.size(); ++i)
            all[(size_t) i] = build((Id) i);
        return all;
    }();

    const auto& shape = shapes[(size_t) icon];
    const float size = juce::jmin(area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre(size, size);
    const auto transform = juce::AffineTransform::scale(size / 24.0f).translated(box.getX(), box.getY());

    g.setColour(colour);

    if (! shape.fill.isEmpty())
        g.fillPath(shape.fill, transform);

    if (! shape.stroke.isEmpty())
    {
        auto path = shape.stroke;
        path.applyTransform(transform);
        g.strokePath(path, { strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }
}
}
