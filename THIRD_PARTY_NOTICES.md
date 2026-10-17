# Third-party software

GuitarTX is built with the **JUCE** framework, used here under the **AGPLv3**
(JUCE is dual-licensed: AGPLv3 or the commercial JUCE licence).
https://github.com/juce-framework/JUCE

The JUCE modules used by GuitarTX bundle the following third-party libraries.
Their full licence texts are shipped inside the JUCE source tree
(see `JUCE.spdx.json` and each library's folder under `modules/`).

| Library | Licence | Used for |
|---|---|---|
| zlib | Zlib | compression (juce_core) |
| FLAC | BSD-3-Clause | FLAC reading (juce_audio_formats) |
| libpng | libpng-2.0 | PNG images (juce_graphics) |
| IJG libjpeg | IJG | JPEG images (juce_graphics) |
| HarfBuzz | MIT-Modern-Variant | text shaping (juce_graphics) |
| SheenBidi | Apache-2.0 | bidirectional text (juce_graphics) |
| LunaSVG / PlutoVG | MIT | SVG rendering (juce_graphics) |
| Steinberg ASIO SDK | GPL-3.0-only (or Steinberg licence) | ASIO audio driver support (juce_audio_devices) |

ASIO is a trademark and software of Steinberg Media Technologies GmbH.
