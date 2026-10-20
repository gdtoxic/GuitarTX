# CMake-сборка GuitarTX (используется в GitHub Actions; локально можно собирать и через Projucer).
#
#   cmake -B build -A x64                     # JUCE скачается автоматически (тег ниже)
#   cmake -B build -A x64 -DGUITARTX_JUCE_DIR="C:/Program Files/JUCE"   # или свой JUCE
#   cmake --build build --config Release
#
# Готовый exe: build/GuitarTX_artefacts/Release/GuitarTX.exe

cmake_minimum_required(VERSION 3.22)

project(GuitarTX VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Статический CRT: exe работает без установки VC++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(GUITARTX_JUCE_TAG "9.0.2" CACHE STRING "Версия JUCE для автозагрузки")
set(GUITARTX_JUCE_DIR "" CACHE PATH "Папка с локальным JUCE (пусто = скачать)")

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
