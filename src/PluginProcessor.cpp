#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// UTILITY FUNCTIONS
//==============================================================================
static inline float clampf (float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
static inline int   clampi (int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
static inline float lerpf  (float a, float b, float t) { return a + (b - a) * t; }

//==============================================================================
// CHEBYSHEV POLYNOMIALS - For targeted harmonic generation
// These generate specific harmonics when applied to a sine wave input
//==============================================================================
static inline float cheb2 (float x) { return 2.0f * x * x - 1.0f; }                           // 2nd harmonic
static inline float cheb3 (float x) { return x * (4.0f * x * x - 3.0f); }                     // 3rd harmonic
static inline float cheb4 (float x) { float x2 = x * x; return 8.0f * x2 * x2 - 8.0f * x2 + 1.0f; }  // 4th
static inline float cheb5 (float x) { float x2 = x * x; return x * (16.0f * x2 * x2 - 20.0f * x2 + 5.0f); } // 5th

//==============================================================================
// FAST APPROXIMATIONS
//==============================================================================
static inline float fastTanh (float x)
{
    // Pade approximation - accurate for |x| < 3, clamped beyond
    x = clampf (x, -4.5f, 4.5f);
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

//==============================================================================
// HARMONIC WAVESHAPER
// Attempt mix of Chebyshev polynomials for targeted harmonic content
//==============================================================================
struct HarmonicShaper
{
    float h2 = 0.0f;  // 2nd harmonic amount (even - tube/warm)
    float h3 = 0.0f;  // 3rd harmonic amount (odd - transformer/crunch)
    float h4 = 0.0f;  // 4th harmonic
    float h5 = 0.0f;  // 5th harmonic
    float drive = 1.0f;
    float asymmetry = 0.0f;  // 0 = symmetric, >0 = more positive bias

    float process (float x) const
    {
        // Apply drive
        float driven = x * drive;

        // Soft clip the input to keep Chebyshev polynomials stable
        float clipped = clampf (driven, -1.0f, 1.0f);

        // Attempt fundamental + harmonics mix
        float fundamental = fastTanh (driven);

        // Add targeted harmonics (scaled by input level for natural behavior)
        float level = std::fabs (clipped);
        float harmonics = h2 * cheb2 (clipped) * level
                        + h3 * cheb3 (clipped) * level
                        + h4 * cheb4 (clipped) * level * 0.7f
                        + h5 * cheb5 (clipped) * level * 0.5f;

        float out = fundamental + harmonics;

        // Apply asymmetry (even harmonic boost from DC bias behavior)
        if (asymmetry > 0.0f)
        {
            float bias = asymmetry * 0.1f;
            out = fastTanh ((out + bias) * 1.1f) - fastTanh (bias);
        }

        return out;
    }
};

//==============================================================================
// FILTERS
//==============================================================================
struct OnePoleLP
{
    float z = 0.0f;
    float a = 0.0f;

    void setCutoff (float hz, float sr)
    {
        hz = clampf (hz, 10.0f, sr * 0.49f);
        a = std::exp (-6.28318530718f * hz / sr);
    }

    float process (float x)
    {
        z = a * z + (1.0f - a) * x;
        return z;
    }

    void reset() { z = 0.0f; }
};

struct OnePoleHP
{
    float z = 0.0f;
    float a = 0.0f;

    void setCutoff (float hz, float sr)
    {
        hz = clampf (hz, 10.0f, sr * 0.49f);
        a = std::exp (-6.28318530718f * hz / sr);
    }

    float process (float x)
    {
        z = a * z + (1.0f - a) * x;
        return x - z;
    }

    void reset() { z = 0.0f; }
};

//==============================================================================
// BIQUAD FILTER - For crossover and EQ
//==============================================================================
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() { z1 = z2 = 0.0f; }

    float process (float x)
    {
        float y = b0 * x + b1 * z1 + b2 * z2 - a1 * z1 - a2 * z2;
        z2 = z1;
        z1 = y;
        return y;
    }

    void setLowpass (float freq, float q, float sr)
    {
        float w0 = 6.28318530718f * freq / sr;
        float cosw0 = std::cos (w0);
        float sinw0 = std::sin (w0);
        float alpha = sinw0 / (2.0f * q);

        float a0 = 1.0f + alpha;
        b0 = ((1.0f - cosw0) / 2.0f) / a0;
        b1 = (1.0f - cosw0) / a0;
        b2 = b0;
        a1 = (-2.0f * cosw0) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setHighpass (float freq, float q, float sr)
    {
        float w0 = 6.28318530718f * freq / sr;
        float cosw0 = std::cos (w0);
        float sinw0 = std::sin (w0);
        float alpha = sinw0 / (2.0f * q);

        float a0 = 1.0f + alpha;
        b0 = ((1.0f + cosw0) / 2.0f) / a0;
        b1 = -(1.0f + cosw0) / a0;
        b2 = b0;
        a1 = (-2.0f * cosw0) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setLowShelf (float freq, float gainDb, float sr)
    {
        float A = std::pow (10.0f, gainDb / 40.0f);
        float w0 = 6.28318530718f * freq / sr;
        float cosw0 = std::cos (w0);
        float sinw0 = std::sin (w0);
        float alpha = sinw0 / 2.0f * std::sqrt (2.0f);

        float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt (A) * alpha;
        b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt (A) * alpha)) / a0;
        b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt (A) * alpha)) / a0;
        a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt (A) * alpha) / a0;
    }

    void setHighShelf (float freq, float gainDb, float sr)
    {
        float A = std::pow (10.0f, gainDb / 40.0f);
        float w0 = 6.28318530718f * freq / sr;
        float cosw0 = std::cos (w0);
        float sinw0 = std::sin (w0);
        float alpha = sinw0 / 2.0f * std::sqrt (2.0f);

        float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt (A) * alpha;
        b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt (A) * alpha)) / a0;
        b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt (A) * alpha)) / a0;
        a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        a2 = ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt (A) * alpha) / a0;
    }

    void setPeak (float freq, float gainDb, float q, float sr)
    {
        float A = std::pow (10.0f, gainDb / 40.0f);
        float w0 = 6.28318530718f * freq / sr;
        float cosw0 = std::cos (w0);
        float sinw0 = std::sin (w0);
        float alpha = sinw0 / (2.0f * q);

        float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cosw0) / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = b1;
        a2 = (1.0f - alpha / A) / a0;
    }
};

//==============================================================================
// DC BLOCKER
//==============================================================================
struct DcBlocker
{
    float x1 = 0.0f, y1 = 0.0f;
    float r = 0.995f;

    void setFreq (float hz, float sr)
    {
        r = 1.0f - (6.28318530718f * hz / sr);
        r = clampf (r, 0.9f, 0.999f);
    }

    float process (float x)
    {
        float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    void reset() { x1 = y1 = 0.0f; }
};

//==============================================================================
// ENVELOPE FOLLOWER - For transient/compression behavior
//==============================================================================
struct EnvelopeFollower
{
    float env = 0.0f;
    float attackCoef = 0.0f;
    float releaseCoef = 0.0f;

    void setTimes (float attackMs, float releaseMs, float sr)
    {
        attackCoef = 1.0f - std::exp (-1.0f / (attackMs * 0.001f * sr));
        releaseCoef = 1.0f - std::exp (-1.0f / (releaseMs * 0.001f * sr));
    }

    float process (float x)
    {
        float abs_x = std::fabs (x);
        if (abs_x > env)
            env += attackCoef * (abs_x - env);
        else
            env += releaseCoef * (abs_x - env);
        return env;
    }

    void reset() { env = 0.0f; }
};

//==============================================================================
// 3-BAND CROSSOVER - For multiband saturation
//==============================================================================
struct ThreeBandCrossover
{
    Biquad lp1, lp2;   // Low band
    Biquad hp1, hp2;   // High band
    // Mid = input - low - high (allpass derived)

    float lowFreq = 200.0f;
    float highFreq = 3000.0f;

    void setup (float lowF, float highF, float sr)
    {
        lowFreq = lowF;
        highFreq = highF;

        lp1.setLowpass (lowF, 0.707f, sr);
        lp2.setLowpass (lowF, 0.707f, sr);
        hp1.setHighpass (highF, 0.707f, sr);
        hp2.setHighpass (highF, 0.707f, sr);
    }

    void process (float x, float& low, float& mid, float& high)
    {
        low = lp2.process (lp1.process (x));
        high = hp2.process (hp1.process (x));
        mid = x - low - high;
    }

    void reset()
    {
        lp1.reset(); lp2.reset();
        hp1.reset(); hp2.reset();
    }
};

//==============================================================================
// TRANSFORMER EMULATION
// Models frequency-dependent saturation and subtle phase behavior
//==============================================================================
struct TransformerStage
{
    Biquad lowBoost;
    Biquad highRoll;
    HarmonicShaper shaper;
    EnvelopeFollower envLow;
    float bassWeight = 1.0f;
    float sr = 44100.0f;

    void setup (float sampleRate, float bassW, float h2, float h3, float drive)
    {
        sr = sampleRate;
        bassWeight = bassW;
        shaper.h2 = h2;
        shaper.h3 = h3;
        shaper.drive = drive;

        lowBoost.setLowShelf (120.0f, bassW * 2.0f, sr);
        highRoll.setLowpass (18000.0f, 0.707f, sr);
        envLow.setTimes (5.0f, 80.0f, sr);
    }

    float process (float x)
    {
        // Bass frequencies saturate first (transformer core behavior)
        float env = envLow.process (x);
        float dynamicDrive = shaper.drive * (1.0f + env * bassWeight * 0.3f);

        HarmonicShaper dynShaper = shaper;
        dynShaper.drive = dynamicDrive;

        float shaped = dynShaper.process (x);

        // Pre-emphasis EQ (transformer coloration)
        shaped = lowBoost.process (shaped);
        shaped = highRoll.process (shaped);

        return shaped;
    }

    void reset()
    {
        lowBoost.reset();
        highRoll.reset();
        envLow.reset();
    }
};

//==============================================================================
// MODEL DEFINITIONS - Based on famous console characteristics
//==============================================================================
enum ModelId
{
    MODEL_CONSOLE_73 = 0,     // Neve 1073 style
    MODEL_BRITISH_CLEAN,      // SSL 4000 style
    MODEL_AMERICAN_PUNCH,     // API 512 style
    MODEL_DESK_CREAM,         // Trident A-Range style
    MODEL_CLASS_A_SILK,       // Harrison 32C style
    MODEL_ABBEY_GLOW,         // EMI TG12345 style
    MODEL_VALVE_WARMTH,       // Tube console style
    MODEL_TAPE_TOUCH,         // Studer/Ampex style
    MODEL_GERMANIUM_ERA,      // 60s germanium transistor
    MODEL_TRANSFORMER_IRON,   // Heavy transformer color
    MODEL_COUNT
};

//==============================================================================
// CONSOLE MODEL PARAMETERS
//==============================================================================
struct ConsoleModelParams
{
    // Harmonic content
    float h2;           // 2nd harmonic (even - warm)
    float h3;           // 3rd harmonic (odd - edge)
    float h4;           // 4th harmonic
    float h5;           // 5th harmonic
    float asymmetry;    // Asymmetric distortion amount

    // Drive and dynamics
    float baseDrive;    // Base drive amount
    float maxDrive;     // Maximum drive at 100%

    // Frequency response
    float hpFreq;       // Input highpass
    float lpFreq;       // Output lowpass
    float lowShelfFreq; // Low shelf frequency
    float lowShelfGain; // Low shelf gain (dB)
    float highShelfFreq;// High shelf frequency
    float highShelfGain;// High shelf gain (dB)

    // Multiband saturation balance
    float lowSatMult;   // Low band saturation multiplier
    float midSatMult;   // Mid band saturation multiplier
    float highSatMult;  // High band saturation multiplier

    // Crossover frequencies
    float crossLow;     // Low/mid crossover
    float crossHigh;    // Mid/high crossover

    // Dynamics
    float attackMs;     // Envelope attack
    float releaseMs;    // Envelope release
    float compression;  // Dynamic compression amount
};

static const ConsoleModelParams kModelParams[MODEL_COUNT] =
{
    // MODEL_CONSOLE_73 (Neve 1073 style)
    // Known for: Rich 3rd harmonic, transformer weight, musical saturation
    //         h2     h3     h4     h5     asym   bDrv   mDrv   hpF     lpF      lsF     lsG    hsF      hsG    lSat   mSat   hSat   xLo     xHi     atk    rel    comp
    {         0.15f, 0.35f, 0.08f, 0.12f, 0.1f,  1.0f,  3.5f,  20.0f,  18000.f, 100.0f, 1.5f,  8000.0f, -0.5f, 1.3f,  1.0f,  0.8f,  180.0f, 3500.f, 2.0f,  60.0f, 0.15f },

    // MODEL_BRITISH_CLEAN (SSL 4000 style)
    // Known for: Clean, punchy, controlled, tight low end
    {         0.05f, 0.12f, 0.03f, 0.02f, 0.0f,  1.0f,  2.2f,  25.0f,  20000.f, 80.0f,  0.3f,  12000.f, 0.5f,  0.8f,  1.0f,  1.1f,  150.0f, 4000.f, 0.5f,  40.0f, 0.08f },

    // MODEL_AMERICAN_PUNCH (API 512 style)
    // Known for: Punchy mids, 2nd+3rd balance, aggressive but musical
    {         0.22f, 0.28f, 0.10f, 0.08f, 0.05f, 1.0f,  4.0f,  30.0f,  16000.f, 120.0f, 1.0f,  6000.0f, 1.2f,  1.0f,  1.4f,  0.9f,  200.0f, 3000.f, 1.0f,  50.0f, 0.12f },

    // MODEL_DESK_CREAM (Trident A-Range style)
    // Known for: Creamy midrange, rich harmonics, vintage vibe
    {         0.25f, 0.20f, 0.12f, 0.06f, 0.08f, 1.0f,  3.2f,  25.0f,  15000.f, 150.0f, 1.8f,  5000.0f, -0.8f, 1.1f,  1.3f,  0.7f,  250.0f, 2800.f, 3.0f,  80.0f, 0.18f },

    // MODEL_CLASS_A_SILK (Harrison 32C style)
    // Known for: Clean warmth, silky highs, transparent coloration
    {         0.12f, 0.08f, 0.04f, 0.02f, 0.02f, 1.0f,  2.5f,  18.0f,  22000.f, 90.0f,  0.6f,  10000.f, 1.0f,  0.9f,  1.0f,  1.2f,  160.0f, 4500.f, 1.5f,  45.0f, 0.06f },

    // MODEL_ABBEY_GLOW (EMI TG12345 style)
    // Known for: Vintage colored, soft bandwidth, Beatles/Pink Floyd sound
    {         0.30f, 0.18f, 0.15f, 0.10f, 0.12f, 1.0f,  3.0f,  40.0f,  12000.f, 200.0f, 2.0f,  4000.0f, -1.5f, 1.2f,  1.1f,  0.6f,  220.0f, 2500.f, 4.0f,  100.f, 0.20f },

    // MODEL_VALVE_WARMTH (Tube console style)
    // Known for: 2nd harmonic dominant, soft compression, round tone
    {         0.40f, 0.12f, 0.08f, 0.03f, 0.15f, 1.0f,  3.8f,  22.0f,  14000.f, 100.0f, 1.2f,  6000.0f, -1.0f, 1.1f,  1.0f,  0.75f, 180.0f, 3200.f, 5.0f,  120.f, 0.25f },

    // MODEL_TAPE_TOUCH (Studer/Ampex style)
    // Known for: Head bump, HF saturation, gentle compression, hysteresis
    {         0.20f, 0.25f, 0.12f, 0.08f, 0.06f, 1.0f,  3.5f,  28.0f,  14000.f, 80.0f,  2.5f,  8000.0f, -2.0f, 1.0f,  1.0f,  1.3f,  150.0f, 4000.f, 1.0f,  70.0f, 0.22f },

    // MODEL_GERMANIUM_ERA (60s germanium transistor style)
    // Known for: Unpredictable, asymmetric, fuzzy, vintage character
    {         0.35f, 0.30f, 0.18f, 0.15f, 0.25f, 1.0f,  5.0f,  50.0f,  10000.f, 250.0f, 1.0f,  3500.0f, -2.5f, 1.2f,  1.4f,  0.5f,  300.0f, 2200.f, 2.0f,  90.0f, 0.18f },

    // MODEL_TRANSFORMER_IRON (Heavy transformer color)
    // Known for: Strong coloration, frequency-dependent saturation, weight
    {         0.18f, 0.42f, 0.14f, 0.18f, 0.08f, 1.0f,  4.5f,  25.0f,  16000.f, 80.0f,  2.2f,  5000.0f, -1.2f, 1.5f,  1.0f,  0.7f,  160.0f, 3000.f, 3.0f,  75.0f, 0.20f }
};

//==============================================================================
// CHANNEL PROCESSOR - Per-channel state for one console model
//==============================================================================
struct ChannelProcessor
{
    // Input stage
    OnePoleHP inputHP;
    DcBlocker dcIn;

    // Multiband
    ThreeBandCrossover crossover;
    HarmonicShaper lowShaper, midShaper, highShaper;

    // EQ
    Biquad lowShelf;
    Biquad highShelf;

    // Output stage
    OnePoleLP outputLP;
    DcBlocker dcOut;

    // Dynamics
    EnvelopeFollower envelope;

    // State
    float sr = 44100.0f;
    int currentModel = -1;

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        currentModel = -1;  // Force reconfiguration

        dcIn.setFreq (8.0f, sr);
        dcOut.setFreq (5.0f, sr);

        reset();
    }

    void reset()
    {
        inputHP.reset();
        dcIn.reset();
        crossover.reset();
        lowShelf.reset();
        highShelf.reset();
        outputLP.reset();
        dcOut.reset();
        envelope.reset();
    }

    void configureForModel (int model, float amount)
    {
        if (model < 0 || model >= MODEL_COUNT)
            model = 0;

        const auto& p = kModelParams[model];

        // Input HP
        inputHP.setCutoff (lerpf (10.0f, p.hpFreq, amount), sr);

        // Crossover
        crossover.setup (p.crossLow, p.crossHigh, sr);

        // Calculate drive based on amount
        float drive = lerpf (p.baseDrive, p.maxDrive, amount);

        // Configure band shapers with model's harmonic profile
        auto configShaper = [&] (HarmonicShaper& shaper, float mult)
        {
            shaper.h2 = p.h2 * amount;
            shaper.h3 = p.h3 * amount;
            shaper.h4 = p.h4 * amount;
            shaper.h5 = p.h5 * amount;
            shaper.drive = drive * mult;
            shaper.asymmetry = p.asymmetry * amount;
        };

        configShaper (lowShaper, p.lowSatMult);
        configShaper (midShaper, p.midSatMult);
        configShaper (highShaper, p.highSatMult);

        // EQ - scaled by amount
        float lowGain = p.lowShelfGain * amount;
        float highGain = p.highShelfGain * amount;
        lowShelf.setLowShelf (p.lowShelfFreq, lowGain, sr);
        highShelf.setHighShelf (p.highShelfFreq, highGain, sr);

        // Output LP
        float lpFreq = lerpf (20000.0f, p.lpFreq, amount);
        outputLP.setCutoff (lpFreq, sr);

        // Dynamics
        envelope.setTimes (p.attackMs, p.releaseMs, sr);

        currentModel = model;
    }

    float process (float x, int model, float amount)
    {
        // Reconfigure if model changed (or periodically for amount changes)
        if (model != currentModel)
            configureForModel (model, amount);

        const auto& p = kModelParams[model];

        // Input DC block and HP
        x = dcIn.process (x);
        x = inputHP.process (x);

        // Envelope for dynamics
        float env = envelope.process (x);

        // Dynamic drive modulation (compression behavior)
        float compression = p.compression * amount;
        float dynamicGain = 1.0f - compression * clampf (env * 2.0f, 0.0f, 1.0f);

        // Split into bands
        float low, mid, high;
        crossover.process (x, low, mid, high);

        // Apply saturation to each band
        float drive = lerpf (p.baseDrive, p.maxDrive, amount);

        // Update shapers with dynamic drive
        lowShaper.drive = drive * p.lowSatMult * dynamicGain;
        midShaper.drive = drive * p.midSatMult;
        highShaper.drive = drive * p.highSatMult * (1.0f + compression * 0.3f);

        low = lowShaper.process (low);
        mid = midShaper.process (mid);
        high = highShaper.process (high);

        // Recombine
        float y = low + mid + high;

        // Apply EQ coloration
        y = lowShelf.process (y);
        y = highShelf.process (y);

        // Output LP and DC block
        y = outputLP.process (y);
        y = dcOut.process (y);

        // Soft limit to prevent overs
        y = fastTanh (y * 0.9f) / 0.9f;

        return y;
    }
};

//==============================================================================
// MAIN DSP ENGINE
//==============================================================================
class VillainAudioProcessor::AnalogEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate > 1000.0 ? sampleRate : 44100.0;
        left.prepare ((float) sr);
        right.prepare ((float) sr);
    }

    void process (juce::AudioBuffer<float>& buffer, int model, float mix01)
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        const float wetMix = clampf (mix01, 0.0f, 1.0f);

        // Early exit if mix is zero
        if (wetMix < 0.0001f)
            return;

        const float dryMix = 1.0f - wetMix;

        auto* ch0 = buffer.getWritePointer (0);
        auto* ch1 = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

        // Configure processors for current model and amount
        // Note: amount is the wet mix - more mix = more effect intensity
        left.configureForModel (model, wetMix);
        right.configureForModel (model, wetMix);

        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = ch0[i];
            const float inR = ch1 ? ch1[i] : inL;

            // Process through console model
            const float wetL = left.process (inL, model, wetMix);
            const float wetR = right.process (inR, model, wetMix);

            // Dry/wet mix
            ch0[i] = dryMix * inL + wetMix * wetL;
            if (ch1)
                ch1[i] = dryMix * inR + wetMix * wetR;
        }
    }

private:
    double sr = 44100.0;
    ChannelProcessor left;
    ChannelProcessor right;
};

//==============================================================================
// PLUGIN PROCESSOR IMPLEMENTATION
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
        juce::ParameterID { paramModelId, 1 }, "Model", 0, kNumModels - 1, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramMixId, 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f));

    return { params.begin(), params.end() };
}

juce::StringArray VillainAudioProcessor::getModelNames()
{
    return {
        "1. 73",
        "2. British Clean",
        "3. American Punch",
        "4. Chocolate Cream",
        "5. Silk Milk",
        "6. Low Glow",
        "7. Valve Storm",
        "8. Stereo Tape",
        "9. Germanium",
        "10. Iron Moon"
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
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VillainAudioProcessor();
}
