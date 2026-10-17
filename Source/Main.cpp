#include <JuceHeader.h>
#include "MainComponent.h"

//==============================================================================
class GuitarTXApplication  : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        // Лог в файл: %APPDATA%\GuitarTX\GuitarTX.log
        logger.reset (juce::FileLogger::createDefaultAppLogger ("GuitarTX", "GuitarTX.log",
                                                                "GuitarTX " + getApplicationVersion()));
        juce::Logger::setCurrentLogger (logger.get());

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;

        juce::Logger::setCurrentLogger (nullptr);
        logger = nullptr;
    }

    void anotherInstanceStarted (const juce::String&) override {}

    //==============================================================================
    class MainWindow    : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                          .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (GuitarTXApplication)
