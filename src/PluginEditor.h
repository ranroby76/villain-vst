#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

class VillainAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit VillainAudioProcessorEditor (VillainAudioProcessor&);
    ~VillainAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    VillainAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;

    juce::Image pages[VillainAudioProcessor::kNumModels];
    juce::Image knobFilmstrip;

    int currentModel = 0;

    //==============================================================================
    class FilmstripKnobLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void setFilmstrip (juce::Image img, int frames, bool vertical)
        {
            filmstrip = img;
            numFrames = juce::jmax (1, frames);
            isVertical = vertical;
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                               float sliderPosProportional,
                               float /*rotaryStartAngle*/, float /*rotaryEndAngle*/,
                               juce::Slider& /*slider*/) override
        {
            if (!filmstrip.isValid() || numFrames <= 0)
                return;

            const int frame = juce::jlimit (0, numFrames - 1,
                                            (int) std::round (sliderPosProportional * (numFrames - 1)));

            if (isVertical)
            {
                const int fw = filmstrip.getWidth();
                const int fh = filmstrip.getHeight() / numFrames;
                juce::Rectangle<int> src (0, frame * fh, fw, fh);
                g.drawImage (filmstrip, x, y, w, h, src.getX(), src.getY(), src.getWidth(), src.getHeight());
            }
            else
            {
                const int fw = filmstrip.getWidth() / numFrames;
                const int fh = filmstrip.getHeight();
                juce::Rectangle<int> src (frame * fw, 0, fw, fh);
                g.drawImage (filmstrip, x, y, w, h, src.getX(), src.getY(), src.getWidth(), src.getHeight());
            }
        }

    private:
        juce::Image filmstrip;
        int numFrames = 1;
        bool isVertical = true;
    };

    FilmstripKnobLookAndFeel knobLnf;

    //==============================================================================
    // Mix knob: keep cursor "frozen" (unbounded movement) while dragging + show different cursor icon.
    class MixKnobSlider final : public juce::Slider
    {
    public:
        MixKnobSlider() = default;

        void mouseDown (const juce::MouseEvent& e) override
        {
            // Hide/lock the OS pointer movement so it won't roam away from the knob.
            e.source.enableUnboundedMouseMovement (true);

            // Use a different cursor while dragging.
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);

            juce::Slider::mouseDown (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            juce::Slider::mouseUp (e);

            e.source.enableUnboundedMouseMovement (false);
            setMouseCursor (juce::MouseCursor::NormalCursor);
        }

        void mouseExit (const juce::MouseEvent& e) override
        {
            // Safety: if for any reason we leave while dragging, ensure we return cursor.
            if (! isMouseButtonDown())
                setMouseCursor (juce::MouseCursor::NormalCursor);

            juce::Slider::mouseExit (e);
        }
    };

    MixKnobSlider mixKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

    //==============================================================================
    class ModelGrid : public juce::Component
    {
    public:
        explicit ModelGrid (juce::AudioProcessorValueTreeState& state);
        ~ModelGrid() override;

        void resized() override;
        void paint (juce::Graphics& g) override;

        void syncFromParameter();
        int  getSelected() const { return selected; }

        // Set selector font size (in pixels). We will match the UI text size (e.g. "the great electricity").
        void setSelectorFontHeight (float newHeight);

    private:
        juce::AudioProcessorValueTreeState& apvts;

        struct TableButtonLookAndFeel : public juce::LookAndFeel_V4
        {
            float fixedFontHeight = 16.0f;

            juce::Font getTextButtonFont (juce::TextButton&, int /*buttonHeight*/) override
            {
                return juce::Font (juce::Font::getDefaultSansSerifFontName(),
                                   juce::jmax (8.0f, fixedFontHeight),
                                   juce::Font::bold);
            }

            void drawButtonBackground (juce::Graphics& g,
                                       juce::Button& button,
                                       const juce::Colour& /*backgroundColour*/,
                                       bool isMouseOverButton,
                                       bool isButtonDown) override
            {
                const bool on = button.getToggleState();

                auto r = button.getLocalBounds().toFloat();

                auto baseOff  = juce::Colour::fromRGB (165, 135, 25);
                auto baseOn   = juce::Colour::fromRGB (220, 185, 40);
                auto hoverAdd = juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, 0.07f);
                auto downAdd  = juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, 0.08f);

                auto c = on ? baseOn : baseOff;

                if (isMouseOverButton)
                    c = c.overlaidWith (hoverAdd);

                if (isButtonDown)
                    c = c.overlaidWith (downAdd);

                g.setColour (c);
                g.fillRect (r);
            }

            void drawButtonText (juce::Graphics& g,
                                 juce::TextButton& button,
                                 bool /*isMouseOverButton*/,
                                 bool /*isButtonDown*/) override
            {
                juce::Font f = getTextButtonFont (button, button.getHeight());
                g.setFont (f);

                g.setColour (juce::Colours::black);

                auto r = button.getLocalBounds().reduced (4);

                // Allow 2 lines so "word on word" names can render fully.
                g.drawFittedText (button.getButtonText(), r, juce::Justification::centred, 2);
            }
        };

        TableButtonLookAndFeel tableLnf;

        juce::TextButton buttons[VillainAudioProcessor::kNumModels];
        int selected = 0;

        void setModel (int idx);
        void updateButtonStates();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelGrid)
    };

    ModelGrid modelGrid;

    //==============================================================================
    class PresetBar : public juce::Component
    {
    public:
        explicit PresetBar (VillainAudioProcessor& proc);

        void resized() override;
        void paint (juce::Graphics& g) override;

        void refreshPresetName();

    private:
        VillainAudioProcessor& processor;

        struct PresetLookAndFeel : public juce::LookAndFeel_V4
        {
            juce::Font f;
            explicit PresetLookAndFeel (juce::Font font) : f (std::move (font)) {}

            juce::Font getComboBoxFont (juce::ComboBox&) override { return f; }
            juce::Font getTextButtonFont (juce::TextButton&, int) override { return f; }
        };

        juce::Font comicFont;
        PresetLookAndFeel lnf;

        juce::ComboBox presetBox;
        juce::TextButton loadButton { "Load" };
        juce::TextButton saveButton { "Save" };

        std::unique_ptr<juce::FileChooser> fileChooser;

        void onLoad();
        void onSave();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
    };

    PresetBar presetBar;

    //==============================================================================
    // Resizing support (corner drag + aspect-locked scaling)
    juce::ComponentBoundsConstrainer resizeConstrainer;
    juce::ResizableCornerComponent   resizer;

    //==============================================================================
    // Base UI geometry (designed for the background PNG pages)
    const juce::Rectangle<int> presetBounds   { 24, 127, 500, 52 };
    const juce::Rectangle<int> selectorBounds { 24, 187, 500, 110 };

    // Correct knob placement in base (100%) coordinates
    const juce::Rectangle<float> mixKnobBoundsF { 373.5f, 655.5f, 128.0f, 128.0f };

    static constexpr int kUiW = 550;
    static constexpr int kUiH = 844;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VillainAudioProcessorEditor)
};