#include "PluginEditor.h"
#include "BinaryData.h"

static juce::Image loadPngFromBinary (const void* data, int size)
{
    return juce::ImageCache::getFromMemory (data, size);
}

static juce::Font makeComicLikeFont()
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), 16.0f, juce::Font::bold));
}

static juce::String stripNumericPrefix (juce::String s)
{
    // Remove leading numbering like: "1 ", "01 ", "1. ", "1) ", "01 - ", "1: " etc.
    s = s.trimStart();

    int i = 0;
    while (i < s.length() && juce::CharacterFunctions::isDigit (s[i]))
        ++i;

    if (i > 0)
    {
        while (i < s.length())
        {
            const juce::juce_wchar c = s[i];
            if (c == ' ' || c == '.' || c == ')' || c == '(' || c == '-' || c == ':' || c == 0x2013 || c == 0x2014)
                ++i;
            else
                break;
        }

        s = s.substring (i).trimStart();
    }

    return s;
}

static juce::String wordOnWordIfTwoWords (const juce::String& s)
{
    auto text = s.trim();

    // Split by whitespace.
    juce::StringArray parts;
    parts.addTokens (text, " \t\r\n", "");
    parts.removeEmptyStrings();

    if (parts.size() == 2)
        return parts[0] + "\n" + parts[1];

    return text;
}

static juce::String toTitleCaseWords (const juce::String& s)
{
    // Capital letter at start of each word (including words on separate lines).
    // Non-letters are preserved; we treat whitespace as word boundaries.
    juce::String out;
    out.preallocateBytes ((size_t) s.getNumBytesAsUTF8());

    bool atWordStart = true;

    for (int i = 0; i < s.length(); ++i)
    {
        const juce::juce_wchar c = s[i];

        if (juce::CharacterFunctions::isWhitespace (c))
        {
            out << c;
            atWordStart = true;
            continue;
        }

        if (juce::CharacterFunctions::isLetter (c))
        {
            if (atWordStart)
                out << juce::String::charToString (juce::CharacterFunctions::toUpperCase (c));
            else
                out << juce::String::charToString (juce::CharacterFunctions::toLowerCase (c));

            atWordStart = false;
            continue;
        }

        // Non-letter, non-whitespace: keep it, but don't force a new word unless next char is whitespace.
        out << c;
        atWordStart = false;
    }

    return out;
}

//==============================================================================
VillainAudioProcessorEditor::ModelGrid::ModelGrid (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    const auto names = VillainAudioProcessor::getModelNames();

    // Default: match UI text size. We will also update this on editor resize to keep scaling consistent.
    tableLnf.fixedFontHeight = 16.0f;

    for (int i = 0; i < VillainAudioProcessor::kNumModels; ++i)
    {
        auto& b = buttons[i];

        juce::String name = names[i];
        name = stripNumericPrefix (name);
        name = wordOnWordIfTwoWords (name);
        name = toTitleCaseWords (name);

        b.setButtonText (name);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (0xBEEF);

        // Cell feel: avoid JUCE default bevels.
        b.setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        b.setColour (juce::TextButton::textColourOffId,  juce::Colours::black);
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);

        b.setConnectedEdges (juce::Button::ConnectedOnLeft
                           | juce::Button::ConnectedOnRight
                           | juce::Button::ConnectedOnTop
                           | juce::Button::ConnectedOnBottom);

        b.setLookAndFeel (&tableLnf);

        b.onClick = [this, i] { setModel (i); };
        addAndMakeVisible (b);
    }

    syncFromParameter();
    updateButtonStates();
}

VillainAudioProcessorEditor::ModelGrid::~ModelGrid()
{
    for (auto& b : buttons)
        b.setLookAndFeel (nullptr);
}

void VillainAudioProcessorEditor::ModelGrid::setSelectorFontHeight (float newHeight)
{
    tableLnf.fixedFontHeight = juce::jlimit (8.0f, 40.0f, newHeight);
    repaint();
}

void VillainAudioProcessorEditor::ModelGrid::paint (juce::Graphics& g)
{
    // Table border + grid lines (real "table" look)
    auto r = getLocalBounds();

    // Background behind cells
    g.setColour (juce::Colour::fromRGB (155, 125, 20));
    g.fillRoundedRectangle (r.toFloat(), 8.0f);

    // Grid
    const int cols = 5;
    const int rows = 2;

    const float x0 = (float) r.getX();
    const float y0 = (float) r.getY();
    const float w  = (float) r.getWidth();
    const float h  = (float) r.getHeight();

    const float cellW = w / (float) cols;
    const float cellH = h / (float) rows;

    // Border
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 2.0f);

    // Inner lines
    g.setColour (juce::Colours::black.withAlpha (0.35f));

    for (int c = 1; c < cols; ++c)
    {
        const float x = x0 + cellW * (float) c;
        g.drawLine (x, y0, x, y0 + h, 1.0f);
    }

    for (int rr = 1; rr < rows; ++rr)
    {
        const float y = y0 + cellH * (float) rr;
        g.drawLine (x0, y, x0 + w, y, 1.0f);
    }
}

void VillainAudioProcessorEditor::ModelGrid::resized()
{
    auto r = getLocalBounds();

    const int cols = 5;
    const int rows = 2;

    const int cellW = r.getWidth() / cols;
    const int cellH = r.getHeight() / rows;

    int idx = 0;
    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            if (idx >= VillainAudioProcessor::kNumModels)
                break;

            auto cell = juce::Rectangle<int> (r.getX() + col * cellW,
                                              r.getY() + row * cellH,
                                              cellW,
                                              cellH);

            // 1px inset so grid lines remain visible.
            buttons[idx].setBounds (cell.reduced (1));
            ++idx;
        }
    }
}

void VillainAudioProcessorEditor::ModelGrid::setModel (int idx)
{
    idx = juce::jlimit (0, VillainAudioProcessor::kNumModels - 1, idx);

    if (auto* p = apvts.getParameter (VillainAudioProcessor::paramModelId))
    {
        const float normalized = p->convertTo0to1 ((float) idx);
        p->beginChangeGesture();
        p->setValueNotifyingHost (normalized);
        p->endChangeGesture();
    }
}

void VillainAudioProcessorEditor::ModelGrid::updateButtonStates()
{
    for (int i = 0; i < VillainAudioProcessor::kNumModels; ++i)
        buttons[i].setToggleState (i == selected, juce::dontSendNotification);
}

void VillainAudioProcessorEditor::ModelGrid::syncFromParameter()
{
    if (auto* v = apvts.getRawParameterValue (VillainAudioProcessor::paramModelId))
    {
        selected = juce::jlimit (0, VillainAudioProcessor::kNumModels - 1,
                                 (int) std::round (v->load()));
        updateButtonStates();
    }
}

//==============================================================================
VillainAudioProcessorEditor::PresetBar::PresetBar (VillainAudioProcessor& proc)
    : processor (proc),
      comicFont (makeComicLikeFont().withHeight (16.0f)),
      lnf (comicFont)
{
    presetBox.setEditableText (true);
    presetBox.setJustificationType (juce::Justification::centredLeft);
    presetBox.setTextWhenNothingSelected ("Default");
    presetBox.setTextWhenNoChoicesAvailable ("Default");
    presetBox.setText ("Default", juce::dontSendNotification);

    presetBox.setLookAndFeel (&lnf);
    loadButton.setLookAndFeel (&lnf);
    saveButton.setLookAndFeel (&lnf);

    loadButton.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (40, 40, 40));
    loadButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);

    saveButton.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (40, 40, 40));
    saveButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);

    loadButton.onClick = [this] { onLoad(); };
    saveButton.onClick = [this] { onSave(); };

    presetBox.onChange = [this]
    {
        processor.setCurrentPresetName (presetBox.getText());
    };

    addAndMakeVisible (presetBox);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (saveButton);

    refreshPresetName();
}

VillainAudioProcessorEditor::PresetBar::~PresetBar()
{
    presetBox.setLookAndFeel (nullptr);
    loadButton.setLookAndFeel (nullptr);
    saveButton.setLookAndFeel (nullptr);
}

void VillainAudioProcessorEditor::PresetBar::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour::fromRGB (25, 25, 25));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 10.0f, 2.0f);
}

void VillainAudioProcessorEditor::PresetBar::resized()
{
    auto r = getLocalBounds().reduced (8);

    auto left = r.removeFromLeft (r.getWidth() - 160);
    presetBox.setBounds (left);

    auto btnArea = r;
    saveButton.setBounds (btnArea.removeFromRight (76).reduced (2));
    loadButton.setBounds (btnArea.removeFromRight (76).reduced (2));
}

void VillainAudioProcessorEditor::PresetBar::refreshPresetName()
{
    const auto name = processor.getCurrentPresetName();
    if (name.isNotEmpty() && presetBox.getText() != name)
        presetBox.setText (name, juce::dontSendNotification);
}

void VillainAudioProcessorEditor::PresetBar::onLoad()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load Villain preset...", juce::File(), "*.villainpreset");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (!file.existsAsFile())
                                      return;

                                  juce::String loadedName, err;
                                  if (!processor.loadPresetFromFile (file, loadedName, err))
                                  {
                                      // ✅ JUCE 8 FIX: Changed from juce::AlertWindow::WarningIcon
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Villain", err);
                                      return;
                                  }

                                  processor.setCurrentPresetName (loadedName);
                                  presetBox.setText (loadedName, juce::dontSendNotification);
                              });
}

void VillainAudioProcessorEditor::PresetBar::onSave()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Save Villain preset...", juce::File(), "*.villainpreset");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;

                                  if (file.getFileExtension().isEmpty())
                                      file = file.withFileExtension (".villainpreset");

                                  const auto presetName = file.getFileNameWithoutExtension();

                                  juce::String err;
                                  if (!processor.savePresetToFile (file, presetName, err))
                                  {
                                      // ✅ JUCE 8 FIX: Changed from juce::AlertWindow::WarningIcon
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Villain", err);
                                      return;
                                  }

                                  processor.setCurrentPresetName (presetName);
                                  presetBox.setText (presetName, juce::dontSendNotification);
                              });
}

//==============================================================================
VillainAudioProcessorEditor::VillainAudioProcessorEditor (VillainAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      apvts (p.getAPVTS()),
      modelGrid (apvts),
      presetBar (p),
      resizer (this, &resizeConstrainer)
{
    // Default editor size: 75% of the original design size.
    const int startW = juce::roundToInt ((float) kUiW * 0.75f);
    const int startH = juce::roundToInt ((float) kUiH * 0.75f);

    resizeConstrainer.setFixedAspectRatio ((double) kUiW / (double) kUiH);

    const int minW = juce::roundToInt ((float) kUiW * 0.50f);
    const int minH = juce::roundToInt ((float) kUiH * 0.50f);
    const int maxW = juce::roundToInt ((float) kUiW * 2.00f);
    const int maxH = juce::roundToInt ((float) kUiH * 2.00f);
    resizeConstrainer.setSizeLimits (minW, minH, maxW, maxH);

    setResizable (true, true);
    setConstrainer (&resizeConstrainer);
    setSize (startW, startH);

    pages[0] = loadPngFromBinary (BinaryData::a1_png,  BinaryData::a1_pngSize);
    pages[1] = loadPngFromBinary (BinaryData::a2_png,  BinaryData::a2_pngSize);
    pages[2] = loadPngFromBinary (BinaryData::a3_png,  BinaryData::a3_pngSize);
    pages[3] = loadPngFromBinary (BinaryData::a4_png,  BinaryData::a4_pngSize);
    pages[4] = loadPngFromBinary (BinaryData::a5_png,  BinaryData::a5_pngSize);
    pages[5] = loadPngFromBinary (BinaryData::a6_png,  BinaryData::a6_pngSize);
    pages[6] = loadPngFromBinary (BinaryData::a7_png,  BinaryData::a7_pngSize);
    pages[7] = loadPngFromBinary (BinaryData::a8_png,  BinaryData::a8_pngSize);
    pages[8] = loadPngFromBinary (BinaryData::a9_png,  BinaryData::a9_pngSize);
    pages[9] = loadPngFromBinary (BinaryData::a10_png, BinaryData::a10_pngSize);

    // Updated knob filmstrip: 128 x 12928, 101 steps (vertical frames).
    knobFilmstrip = loadPngFromBinary (BinaryData::knob_png, BinaryData::knob_pngSize);
    knobLnf.setFilmstrip (knobFilmstrip, 101, true);

    mixKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mixKnob.setLookAndFeel (&knobLnf);

    // New max behavior: 70% is now the "effective 100%".
    // Meaning: the knob still goes 0..1, but we clamp the control range to 0..0.70.
    // So visual 100% equals 0.70 internally.
    mixKnob.setRange (0.0, 0.70, 0.007); // 101 steps: 0..0.70 in 1% chunks of the OLD range
    mixKnob.setDoubleClickReturnValue (true, 0.0);

    // Ensure normal cursor when not dragging.
    mixKnob.setMouseCursor (juce::MouseCursor::PointingHandCursor);

    addAndMakeVisible (mixKnob);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, VillainAudioProcessor::paramMixId, mixKnob);

    addAndMakeVisible (presetBar);
    addAndMakeVisible (modelGrid);

    addAndMakeVisible (resizer);
    resizer.setAlwaysOnTop (true);

    startTimerHz (30);
    timerCallback();
}

VillainAudioProcessorEditor::~VillainAudioProcessorEditor()
{
    mixKnob.setLookAndFeel (nullptr);
}

void VillainAudioProcessorEditor::timerCallback()
{
    const int model = juce::jlimit (0, VillainAudioProcessor::kNumModels - 1,
                                   (int) std::round (apvts.getRawParameterValue (VillainAudioProcessor::paramModelId)->load()));

    if (model != currentModel)
    {
        currentModel = model;
        repaint();
    }

    presetBar.refreshPresetName();
    modelGrid.syncFromParameter();
}

void VillainAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    auto page = pages[currentModel];
    if (!page.isValid())
        return;

    const float s = (float) getWidth() / (float) kUiW;

    if (std::abs (s - 1.0f) < 1.0e-6f)
    {
        g.drawImageAt (page, 0, 0, false);
    }
    else
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        const auto t = juce::AffineTransform::scale (s);
        g.drawImageTransformed (page, t, false);
    }
}

void VillainAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) kUiW;

    // Keep selector text size consistent with the UI text and scale with the UI.
    modelGrid.setSelectorFontHeight (16.0f * s);

    auto scaleRect = [s] (juce::Rectangle<int> r)
    {
        return juce::Rectangle<int> (juce::roundToInt ((float) r.getX() * s),
                                     juce::roundToInt ((float) r.getY() * s),
                                     juce::roundToInt ((float) r.getWidth() * s),
                                     juce::roundToInt ((float) r.getHeight() * s));
    };

    auto scaleRectF = [s] (juce::Rectangle<float> r)
    {
        return juce::Rectangle<int> (juce::roundToInt (r.getX() * s),
                                     juce::roundToInt (r.getY() * s),
                                     juce::roundToInt (r.getWidth() * s),
                                     juce::roundToInt (r.getHeight() * s));
    };

    presetBar.setBounds (scaleRect (presetBounds));
    modelGrid.setBounds (scaleRect (selectorBounds));
    mixKnob.setBounds   (scaleRectF (mixKnobBoundsF));

    const int grip = juce::roundToInt (16.0f * s);
    resizer.setBounds (getWidth() - grip, getHeight() - grip, grip, grip);
}
