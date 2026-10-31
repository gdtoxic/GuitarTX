# GuitarTX

[![Build](https://github.com/gdtoxic/GuitarTX/actions/workflows/build.yml/badge.svg)](https://github.com/gdtoxic/GuitarTX/actions/workflows/build.yml)

Standalone гитарный процессор на **JUCE** (Windows). Цель — тяжёлый современный звук в духе Doom / Mick Gordon: плотный дисторшн, саб-808, хаос-шумы.

## Возможности

- **Главное окно** — компактное, фиксированного размера: сверху пресеты (категория, ‹ ›, Save, A/B; клик по имени — библиотека), тюнер, MIDI и настройки; под ними **полоса сцены** — входной уровень, запись, лупер (состояние, такты, Undo), барабаны (play, темп, паттерн), выходной уровень. Кнопки инструментов открывают и закрывают свои окна.
- **Перестраиваемая цепочка эффектов** — drag & drop, добавление/удаление на лету без щелчков (кроссфейд при перестройке графа).
- **Параллельная линия** — эффекты получают чистый сигнал и подмешиваются к основной цепочке (например, Sub 808 под гитарой).
- **Эффекты:**

| Эффект | Что делает |
|---|---|
| Noise Gate | Гейт для хай-гейна |
| Compressor | Педальный компрессор: threshold, ratio, attack / release, makeup, **Mix** (параллельная компрессия); индикатор подавления |
| Wah | Резонансный фильтр 350 Гц – 2.2 кГц: **Pedal** (Position под экспрессию через MIDI Learn), **Auto** (от силы удара), **LFO** |
| Overdrive | Буст / мягкий перегруз перед дисторшном |
| Distortion | Хай-гейн: 2 каскада клиппинга, 4x oversampling, пост-EQ; **модели усилителя**: Modern, American (5150 / Rectifier, сэг), British (JCM800), Doom Fuzz (Big Muff), Tight (djent); **оконечник**: Power (насыщение мощных ламп), Sag (просадка питания), Resonance (резонанс динамика) |
| Cabinet | Свёртка с IR кабинета (загрузка своих `.wav`, Clear — обратно на встроенную) или встроенные модели: 4x12 Modern / Vintage, 2x12 Open, 1x12 Combo; **второй IR (B)** с ручкой **Mix A/B**, **микрофон**: позиция (центр → край диффузора) и расстояние |
| Harmonizer | Два голоса **по ладу** (Key + Scale: мажор, минор, гармонический минор, дорийский, фригийский, миксолидийский): ±терция / кварта / квинта / секста / октава |
| Modulation | **Chorus / Flanger / Phaser / Tremolo**: rate, depth, feedback, mix, стерео-ширина |
| Tone Match | Подгонка тембра под эталонную запись (минимально-фазовый EQ, без задержки) |
| Sub 808 | Саб-бас по высоте гитары (YIN-трекинг, Sine / Square / Divider, Punch) |
| Chaos | Fuzz / ring mod / bitcrush / downsample / жёсткий гейт |
| Pitch | Октавер / whammy / pitch-drop: сдвиг ±24 полутона (и для аккордов), режимы Static / Dive / Rise; **LFO** высоты: Sine / Triangle / Square (трель) / Saw / Random / Random Steps, Retrig от атаки |
| Delay | Digital / Tape / Ping-Pong, до 2 с, tone и low cut в петле, модуляция, duck |
| Reverb | Room / Plate / Hall (FDN), decay 0.2–20 с, pre-delay, damping, low cut, duck |
| Glitch | **Stutter** (повтор доли, Ramp — разгон), **Glitch** (случайные повторы / реверс / дробление / октава по сетке, Chaos), **Freeze** (бесконечное удержание, играешь поверх); BPM + Tap или **Sync** к общему темпу, доли 1/2…1/64 и триоли |
| EQ | Параметрический: low cut, low shelf, 2 колокола, high shelf, high cut — с графиком АЧХ и 16 заводскими кривыми (стрелки `<` `>` или список) |

- **Master** — компрессор + lookahead-лимитер на выходе.
- **Tuner** — кнопка TUNER (Esc — закрыть): нота, стрелка в центах, частота. Точность < 1 цента от drop-A (27.5 Гц), глушит выход на время настройки, эталон A4 = 430…450 Гц.
- **MIDI** — кнопка MIDI: педаль экспрессии и любая ручка через MIDI Learn (ПКМ по ручке), футсвитч вкл/выкл педали (ПКМ по заголовку; **Toggle** — каждое нажатие переключает, **Hold** — включено, пока кнопка нажата, отпустил — выкл.; режим есть у педалей, снапшота B, тюнера, записи, барабанов и LISTEN — двойной клик по строке в окне MIDI или ПКМ по заголовку педали), Program Change → пресет, CC → следующий / предыдущий пресет, тюнер, запись, снапшот A/B, **Tap tempo** (общий темп и Glitch), **Drums Play / Stop**, **Drums LISTEN**, **Looper** (нажатие / двойное — стоп / удержание 2 с — очистка), **Looper stop / undo / clear**.
- **Драм-машина** — кнопка DRUMS (▶ рядом — Play / Stop): 8 дорожек (Kick, Snare, Closed / Open Hat, Low / High Tom, Crash, Metal), синтезированные киты **Doom / Metal / Industrial** или свой WAV на любой дорожке; секвенсор до 32 шагов, сетка 1/16, 1/8T, 1/16T, 1/32, акценты и ghost-ноты, swing; 14 заводских паттернов (Doom Half-Time, Doom March, Blast Beat, Djent Groove, Machine Gun, Industrial Stomp…) + свои (`%APPDATA%\GuitarTX\Patterns`). **Общий темп** 40–300 BPM с Tap; смена паттерна во время игры — с начала следующего такта. Барабаны идут мимо гитарной цепочки, попадают в выход и в Wet-запись (DI остаётся чистым).
  - **LISTEN** — сыграй рифф, замолчи: темп, сетка и бит под рифф выставятся сами (кик в унисон, снейр по стилю, томы в паузах). То же из файла (**File...**) или из последней DI-записи (**Last DI**). Каждый дубль сохраняется в `Документы\GuitarTX\Recordings\Listen` (последние 30). Распознаёт палм-мьюты, звенящие аккорды, хаммер-оны и пулл-оффы.
  - **FOLLOW** — барабаны вступают с первой нотой и подтягивают темп и фазу под игру.
  - **Generate** по стилю (Doom, Sludge, Thrash, Blast, Djent, Metalcore, Industrial, Nu-Metal, Rock) + Density, **Vary** — вариация текущего паттерна, **сбивки** каждые 2 / 4 / 8 повторов с крэшем.
- **Лупер** — кнопка LOOP: до 60 с, запись → игра → наложение, Stop, Undo / Redo последнего слоя, x2, Clear, Save WAV (`Документы\GuitarTX\Recordings\Loops`). С барабанами луп привязан к тактам (нажал чуть позже доли — запись всё равно с доли); без барабанов первый луп задаёт темп и запускает барабаны ровно со своим началом. Шов без щелчка.
- **Рекордер** — кнопка REC: два WAV одной длины — **DI** (сухая гитара, моно) и **Wet** (обработанный звук, стерео), 24-bit или 32-bit float, папка `Документы\GuitarTX\Recordings`. **Re-amp** (меню ▾): DI-файл играет вместо гитары через текущий пресет и записывается заново.
- **Snapshots A/B** — кнопки A / B рядом с пресетом: два набора ручек и On/Off для одной цепочки (ритм / соло), переключение мгновенное, сохраняются в пресете. ПКМ — скопировать в другой снапшот.
- **Браузер пресетов** — кнопка Browse: поиск по имени, стилю и тегам, избранное (★), папки, свои теги, импорт/экспорт `.gtxpreset`.
- **Пресеты** цепочек и настроек: 90 заводских в 11 категориях (Doom, Metal, Nu-Metal, Industrial, Extreme, Alt Rock, Clean & Crunch, **Top Picks**, **Showcase**, **Insane**, Utility) + пользовательские (`%APPDATA%\GuitarTX\Presets\*.gtxpreset`).
- Регуляторы Input / Output, индикаторы уровня, автосохранение сессии, **загрузка CPU** в строке статуса, **масштаб интерфейса** 80–125 % (кнопка настроек).

## Сборка

Готовый exe для каждого коммита собирает GitHub Actions (вкладка **Actions** → последний запуск → артефакт `GuitarTX-…-win64`). Релиз: **Releases → Draft a new release** → тег `vX.Y.Z` → **Publish** — через ~5 минут CI прикрепит к релизу zip и SHA-256.

### CMake (проще всего)

Нужны **Visual Studio** (2022 или новее, компоненты C++) и **CMake 3.22+**. JUCE скачается сам (версия закреплена в `CMakeLists.txt`):

```
cmake -B build -A x64
cmake --build build --config Release
```

Готовый файл: `build/GuitarTX_artefacts/Release/GuitarTX.exe`. Тесты: `-DGUITARTX_BUILD_TESTS=ON`, затем `ctest --test-dir build -C Release`. Свой JUCE вместо загрузки: `-DGUITARTX_JUCE_DIR="C:/Program Files/JUCE"`.

### Projucer

Требуется:
- **JUCE 9.0.2+** (проект использует модуль `juce_audio_processors_headless`); модули ожидаются в `C:/Program Files/JUCE/modules`, иначе поменяй путь в Projucer
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
CMakeLists.txt     CMake-сборка (CI и локально)
tests/             интеграционные тесты (DSP, тюнер, пресеты)
Resources/         иконка приложения (SVG-исходник, PNG, многоразмерный .ico)
packaging/         файлы для zip-релиза
.github/           GitHub Actions, шаблоны issues
CHANGELOG.md       история изменений
```

## Лицензия

GuitarTX — свободное ПО под **GNU AGPLv3** (см. [LICENSE.md](LICENSE.md)).
Можно бесплатно пользоваться, изучать, менять и распространять; производные работы тоже должны оставаться открытыми под AGPLv3.

Сторонние компоненты (JUCE и библиотеки внутри него) — см. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
