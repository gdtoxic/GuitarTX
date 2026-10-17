# GuitarTX

Standalone гитарный процессор на **JUCE** (Windows). Цель — тяжёлый современный звук в духе Doom / Mick Gordon: плотный дисторшн, саб-808, хаос-шумы.

## Возможности

- **Перестраиваемая цепочка эффектов** — drag & drop, добавление/удаление на лету без щелчков (кроссфейд при перестройке графа).
- **Параллельная линия** — эффекты получают чистый сигнал и подмешиваются к основной цепочке (например, Sub 808 под гитарой).
- **Эффекты:**

| Эффект | Что делает |
|---|---|
| Noise Gate | Гейт для хай-гейна |
| Overdrive | Буст / мягкий перегруз перед дисторшном |
| Distortion | Тугой хай-гейн: 2 каскада клиппинга, 4x oversampling, пост-EQ |
| Cabinet | Свёртка с IR кабинета (загрузка своих `.wav`) |
| Tone Match | Подгонка тембра под эталонную запись (минимально-фазовый EQ, без задержки) |
| Sub 808 | Саб-бас по высоте гитары (YIN-трекинг, Sine / Square / Divider, Punch) |
| Chaos | Fuzz / ring mod / bitcrush / downsample / жёсткий гейт |

- **Master** — компрессор + lookahead-лимитер на выходе.
- **Пресеты** цепочек и настроек: заводские + пользовательские (`%APPDATA%\GuitarTX\Presets\*.gtxpreset`).
- Регуляторы Input / Output, индикаторы уровня, автосохранение сессии.

## Сборка

Требуется:
- **JUCE** свежей версии (проект использует модуль `juce_audio_processors_headless`); модули ожидаются в `C:/Program Files/JUCE/modules`, иначе поменяй путь в Projucer
- **Visual Studio 2026** (C++17)

Шаги:
1. Открой `GuitarTX.jucer` в Projucer → **Save Project** (пересоздаёт `Builds/` и `JuceLibraryCode/` под твой путь к JUCE).
2. Открой `Builds/VisualStudio2026/GuitarTX.sln`.
3. Конфигурация **Release | x64** → Build.

## Структура

```
Source/            исходники (движок, эффекты, UI)
JuceLibraryCode/   сгенерировано Projucer
Builds/            проект Visual Studio 2026
GuitarTX.jucer     проект Projucer
```

## Лицензия

GuitarTX — свободное ПО под **GNU AGPLv3** (см. [LICENSE.md](LICENSE.md)).
Можно бесплатно пользоваться, изучать, менять и распространять; производные работы тоже должны оставаться открытыми под AGPLv3.

Сторонние компоненты (JUCE и библиотеки внутри него) — см. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
