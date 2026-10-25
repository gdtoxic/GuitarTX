# CMake-сборка GuitarTX (используется в GitHub Actions; локально можно собирать и через Projucer).
#
#   cmake -B build -A x64                     # JUCE скачается автоматически (тег ниже)
#   cmake -B build -A x64 -DGUITARTX_JUCE_DIR="C:/Program Files/JUCE"   # или свой JUCE
#   cmake --build build --config Release
#
# Готовый exe: build/GuitarTX_artefacts/Release/GuitarTX.exe

cmake_minimum_required(VERSION 3.22)

project(GuitarTX VERSION 0.4.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Статический CRT: exe работает без установки VC++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(GUITARTX_JUCE_TAG "9.0.2" CACHE STRING "Версия JUCE для автозагрузки")
set(GUITARTX_JUCE_DIR "" CACHE PATH "Папка с локальным JUCE (пусто = скачать)")
option(GUITARTX_BUILD_TESTS "Собрать интеграционные тесты (GuitarTXTests)" OFF)

if(GUITARTX_JUCE_DIR)
    add_subdirectory("${GUITARTX_JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${GUITARTX_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(GuitarTX
    PRODUCT_NAME "GuitarTX"
    COMPANY_NAME "GuitarTX"
    VERSION      ${PROJECT_VERSION})

# JuceHeader.h с ProjectInfo:: генерируется в папке сборки
# (JuceLibraryCode/ - только для Projucer, в CMake-сборку не подключается)
juce_generate_juce_header(GuitarTX)

# Иконка приложения (exe, окно, панель задач): готовый многоразмерный .ico,
# мелкие размеры нарисованы отдельно (толще линии), а не уменьшены из 512 px
if (WIN32)
    set(GUITARTX_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/GuitarTX_icon.rc")
    file(WRITE "${GUITARTX_ICON_RC}"
         "IDI_ICON1 ICON DISCARDABLE \"${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.ico\"\n")
    target_sources(GuitarTX PRIVATE "${GUITARTX_ICON_RC}")
endif()

file(GLOB GUITARTX_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")

target_sources(GuitarTX PRIVATE ${GUITARTX_SOURCES})

target_compile_definitions(GuitarTX PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:GuitarTX,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:GuitarTX,JUCE_VERSION>"
    JUCE_ASIO=1
    JUCE_ASIO_USE_EXTERNAL_SDK=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

if(MSVC)
    # Исходники в UTF-8 (комментарии на русском)
    target_compile_options(GuitarTX PRIVATE /bigobj /utf-8)
endif()

target_link_libraries(GuitarTX
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_processors_headless
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Интеграционные тесты: DSP и пресеты без аудиоустройства и UI
if(GUITARTX_BUILD_TESTS)
    juce_add_console_app(GuitarTXTests
        PRODUCT_NAME "GuitarTX"
        VERSION      ${PROJECT_VERSION})

    juce_generate_juce_header(GuitarTXTests)

    # Всё, кроме окна приложения и UI-компонентов
    set(GUITARTX_TEST_SOURCES ${GUITARTX_SOURCES})
    list(FILTER GUITARTX_TEST_SOURCES EXCLUDE REGEX "(Main|MainComponent|TunerComponent|EffectPanel|RackComponent|PresetBar|PresetBrowser|MidiWindow|DrumWindow)\\.cpp$")

    target_sources(GuitarTXTests PRIVATE
        ${GUITARTX_TEST_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/GuitarTXTests.cpp")

    target_include_directories(GuitarTXTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

    target_compile_definitions(GuitarTXTests PRIVATE
        JUCE_ASIO=1
        JUCE_ASIO_USE_EXTERNAL_SDK=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

    if(MSVC)
        target_compile_options(GuitarTXTests PRIVATE /bigobj /utf-8)
    endif()

    target_link_libraries(GuitarTXTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors_headless
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags)

    enable_testing()
    add_test(NAME GuitarTXTests COMMAND GuitarTXTests)
endif()
