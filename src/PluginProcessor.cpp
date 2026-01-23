#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
static inline float clampf (float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
static inline int   clampi (int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
static inline float lerpf  (float a, float b, float t) { return a + (b - a) * t; }

static inline float fastTanh (float x)
{
    x = clampf (x, -6.0f, 6.0f);
    return std::tanh (x);
}

static inline float softClipCubic (float x)
{
    if (x <= -1.0f) return -2.0f / 3.0f;
    if (x >=  1.0f) return  2.0f / 3.0f;
    return x - (x * x * x) / 3.0f;
}

static inline float wrapDrive (float x, float drive)
{
    return fastTanh (x * drive);
}

struct OnePoleLP
{
    float z = 0.0f;
    float a = 0.0f;

    void setCutHz (float hz, float sr)
    {
        hz = clampf (hz, 20.0f, 0.49f * sr);
        a = std::exp (-6.28318530717958647692f * hz / sr);
    }

    float process (float x)
    {
        z = a * z + (1.0f - a) * x;
        return z;
    }

    void clear() { z = 0.0f; }
};

struct OnePoleHP
{
    float z = 0.0f;
    float a = 0.0f;

    void setCutHz (float hz, float sr)
    {
        hz = clampf (hz, 10.0f, 0.49f * sr);
        a = std::exp (-6.28318530717958647692f * hz / sr);
    }

    float process (float x)
    {
        z = a * z + (1.0f - a) * x;
        return x - z;
    }

    void clear() { z = 0.0f; }
};

struct DcBlock
{
    float x1 = 0.0f;
    float y1 = 0.0f;
    float r  = 0.995f;

    void setCutHz (float hz, float sr)
    {
        hz = clampf (hz, 5.0f, 60.0f);
        r = std::exp (-6.28318530717958647692f * hz / sr);
    }

    float process (float x)
    {
        float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    void clear()
    {
        x1 = 0.0f;
        y1 = 0.0f;
    }
};

static inline uint32_t xorshift32 (uint32_t& s)
{
    uint32_t x = s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s = x;
    return x;
}

static inline float noiseWhite (uint32_t& s)
{
    const uint32_t r = xorshift32 (s);
    const float u = (float) (r & 0x00FFFFFFu) / 16777215.0f;
    return (u * 2.0f - 1.0f);
}

enum ModelId
{
    MODEL_GREAT_ELECTRICITY = 0,
    MODEL_VELVET_MOJO,
    MODEL_IRON_TRANSFORMER,
    MODEL_TUBE_SPARKLE,
    MODEL_TAPE_SATIN,
    MODEL_HEAD_CRUNCH,
    MODEL_DIODE_HEAT,
    MODEL_GERMANIUM_KISS,
    MODEL_LOW_GLOW,
    MODEL_ANCIENT_VINTAGE,
    MODEL_COUNT
};

struct ChannelState
{
    OnePoleLP lpA;
    OnePoleLP lpB;
    OnePoleHP hpA;
    DcBlock   dc;

    float sag = 0.0f;
    uint32_t rng = 0x12345678u;

    void clear()
    {
        lpA.clear();
        lpB.clear();
        hpA.clear();
        dc.clear();
        sag = 0.0f;
        rng = 0x12345678u;
    }
};

static float processModelSample (int model, float x, float amount01, float sr, ChannelState& st)
{
    const float a = clampf (amount01, 0.0f, 1.0f);

    st.dc.setCutHz (10.0f, sr);
    x = st.dc.process (x);

    float y = x;

    switch (model)
    {
        default:
        case MODEL_GREAT_ELECTRICITY:
        {
            float drive = lerpf (1.0f, 3.5f, a);
            st.lpA.setCutHz (lerpf (9000.0f, 15000.0f, 1.0f - a), sr);
            y = wrapDrive (y, drive);
            y = st.lpA.process (y);
        } break;

        case MODEL_VELVET_MOJO:
        {
            float drive = lerpf (1.0f, 2.2f, a);
            st.hpA.setCutHz (lerpf (20.0f, 35.0f, a), sr);
            y = st.hpA.process (y);
            y = softClipCubic (y * drive);
            st.lpA.setCutHz (lerpf (12000.0f, 18000.0f, 1.0f - 0.7f * a), sr);
            y = st.lpA.process (y);
        } break;

        case MODEL_IRON_TRANSFORMER:
        {
            float drive = lerpf (1.0f, 4.5f, a);
            st.hpA.setCutHz (lerpf (18.0f, 55.0f, a), sr);
            y = st.hpA.process (y);

            float bias = 0.06f * a;
            y = wrapDrive (y + bias, drive) - wrapDrive (bias, drive);

            st.lpA.setCutHz (lerpf (9000.0f, 14000.0f, 1.0f - a), sr);
            y = st.lpA.process (y);
        } break;

        case MODEL_TUBE_SPARKLE:
        {
            float drive = lerpf (1.0f, 3.8f, a);
            float bias  = 0.10f * a;

            st.hpA.setCutHz (lerpf (15.0f, 45.0f, a), sr);
            y = st.hpA.process (y);

            y = wrapDrive (y + bias, drive) - wrapDrive (bias, drive);
            st.lpA.setCutHz (lerpf (8000.0f, 13000.0f, 1.0f - a), sr);
            y = st.lpA.process (y);
        } break;

        case MODEL_TAPE_SATIN:
        {
            float drive = lerpf (1.0f, 3.0f, a);

            st.hpA.setCutHz (lerpf (10.0f, 35.0f, a), sr);
            float hp = st.hpA.process (y);
            float low = y - hp;
            y = hp + low * lerpf (1.05f, 1.18f, a);

            y = softClipCubic (y * drive);

            st.lpA.setCutHz (lerpf (6500.0f, 11000.0f, 1.0f - a), sr);
            y = st.lpA.process (y);
        } break;

        case MODEL_HEAD_CRUNCH:
        {
            float drive = lerpf (1.0f, 6.0f, a);

            st.hpA.setCutHz (lerpf (18.0f, 60.0f, a), sr);
            y = st.hpA.process (y);

            y = wrapDrive (y, drive);

            st.lpA.setCutHz (lerpf (5500.0f, 9500.0f, 1.0f - a), sr);
            y = st.lpA.process (y);
        } break;

        case MODEL_DIODE_HEAT:
        {
            float drive = lerpf (1.0f, 7.0f, a);

            st.lpA.setCutHz (18000.0f, sr);
            float bright = y - st.lpA.process (y);
            y = y + bright * (0.25f * a);

            float posDrive = drive * 1.05f;
            float negDrive = drive * 0.85f;
            float yp = wrapDrive (std::max (0.0f, y), posDrive);
            float yn = wrapDrive (std::max (0.0f, -y), negDrive);
            y = yp - yn;

            st.lpB.setCutHz (lerpf (8000.0f, 14000.0f, 1.0f - a), sr);
            y = st.lpB.process (y);
        } break;

        case MODEL_GERMANIUM_KISS:
        {
            float drive = lerpf (1.0f, 8.5f, a);
            float bias  = 0.14f * a;

            st.hpA.setCutHz (lerpf (20.0f, 80.0f, a), sr);
            y = st.hpA.process (y);

            y = wrapDrive (y + bias, drive) - wrapDrive (bias, drive);
            y = 0.75f * y + 0.25f * softClipCubic (y);

            st.lpA.setCutHz (lerpf (6500.0f, 11500.0f, 1.0f - a), sr);
            y = st.lpA.process (y);
        } break;

        case MODEL_LOW_GLOW:
        {
            float drive = lerpf (1.0f, 5.0f, a);

            float absx = std::fabs (y);
            float atk = 1.0f - std::exp (-1.0f / (0.003f * sr));
            float rel = 1.0f - std::exp (-1.0f / (0.070f * sr));
            if (absx > st.sag) st.sag += (absx - st.sag) * atk;
            else               st.sag += (absx - st.sag) * rel;

            float sagAmt = 0.35f * a;
            float sagGain = 1.0f - sagAmt * clampf (st.sag * 1.5f, 0.0f, 1.0f);

            y *= sagGain;
            y = wrapDrive (y, drive);

            st.lpA.setCutHz (lerpf (9000.0f, 17000.0f, 1.0f - a), sr);
            y = st.lpA.process (y);
        } break;

        case MODEL_ANCIENT_VINTAGE:
        {
            float drive = lerpf (1.0f, 5.5f, a);

            st.hpA.setCutHz (lerpf (60.0f, 180.0f, a), sr);
            y = st.hpA.process (y);

            st.lpA.setCutHz (lerpf (3500.0f, 7500.0f, 1.0f - a), sr);
            y = st.lpA.process (y);

            y = wrapDrive (y, drive);

            float n = noiseWhite (st.rng) * (0.0006f * a);
            y += n;
        } break;
    }

    return y;
}

//==============================================================================
// Private DSP engine type (declared in header as VillainAudioProcessor::AnalogEngine)
class VillainAudioProcessor::AnalogEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        left.clear();
        right.clear();
        left.rng = 0xA341316Cu;
        right.rng = 0xC8013EA4u;
    }

    void process (juce::AudioBuffer<float>& buffer, int model, float mix01)
    {
        const int nCh = buffer.getNumChannels();
        const int nS  = buffer.getNumSamples();
        const float a = clampf (mix01, 0.0f, 1.0f);
        if (a <= 0.00001f)
            return;

        auto* ch0 = buffer.getWritePointer (0);
        auto* ch1 = (nCh > 1) ? buffer.getWritePointer (1) : nullptr;

        for (int i = 0; i < nS; ++i)
        {
            float xL = ch0[i];
            float xR = (ch1 != nullptr) ? ch1[i] : xL;

            float yL = processModelSample (model, xL, a, (float) sr, left);
            float yR = processModelSample (model, xR, a, (float) sr, right);

            ch0[i] = xL + a * (yL - xL);
            if (ch1 != nullptr)
                ch1[i] = xR + a * (yR - xR);
        }
    }

private:
    double sr = 44100.0;
    ChannelState left;
    ChannelState right;
};

//==============================================================================
VillainAudioProcessor::VillainAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    engine = std::make_unique<AnalogEngine>();
}

VillainAudioProcessor::~VillainAudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout VillainAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterInt>(
        paramModelId, "Model", 0, kNumModels - 1, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        paramMixId, "Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f));

    return { params.begin(), params.end() };
}

juce::StringArray VillainAudioProcessor::getModelNames()
{
    return {
        "1. The Great Electricity",
        "2. Velvet mojo",
        "3. Iron Transformer",
        "4. Tube sparkle",
        "5. Tape Satin",
        "6. head Crunch",
        "7. Diode Heat",
        "8. Germanium Kiss",
        "9. low glow",
        "10. Ancient vintage"
    };
}

const juce::String VillainAudioProcessor::getName() const { return JucePlugin_Name; }
bool VillainAudioProcessor::acceptsMidi() const { return false; }
bool VillainAudioProcessor::producesMidi() const { return false; }
bool VillainAudioProcessor::isMidiEffect() const { return false; }
double VillainAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int VillainAudioProcessor::getNumPrograms() { return 1; }
int VillainAudioProcessor::getCurrentProgram() { return 0; }
void VillainAudioProcessor::setCurrentProgram (int) {}
const juce::String VillainAudioProcessor::getProgramName (int) { return {}; }
void VillainAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void VillainAudioProcessor::prepareToPlay (double sampleRate, int)
{
    if (engine)
        engine->prepare (sampleRate);
}

void VillainAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool VillainAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return (in == out) && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}
#endif

void VillainAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int model = clampi ((int) apvts.getRawParameterValue (paramModelId)->load(), 0, kNumModels - 1);
    const float mix = clampf (apvts.getRawParameterValue (paramMixId)->load(), 0.0f, 1.0f);

    if (engine)
        engine->process (buffer, model, mix);
}

//==============================================================================
bool VillainAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* VillainAudioProcessor::createEditor()
{
    return new VillainAudioProcessorEditor (*this);
}

//==============================================================================
void VillainAudioProcessor::setCurrentPresetName (const juce::String& name)
{
    currentPresetName = name.isNotEmpty() ? name : "Default";
}

juce::String VillainAudioProcessor::getCurrentPresetName() const
{
    return currentPresetName.isNotEmpty() ? currentPresetName : "Default";
}

static juce::ValueTree makeStateForSave (juce::AudioProcessorValueTreeState& apvts, const juce::String& presetName)
{
    juce::ValueTree root ("VILLAIN_STATE");
    root.setProperty ("presetName", presetName, nullptr);
    root.addChild (apvts.copyState(), -1, nullptr);
    return root;
}

void VillainAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto root = makeStateForSave (apvts, getCurrentPresetName());
    std::unique_ptr<juce::XmlElement> xml (root.createXml());
    copyXmlToBinary (*xml, destData);
}

void VillainAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (!xml)
        return;

    auto root = juce::ValueTree::fromXml (*xml);

    if (root.hasType ("VILLAIN_STATE"))
    {
        const auto pn = root.getProperty ("presetName").toString();
        setCurrentPresetName (pn);

        auto child = root.getChild (0);
        if (child.isValid())
            apvts.replaceState (child);
    }
    else
    {
        auto vt = juce::ValueTree::fromXml (*xml);
        if (vt.isValid())
            apvts.replaceState (vt);
    }
}

bool VillainAudioProcessor::savePresetToFile (const juce::File& file, const juce::String& presetName, juce::String& errorOut)
{
    errorOut.clear();

    if (file == juce::File() || file.getFullPathName().isEmpty())
    {
        errorOut = "Invalid file.";
        return false;
    }

    auto root = makeStateForSave (apvts, presetName);
    std::unique_ptr<juce::XmlElement> xml (root.createXml());
    if (!xml)
    {
        errorOut = "Failed to create XML.";
        return false;
    }

    if (!xml->writeTo (file, {}))
    {
        errorOut = "Failed to write preset file.";
        return false;
    }

    setCurrentPresetName (presetName);
    return true;
}

bool VillainAudioProcessor::loadPresetFromFile (const juce::File& file, juce::String& loadedPresetName, juce::String& errorOut)
{
    errorOut.clear();
    loadedPresetName.clear();

    if (!file.existsAsFile())
    {
        errorOut = "Preset file not found.";
        return false;
    }

    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (file));
    if (!xml)
    {
        errorOut = "Invalid preset file (XML parse failed).";
        return false;
    }

    auto root = juce::ValueTree::fromXml (*xml);
    if (!root.isValid())
    {
        errorOut = "Invalid preset file (no state).";
        return false;
    }

    if (root.hasType ("VILLAIN_STATE"))
    {
        loadedPresetName = root.getProperty ("presetName").toString();
        if (loadedPresetName.isEmpty())
            loadedPresetName = file.getFileNameWithoutExtension();

        setCurrentPresetName (loadedPresetName);

        auto child = root.getChild (0);
        if (child.isValid())
            apvts.replaceState (child);
        else
            errorOut = "Preset missing parameter state.";
    }
    else
    {
        apvts.replaceState (root);
        loadedPresetName = file.getFileNameWithoutExtension();
        setCurrentPresetName (loadedPresetName);
    }

    return errorOut.isEmpty();
}

//==============================================================================
// REQUIRED by JUCE plugin client (VST3/AU/etc.)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VillainAudioProcessor();
}