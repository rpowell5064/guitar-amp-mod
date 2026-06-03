#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ModulationFactory.h"
#include "DefaultCabIR.h"
#include <cmath>
#include <vector>

// ─── Bus layout ───────────────────────────────────────────────────────────────
juce::AudioProcessor::BusesProperties GuitarAmpAudioProcessor::getBusesProps() {
    return BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true);
}

// ─── Parameter layout ─────────────────────────────────────────────────────────
juce::AudioProcessorValueTreeState::ParameterLayout
GuitarAmpAudioProcessor::createParamLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    using PID   = juce::ParameterID;
    using APF   = juce::AudioParameterFloat;
    using APB   = juce::AudioParameterBool;
    using APC   = juce::AudioParameterChoice;
    using Range = juce::NormalisableRange<float>;

    // ── Noise Gate ────────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"gate_bypass",    1}, "Gate Bypass",    false));
    p.push_back(std::make_unique<APF>(PID{"gate_threshold", 1}, "Gate Threshold", Range{-100.f, 0.f},   -45.f));
    p.push_back(std::make_unique<APF>(PID{"gate_attack",    1}, "Gate Attack",    Range{0.1f, 500.f},     5.f));
    p.push_back(std::make_unique<APF>(PID{"gate_release",   1}, "Gate Release",   Range{1.f, 2000.f},   200.f));
    p.push_back(std::make_unique<APF>(PID{"gate_hold",      1}, "Gate Hold",      Range{0.f, 1000.f},   100.f));
    p.push_back(std::make_unique<APF>(PID{"gate_hysteresis",1}, "Gate Hysteresis",Range{0.f, 24.f},       5.f));

    // ── Pitch (Whammy) ────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"pitch_bypass",      1}, "Pitch Bypass", true));
    p.push_back(std::make_unique<APC>(PID{"pitch_mode",        1}, "Whammy Mode",
        juce::StringArray{"-2 Octave", "-1 Octave", "Detune", "+1 Octave", "+2 Octave"}, 3));
    p.push_back(std::make_unique<APF>(PID{"pitch_expression",  1}, "Expression",   Range{0.f, 1.f}, 0.0f));
    p.push_back(std::make_unique<APF>(PID{"pitch_mix",         1}, "Pitch Mix",    Range{0.f, 1.f}, 1.0f));
    p.push_back(std::make_unique<APF>(PID{"pitch_cents",       1}, "Pitch Tune",   Range{-50.f, 50.f}, 0.0f));

    // ── Overdrive / Drive ─────────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"drive_bypass", 1}, "Drive Bypass", false));
    p.push_back(std::make_unique<APC>(PID{"drive_model",  1}, "Drive Model",
        juce::StringArray{"TS-808", "Life Pedal", "NAM", "ProCo RAT", "Big Muff Pi"}, 0));
    p.push_back(std::make_unique<APF>(PID{"drive_drive",  1}, "Drive",  Range{0.f, 1.f}, 0.0f));
    p.push_back(std::make_unique<APF>(PID{"drive_tone",   1}, "Tone",   Range{0.f, 1.f}, 0.6f));
    p.push_back(std::make_unique<APF>(PID{"drive_level",  1}, "Level",  Range{0.f, 1.f}, 0.9f));
    p.push_back(std::make_unique<APF>(PID{"drive_mix",    1}, "Mix",    Range{0.f, 1.f}, 1.0f));
    p.push_back(std::make_unique<APF>(PID{"drive_octave", 1}, "Octave", Range{0.f, 1.f}, 0.3f));

    // ── Amp ───────────────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"amp_bypass", 1}, "Amp Bypass", false));
    p.push_back(std::make_unique<APC>(PID{"amp_model",  1}, "Amp Model",
        juce::StringArray{"Fender Deluxe", "Marshall JCM800", "EVH 5150 III", "Neural",
                          "Sunn Model T", "Orange Rockerverb 50"}, 1));
    p.push_back(std::make_unique<APF>(PID{"amp_gain",     1}, "Gain",     Range{0.f, 1.f}, 0.65f));
    p.push_back(std::make_unique<APF>(PID{"amp_bass",     1}, "Bass",     Range{0.f, 1.f}, 0.55f));
    p.push_back(std::make_unique<APF>(PID{"amp_mid",      1}, "Mid",      Range{0.f, 1.f}, 0.70f));
    p.push_back(std::make_unique<APF>(PID{"amp_treble",   1}, "Treble",   Range{0.f, 1.f}, 0.65f));
    p.push_back(std::make_unique<APF>(PID{"amp_presence", 1}, "Presence", Range{0.f, 1.f}, 0.70f));
    p.push_back(std::make_unique<APF>(PID{"amp_master",   1}, "Master",   Range{0.f, 1.f}, 0.65f));
    p.push_back(std::make_unique<APF>(PID{"amp_sag",      1}, "Sag",      Range{0.f, 1.f}, 0.3f));
    p.push_back(std::make_unique<APF>(PID{"amp_nam_gain", 1}, "NAM Gain", Range{0.f, 4.f}, 1.5f));

    // ── Sunn Model T ──────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APF>(PID{"sunn_vol1",     1}, "Normal Volume",  Range{0.f, 1.f}, 0.75f));
    p.push_back(std::make_unique<APF>(PID{"sunn_bass1",    1}, "Normal Bass",    Range{0.f, 1.f}, 0.80f));
    p.push_back(std::make_unique<APF>(PID{"sunn_mid1",     1}, "Normal Mid",     Range{0.f, 1.f}, 0.55f));
    p.push_back(std::make_unique<APF>(PID{"sunn_treble1",  1}, "Normal Treble",  Range{0.f, 1.f}, 0.30f));
    p.push_back(std::make_unique<APB>(PID{"sunn_bright",   1}, "Normal Bright",  false));
    p.push_back(std::make_unique<APF>(PID{"sunn_vol2",     1}, "Brite Volume",   Range{0.f, 1.f}, 0.75f));
    p.push_back(std::make_unique<APF>(PID{"sunn_bass2",    1}, "Brite Bass",     Range{0.f, 1.f}, 0.80f));
    p.push_back(std::make_unique<APF>(PID{"sunn_mid2",     1}, "Brite Mid",      Range{0.f, 1.f}, 0.55f));
    p.push_back(std::make_unique<APF>(PID{"sunn_treble2",  1}, "Brite Treble",   Range{0.f, 1.f}, 0.30f));
    p.push_back(std::make_unique<APB>(PID{"sunn_bright2",  1}, "Brite Bright",   false));
    p.push_back(std::make_unique<APB>(PID{"sunn_input_pad",1}, "Input Pad",      false));

    // ── EVH 5150 III ──────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"evh_channel",    1}, "EVH Channel",    true)); // true=Red(lead)
    p.push_back(std::make_unique<APF>(PID{"evh_resonance",  1}, "EVH Resonance",  Range{0.f, 1.f}, 0.5f));

    // ── Sunn Model T — channel link ───────────────────────────────────────────
    p.push_back(std::make_unique<APC>(PID{"sunn_channel_link", 1}, "Sunn Channel Link",
        juce::StringArray{"Independent", "Parallel", "Series"}, 0));

    // ── Orange Rockerverb 50 ──────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"rvb_channel", 1}, "RVB Channel", false));
    p.push_back(std::make_unique<APF>(PID{"rvb_gain",    1}, "RVB Gain",    Range{0.f, 1.f}, 0.55f));
    p.push_back(std::make_unique<APF>(PID{"rvb_bass",    1}, "RVB Bass",    Range{0.f, 1.f}, 0.35f));
    p.push_back(std::make_unique<APF>(PID{"rvb_mid",     1}, "RVB Mid",     Range{0.f, 1.f}, 0.70f));
    p.push_back(std::make_unique<APF>(PID{"rvb_treble",  1}, "RVB Treble",  Range{0.f, 1.f}, 0.5f));
    p.push_back(std::make_unique<APF>(PID{"rvb_master",  1}, "RVB Master",  Range{0.f, 1.f}, 0.65f));
    p.push_back(std::make_unique<APF>(PID{"rvb_sag",     1}, "RVB Sag",     Range{0.f, 1.f}, 0.3f));

    // ── Power Amp ─────────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"pamp_bypass",    1}, "Power Amp Bypass", false));
    p.push_back(std::make_unique<APC>(PID{"pamp_tube",      1}, "Tube Type",
        juce::StringArray{"6L6GC", "EL34", "EL84", "KT88"}, 1));
    // Defaults match getDefaultsForModel(1) — Marshall JCM800 (the plugin's default amp).
    p.push_back(std::make_unique<APF>(PID{"pamp_presence",  1}, "PA Presence",  Range{0.f, 1.f}, 0.55f));
    p.push_back(std::make_unique<APF>(PID{"pamp_depth",     1}, "PA Depth",     Range{0.f, 1.f}, 0.18f));
    p.push_back(std::make_unique<APF>(PID{"pamp_sag",       1}, "PA Sag",       Range{0.f, 1.f}, 0.33f));
    p.push_back(std::make_unique<APF>(PID{"pamp_master",    1}, "PA Master",    Range{0.f, 1.f}, 0.62f));
    p.push_back(std::make_unique<APF>(PID{"pamp_nfb",       1}, "PA NFB",       Range{0.f, 1.f}, 0.42f));
    p.push_back(std::make_unique<APF>(PID{"pamp_resonance", 1}, "PA Resonance", Range{0.f, 1.f}, 0.5f));
    p.push_back(std::make_unique<APB>(PID{"pamp_airfeel",   1}, "Air Feel", false));

    // ── Cabinet ───────────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"cab_bypass",  1}, "Cab Bypass", false));
    p.push_back(std::make_unique<APF>(PID{"cab_lowcut",  1}, "Low Cut",  Range{20.f, 500.f},     80.f));
    p.push_back(std::make_unique<APF>(PID{"cab_highcut", 1}, "High Cut", Range{2000.f, 20000.f}, 16000.f));
    p.push_back(std::make_unique<APF>(PID{"cab_mix",     1}, "Mix",      Range{0.f, 1.f},        1.0f));

    // ── Modulation ────────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"mod_bypass",      1}, "Mod Bypass",    true));
    p.push_back(std::make_unique<APC>(PID{"mod_type",        1}, "Mod Type",
        juce::StringArray{"Boss CE-2 Chorus", "Uni-Vibe"}, 0));
    p.push_back(std::make_unique<APF>(PID{"mod_rate",        1}, "Rate",         Range{0.f, 1.f}, 0.25f));
    p.push_back(std::make_unique<APF>(PID{"mod_depth",       1}, "Depth",        Range{0.f, 1.f}, 0.55f));
    p.push_back(std::make_unique<APF>(PID{"mod_mix",         1}, "Mod Mix",      Range{0.f, 1.f}, 1.0f));
    p.push_back(std::make_unique<APF>(PID{"mod_stereowidth", 1}, "Stereo Width", Range{0.f, 1.f}, 0.0f));
    p.push_back(std::make_unique<APB>(PID{"mod_preamp",      1}, "Preamp",       true));
    p.push_back(std::make_unique<APB>(PID{"mod_vibratomode", 1}, "Vibrato Mode", false));
    p.push_back(std::make_unique<APF>(PID{"mod_outlevel",    1}, "Output Level", Range{0.f, 1.f}, 0.75f));

    // ── Delay ─────────────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"delay_bypass", 1}, "Delay Bypass", false));
    p.push_back(std::make_unique<APC>(PID{"delay_mode",   1}, "Delay Mode",
        juce::StringArray{"Digital", "Tape", "Binson Echorec"}, 0));
    p.push_back(std::make_unique<APF>(PID{"delay_time",        1}, "Time",         Range{1.f, 2000.f},  450.f));
    p.push_back(std::make_unique<APF>(PID{"delay_feedback",    1}, "Feedback",     Range{0.f, 0.98f},   0.15f));
    p.push_back(std::make_unique<APF>(PID{"delay_mix",         1}, "Delay Mix",    Range{0.f, 1.f},     0.15f));
    p.push_back(std::make_unique<APF>(PID{"delay_lowcut",      1}, "Delay LoCut",  Range{20.f, 500.f},  80.f));
    p.push_back(std::make_unique<APF>(PID{"delay_highcut",     1}, "Delay HiCut",  Range{1000.f, 20000.f}, 8000.f));
    p.push_back(std::make_unique<APF>(PID{"delay_stereowidth", 1}, "Stereo Width", Range{0.f, 1.f},     0.5f));
    p.push_back(std::make_unique<APF>(PID{"delay_wow",         1}, "Wow",          Range{0.f, 0.05f},   0.003f));
    p.push_back(std::make_unique<APF>(PID{"delay_flutter",     1}, "Flutter",      Range{0.f, 0.02f},   0.001f));
    p.push_back(std::make_unique<APF>(PID{"delay_sat",         1}, "Tape Sat",     Range{0.f, 1.f},     0.3f));
    p.push_back(std::make_unique<APF>(PID{"delay_tapeage",     1}, "Tape Age",     Range{0.f, 1.f},     0.5f));
    p.push_back(std::make_unique<APB>(PID{"delay_head1",       1}, "Head 1",       true));
    p.push_back(std::make_unique<APB>(PID{"delay_head2",       1}, "Head 2",       true));
    p.push_back(std::make_unique<APB>(PID{"delay_head3",       1}, "Head 3",       true));
    p.push_back(std::make_unique<APB>(PID{"delay_head4",       1}, "Head 4",       true));
    p.push_back(std::make_unique<APF>(PID{"delay_noise",       1}, "Noise",        Range{0.f, 1.f},     0.0f));

    // ── Reverb ────────────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"reverb_bypass",   1}, "Reverb Bypass", false));
    p.push_back(std::make_unique<APF>(PID{"reverb_predelay", 1}, "Pre-Delay", Range{0.f, 100.f},    5.f));
    p.push_back(std::make_unique<APF>(PID{"reverb_decay",    1}, "Decay",     Range{0.1f, 10.f},    0.8f));
    p.push_back(std::make_unique<APF>(PID{"reverb_damping",  1}, "Damping",   Range{0.f, 0.99f},    0.65f));
    p.push_back(std::make_unique<APF>(PID{"reverb_moddepth", 1}, "Mod Depth", Range{0.f, 1.f},      0.2f));
    p.push_back(std::make_unique<APF>(PID{"reverb_modrate",  1}, "Mod Rate",  Range{0.01f, 8.f},    0.5f));
    p.push_back(std::make_unique<APF>(PID{"reverb_mix",      1}, "Reverb Mix",Range{0.f, 1.f},      0.2f));

    // ── Speaker EQ ────────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"eq_bypass",  1}, "EQ Bypass", false));
    p.push_back(std::make_unique<APF>(PID{"eq_b1_freq", 1}, "EQ B1 Freq", Range{  20.f,  500.f},  100.f));
    p.push_back(std::make_unique<APF>(PID{"eq_b1_gain", 1}, "EQ B1 Gain", Range{ -15.f,   15.f},    0.f));
    p.push_back(std::make_unique<APF>(PID{"eq_b2_freq", 1}, "EQ B2 Freq", Range{  80.f,  800.f},  300.f));
    p.push_back(std::make_unique<APF>(PID{"eq_b2_gain", 1}, "EQ B2 Gain", Range{ -15.f,   15.f},    0.f));
    p.push_back(std::make_unique<APF>(PID{"eq_b2_q",    1}, "EQ B2 Q",    Range{   0.3f,   4.f},    1.f));
    p.push_back(std::make_unique<APF>(PID{"eq_b3_freq", 1}, "EQ B3 Freq", Range{ 200.f, 4000.f}, 1000.f));
    p.push_back(std::make_unique<APF>(PID{"eq_b3_gain", 1}, "EQ B3 Gain", Range{ -15.f,   15.f},    0.f));
    p.push_back(std::make_unique<APF>(PID{"eq_b3_q",    1}, "EQ B3 Q",    Range{   0.3f,   4.f},    1.f));
    p.push_back(std::make_unique<APF>(PID{"eq_b4_freq", 1}, "EQ B4 Freq", Range{ 800.f,12000.f}, 4000.f));
    p.push_back(std::make_unique<APF>(PID{"eq_b4_gain", 1}, "EQ B4 Gain", Range{ -15.f,   15.f},    0.f));
    p.push_back(std::make_unique<APF>(PID{"eq_b4_q",    1}, "EQ B4 Q",    Range{   0.3f,   4.f},    1.f));
    p.push_back(std::make_unique<APF>(PID{"eq_b5_freq", 1}, "EQ B5 Freq", Range{1000.f,16000.f}, 8000.f));
    p.push_back(std::make_unique<APF>(PID{"eq_b5_gain", 1}, "EQ B5 Gain", Range{ -15.f,   15.f},    0.f));

    // ── Compressor ────────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"comp_bypass",    1}, "Comp Bypass", false));
    p.push_back(std::make_unique<APC>(PID{"comp_type",      1}, "Comp Type",
        juce::StringArray{"VCA", "1176"}, 0));
    p.push_back(std::make_unique<APF>(PID{"comp_threshold", 1}, "Threshold", Range{-60.f, 0.f}, -30.0f));
    p.push_back(std::make_unique<APC>(PID{"comp_ratio",     1}, "Ratio",
        juce::StringArray{"2:1", "4:1", "8:1", "20:1", "Limit"}, 1));
    p.push_back(std::make_unique<APF>(PID{"comp_attack",    1}, "Attack",  Range{0.f, 10.f}, 6.0f));
    p.push_back(std::make_unique<APF>(PID{"comp_release",   1}, "Release", Range{0.f, 10.f}, 6.0f));
    p.push_back(std::make_unique<APF>(PID{"comp_knee",      1}, "Knee",    Range{0.f, 10.f}, 6.0f));
    p.push_back(std::make_unique<APF>(PID{"comp_makeup",    1}, "Makeup",  Range{0.f, 10.f}, 4.0f));

    // ── Output ────────────────────────────────────────────────────────────────
    p.push_back(std::make_unique<APF>(PID{"output_level", 1}, "Output Level",
        Range{-60.f, 0.f}, -6.0f));

    return { p.begin(), p.end() };
}

// ─── Constructor ───────────────────────────────────────────────────────────────
GuitarAmpAudioProcessor::GuitarAmpAudioProcessor()
    : AudioProcessor(getBusesProps()),
      apvts(*this, nullptr, "Parameters", createParamLayout())
{
    cacheRawParamPointers();
}

void GuitarAmpAudioProcessor::cacheRawParamPointers() {
    auto grab = [&](const char* id) {
        rawParams[id] = apvts.getRawParameterValue(id);
    };
    grab("gate_threshold"); grab("gate_attack"); grab("gate_release");
    grab("gate_hold");      grab("gate_hysteresis");
    grab("pitch_mode"); grab("pitch_expression"); grab("pitch_mix"); grab("pitch_cents");
    grab("drive_model");
    grab("drive_drive");    grab("drive_tone");  grab("drive_level"); grab("drive_mix");
    grab("drive_octave");
    grab("amp_model");
    grab("amp_gain");  grab("amp_bass"); grab("amp_mid");  grab("amp_treble");
    grab("amp_presence"); grab("amp_master"); grab("amp_sag"); grab("amp_nam_gain");
    grab("sunn_vol1");    grab("sunn_bass1");   grab("sunn_mid1");   grab("sunn_treble1");
    grab("sunn_bright");
    grab("sunn_vol2");    grab("sunn_bass2");   grab("sunn_mid2");   grab("sunn_treble2");
    grab("sunn_bright2"); grab("sunn_input_pad"); grab("sunn_channel_link");
    grab("evh_channel"); grab("evh_resonance");
    grab("rvb_channel"); grab("rvb_gain");   grab("rvb_bass");
    grab("rvb_mid");     grab("rvb_treble"); grab("rvb_master"); grab("rvb_sag");
    grab("pamp_tube");     grab("pamp_presence"); grab("pamp_depth");
    grab("pamp_sag");      grab("pamp_master");   grab("pamp_nfb");
    grab("pamp_resonance"); grab("pamp_airfeel");
    grab("cab_lowcut"); grab("cab_highcut"); grab("cab_mix");
    grab("mod_type");
    grab("mod_rate");  grab("mod_depth"); grab("mod_mix");
    grab("mod_stereowidth"); grab("mod_preamp");
    grab("mod_vibratomode"); grab("mod_outlevel");
    grab("delay_mode");     grab("delay_time");    grab("delay_feedback");
    grab("delay_mix");      grab("delay_lowcut");  grab("delay_highcut");
    grab("delay_stereowidth"); grab("delay_wow");  grab("delay_flutter");
    grab("delay_sat");      grab("delay_tapeage");
    grab("delay_head1");    grab("delay_head2"); grab("delay_head3"); grab("delay_head4");
    grab("delay_noise");
    grab("reverb_predelay"); grab("reverb_decay"); grab("reverb_damping");
    grab("reverb_moddepth"); grab("reverb_modrate"); grab("reverb_mix");
    grab("eq_b1_freq"); grab("eq_b1_gain");
    grab("eq_b2_freq"); grab("eq_b2_gain"); grab("eq_b2_q");
    grab("eq_b3_freq"); grab("eq_b3_gain"); grab("eq_b3_q");
    grab("eq_b4_freq"); grab("eq_b4_gain"); grab("eq_b4_q");
    grab("eq_b5_freq"); grab("eq_b5_gain");
    grab("comp_type"); grab("comp_threshold"); grab("comp_ratio");
    grab("comp_attack"); grab("comp_release"); grab("comp_knee"); grab("comp_makeup");
    grab("output_level");
}

// ─── Prepare / Process ────────────────────────────────────────────────────────
void GuitarAmpAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;
    dsp.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    outputLimiter_.prepare(sampleRate, getTotalNumOutputChannels());

    // Install a synthetic Greenback-style default IR if the user hasn't loaded one.
    // Re-generated at each prepare() so the frequency response stays accurate
    // when the host changes sample rate.
    if (!currentIRFile.existsAsFile()) {
        auto defaultIR = DefaultCabIR::generate(sampleRate);
        dsp.loadIR(defaultIR);
    }
}

// Helper: read a named atomic<float>* pointer; 0.0f if not cached.
static inline float rv(const std::unordered_map<juce::String, std::atomic<float>*>& m,
                        const char* key) {
    auto it = m.find(key);
    return it != m.end() ? it->second->load() : 0.0f;
}

void GuitarAmpAudioProcessor::syncDSPParameters() {
    // ── Bypass states (forced off when effect removed from chain) ────────────
    // groupIdx: 0=Gate,1=Pitch,2=Drive,3=Amp+PA,4=Cab,5=Mod,6=Delay,7=Reverb,8=EQ,9=Comp
    auto bypass = [&](const char* id, int blockIdx, int groupIdx) {
        bool forced = (groupIdx >= 0 && groupIdx < 10) ? !effectActive_[groupIdx] : false;
        auto* p = apvts.getRawParameterValue(id);
        if (auto* b = dsp.getSignalChain().getBlock(blockIdx))
            b->setBypass(forced || (p && *p > 0.5f));
    };
    bypass("gate_bypass",   0, 0);
    bypass("pitch_bypass",  1, 1);
    bypass("drive_bypass",  2, 2);
    bypass("amp_bypass",    3, 3);
    bypass("pamp_bypass",   4, 3); // power amp follows amp group
    bypass("cab_bypass",    5, 4);
    bypass("mod_bypass",    6, 5);
    bypass("delay_bypass",  7, 6);
    bypass("reverb_bypass", 8, 7);
    bypass("eq_bypass",     9, 8);
    bypass("comp_bypass",  10, 9);

    // ── Gate ─────────────────────────────────────────────────────────────────
    dsp.setParameter("gate.threshold",  rv(rawParams, "gate_threshold"));
    dsp.setParameter("gate.attack",     rv(rawParams, "gate_attack"));
    dsp.setParameter("gate.release",    rv(rawParams, "gate_release"));
    dsp.setParameter("gate.hold",       rv(rawParams, "gate_hold"));
    dsp.setParameter("gate.hysteresis", rv(rawParams, "gate_hysteresis"));

    // ── Pitch (Whammy) ────────────────────────────────────────────────────────
    dsp.setParameter("pitch.mode",       rv(rawParams, "pitch_mode"));
    dsp.setParameter("pitch.expression", rv(rawParams, "pitch_expression"));
    dsp.setParameter("pitch.mix",        rv(rawParams, "pitch_mix"));
    dsp.setParameter("pitch.cents",      rv(rawParams, "pitch_cents"));

    // ── Drive model (enum — only update on change) ────────────────────────────
    {
        const int driveModelIdx = static_cast<int>(rv(rawParams, "drive_model"));
        if (driveModelIdx != lastOverdriveType) {
            dsp.selectOverdriveModel(OverdriveFactory::fromIndex(driveModelIdx));
            lastOverdriveType = driveModelIdx;
        }
    }
    dsp.setParameter("drive.drive",  rv(rawParams, "drive_drive"));
    dsp.setParameter("drive.tone",   rv(rawParams, "drive_tone"));
    dsp.setParameter("drive.level",  rv(rawParams, "drive_level"));
    dsp.setParameter("drive.mix",    rv(rawParams, "drive_mix"));
    dsp.setParameter("drive.octave", rv(rawParams, "drive_octave"));

    // ── Amp model (enum — only update on change) ──────────────────────────────
    const int ampModelIdx = static_cast<int>(rv(rawParams, "amp_model"));
    if (ampModelIdx != lastAmpModel) {
        dsp.selectAmpModel(static_cast<AmpModel>(ampModelIdx));
        lastAmpModel = ampModelIdx;

        // Snap power-amp knobs to per-amp calibrated defaults.
        pendingPADefaults.store(ampModelIdx);
        triggerAsyncUpdate();

        int recommendedTube = -1;
        switch (ampModelIdx) {
            case 0: dsp.selectTubeType(TubeType::Tube_6L6GC); recommendedTube = 0; break; // Fender Deluxe
            case 1: dsp.selectTubeType(TubeType::Tube_EL34);  recommendedTube = 1; break; // Marshall JCM800
            case 2: dsp.selectTubeType(TubeType::Tube_EL34);  recommendedTube = 1; break; // EVH 5150 III
            case 4: dsp.selectTubeType(TubeType::Tube_6L6GC); recommendedTube = 0; break; // Sunn Model T (6550≈6L6GC)
            case 5: dsp.selectTubeType(TubeType::Tube_EL34);  recommendedTube = 1; break; // Orange Rockerverb 50
            default: break; // NAM (3): keep current tube
        }

        if (recommendedTube >= 0) {
            lastTubeType = recommendedTube;
            pendingTubeUpdate.store(recommendedTube);
            // triggerAsyncUpdate() already called above
        }
    }
    // Model-specific params
    dsp.setParameter("evh.channel",       rv(rawParams, "evh_channel"));
    dsp.setParameter("evh.resonance",     rv(rawParams, "evh_resonance"));
    dsp.setParameter("rvb.channel",       rv(rawParams, "rvb_channel"));
    dsp.setParameter("sunn.channel_link", rv(rawParams, "sunn_channel_link"));
    dsp.setParameter("sunn.vol1",         rv(rawParams, "sunn_vol1"));
    dsp.setParameter("sunn.vol2",         rv(rawParams, "sunn_vol2"));
    dsp.setParameter("sunn.bright",       rv(rawParams, "sunn_bright"));
    dsp.setParameter("sunn.bright2",      rv(rawParams, "sunn_bright2"));
    dsp.setParameter("sunn.input_pad",    rv(rawParams, "sunn_input_pad"));

    // Presence and NAM gain are always shared.
    dsp.setParameter("amp.presence", rv(rawParams, "amp_presence"));
    dsp.setParameter("amp.namGain",  rv(rawParams, "amp_nam_gain"));

    if (ampModelIdx == 4) {
        // Sunn Model T: shared gain/master/sag, dedicated per-channel tonestack
        dsp.setParameter("amp.gain",     rv(rawParams, "amp_gain"));
        dsp.setParameter("amp.master",   rv(rawParams, "amp_master"));
        dsp.setParameter("amp.sag",      rv(rawParams, "amp_sag"));
        dsp.setParameter("sunn.bass1",   rv(rawParams, "sunn_bass1"));
        dsp.setParameter("sunn.mid1",    rv(rawParams, "sunn_mid1"));
        dsp.setParameter("sunn.treble1", rv(rawParams, "sunn_treble1"));
        dsp.setParameter("sunn.bass2",   rv(rawParams, "sunn_bass2"));
        dsp.setParameter("sunn.mid2",    rv(rawParams, "sunn_mid2"));
        dsp.setParameter("sunn.treble2", rv(rawParams, "sunn_treble2"));
    } else if (ampModelIdx == 5) {
        // Rockerverb 50: all dedicated knobs (presence still shared above)
        dsp.setParameter("rvb.gain",   rv(rawParams, "rvb_gain"));
        dsp.setParameter("rvb.bass",   rv(rawParams, "rvb_bass"));
        dsp.setParameter("rvb.mid",    rv(rawParams, "rvb_mid"));
        dsp.setParameter("rvb.treble", rv(rawParams, "rvb_treble"));
        dsp.setParameter("rvb.master", rv(rawParams, "rvb_master"));
        dsp.setParameter("rvb.sag",    rv(rawParams, "rvb_sag"));
    } else {
        // All other models: shared knobs
        dsp.setParameter("amp.gain",   rv(rawParams, "amp_gain"));
        dsp.setParameter("amp.bass",   rv(rawParams, "amp_bass"));
        dsp.setParameter("amp.mid",    rv(rawParams, "amp_mid"));
        dsp.setParameter("amp.treble", rv(rawParams, "amp_treble"));
        dsp.setParameter("amp.master", rv(rawParams, "amp_master"));
        dsp.setParameter("amp.sag",    rv(rawParams, "amp_sag"));
    }

    // ── Power amp tube type (enum — only update on change) ────────────────────
    {
        const int tubeIdx = static_cast<int>(rv(rawParams, "pamp_tube"));
        if (tubeIdx != lastTubeType) {
            dsp.selectTubeType(static_cast<TubeType>(tubeIdx));
            lastTubeType = tubeIdx;
        }
    }
    dsp.setParameter("pamp.presence",  rv(rawParams, "pamp_presence"));
    dsp.setParameter("pamp.depth",     rv(rawParams, "pamp_depth"));
    dsp.setParameter("pamp.sag",       rv(rawParams, "pamp_sag"));
    dsp.setParameter("pamp.master",    rv(rawParams, "pamp_master"));
    dsp.setParameter("pamp.nfb",       rv(rawParams, "pamp_nfb"));
    dsp.setParameter("pamp.resonance", rv(rawParams, "pamp_resonance"));
    dsp.setParameter("pamp.airFeel",   rv(rawParams, "pamp_airfeel"));

    // ── Cabinet ───────────────────────────────────────────────────────────────
    dsp.setParameter("cab.lowCutHz",  rv(rawParams, "cab_lowcut"));
    dsp.setParameter("cab.highCutHz", rv(rawParams, "cab_highcut"));
    dsp.setParameter("cab.mix",       rv(rawParams, "cab_mix"));

    // ── Modulation type ───────────────────────────────────────────────────────
    {
        const int modTypeIdx = static_cast<int>(rv(rawParams, "mod_type"));
        if (modTypeIdx != lastModType) {
            dsp.selectModulationType(ModulationFactory::fromIndex(modTypeIdx));
            lastModType = modTypeIdx;
        }
    }
    dsp.setParameter("mod.rate",        rv(rawParams, "mod_rate"));
    dsp.setParameter("mod.depth",       rv(rawParams, "mod_depth"));
    dsp.setParameter("mod.mix",         rv(rawParams, "mod_mix"));
    dsp.setParameter("mod.stereoWidth", rv(rawParams, "mod_stereowidth"));
    dsp.setParameter("mod.preampOn",    rv(rawParams, "mod_preamp"));
    dsp.setParameter("mod.mode",        rv(rawParams, "mod_vibratomode"));
    dsp.setParameter("mod.outputLevel", rv(rawParams, "mod_outlevel"));

    // ── Delay type ────────────────────────────────────────────────────────────
    const int delayTypeIdx = static_cast<int>(rv(rawParams, "delay_mode"));
    if (delayTypeIdx != lastDelayType) {
        dsp.setDelayType(DelayFactory::fromIndex(delayTypeIdx));
        lastDelayType = delayTypeIdx;
    }
    dsp.setParameter("delay.timeMs",       rv(rawParams, "delay_time"));
    dsp.setParameter("delay.feedback",     rv(rawParams, "delay_feedback"));
    dsp.setParameter("delay.mix",          rv(rawParams, "delay_mix"));
    dsp.setParameter("delay.lowCutHz",     rv(rawParams, "delay_lowcut"));
    dsp.setParameter("delay.highCutHz",    rv(rawParams, "delay_highcut"));
    dsp.setParameter("delay.stereoWidth",  rv(rawParams, "delay_stereowidth"));
    dsp.setParameter("delay.wowDepth",     rv(rawParams, "delay_wow"));
    dsp.setParameter("delay.flutterDepth", rv(rawParams, "delay_flutter"));
    dsp.setParameter("delay.saturation",   rv(rawParams, "delay_sat"));
    dsp.setParameter("delay.tapeAge",      rv(rawParams, "delay_tapeage"));
    {
        int mask = 0;
        if (rv(rawParams, "delay_head1") > 0.5f) mask |= 1;
        if (rv(rawParams, "delay_head2") > 0.5f) mask |= 2;
        if (rv(rawParams, "delay_head3") > 0.5f) mask |= 4;
        if (rv(rawParams, "delay_head4") > 0.5f) mask |= 8;
        dsp.setParameter("delay.headMask",   static_cast<float>(mask));
    }
    dsp.setParameter("delay.noiseLevel",   rv(rawParams, "delay_noise"));

    // ── Reverb ────────────────────────────────────────────────────────────────
    dsp.setParameter("reverb.preDelayMs", rv(rawParams, "reverb_predelay"));
    dsp.setParameter("reverb.decayTime",  rv(rawParams, "reverb_decay"));
    dsp.setParameter("reverb.damping",    rv(rawParams, "reverb_damping"));
    dsp.setParameter("reverb.modDepth",   rv(rawParams, "reverb_moddepth"));
    dsp.setParameter("reverb.modRate",    rv(rawParams, "reverb_modrate"));
    dsp.setParameter("reverb.mix",        rv(rawParams, "reverb_mix"));

    // ── Compressor ────────────────────────────────────────────────────────────
    dsp.setParameter("comp.type",      rv(rawParams, "comp_type"));
    dsp.setParameter("comp.threshold", rv(rawParams, "comp_threshold"));
    dsp.setParameter("comp.ratio",     rv(rawParams, "comp_ratio"));
    dsp.setParameter("comp.attack",    rv(rawParams, "comp_attack"));
    dsp.setParameter("comp.release",   rv(rawParams, "comp_release"));
    dsp.setParameter("comp.knee",      rv(rawParams, "comp_knee"));
    dsp.setParameter("comp.makeup",    rv(rawParams, "comp_makeup"));

    // ── Speaker EQ ────────────────────────────────────────────────────────────
    dsp.setParameter("eq.b1.freq", rv(rawParams, "eq_b1_freq"));
    dsp.setParameter("eq.b1.gain", rv(rawParams, "eq_b1_gain"));
    dsp.setParameter("eq.b2.freq", rv(rawParams, "eq_b2_freq"));
    dsp.setParameter("eq.b2.gain", rv(rawParams, "eq_b2_gain"));
    dsp.setParameter("eq.b2.q",    rv(rawParams, "eq_b2_q"));
    dsp.setParameter("eq.b3.freq", rv(rawParams, "eq_b3_freq"));
    dsp.setParameter("eq.b3.gain", rv(rawParams, "eq_b3_gain"));
    dsp.setParameter("eq.b3.q",    rv(rawParams, "eq_b3_q"));
    dsp.setParameter("eq.b4.freq", rv(rawParams, "eq_b4_freq"));
    dsp.setParameter("eq.b4.gain", rv(rawParams, "eq_b4_gain"));
    dsp.setParameter("eq.b4.q",    rv(rawParams, "eq_b4_q"));
    dsp.setParameter("eq.b5.freq", rv(rawParams, "eq_b5_freq"));
    dsp.setParameter("eq.b5.gain", rv(rawParams, "eq_b5_gain"));
}

void GuitarAmpAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& /*midi*/) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const double blockDurationSec = (double)numSamples / currentSampleRate;

    // Meter the dry input
    inputLevelL.store(buffer.getRMSLevel(0, 0, numSamples));
    inputLevelR.store(numChannels > 1 ? buffer.getRMSLevel(1, 0, numSamples) : 0.0f);

    // Sync all DSP parameters from APVTS (atomic reads, no locks)
    syncDSPParameters();

    const auto t0 = juce::Time::getHighResolutionTicks();

    // Process through signal chain
    float** io = const_cast<float**>(buffer.getArrayOfWritePointers());
    dsp.processBlock(io, io, numSamples, numChannels, currentSampleRate);

    // Apply output level (dB → linear)
    const float outLvl = rv(rawParams, "output_level");
    buffer.applyGain(std::pow(10.0f, outLvl * 0.05f));

    // Brickwall limiter — guarantees output never exceeds 0 dBFS
    outputLimiter_.process(const_cast<float**>(buffer.getArrayOfWritePointers()),
                           numSamples, numChannels);

    // CPU load estimate (smooth with leaky integrator)
    const double elapsed = juce::Time::highResolutionTicksToSeconds(
        juce::Time::getHighResolutionTicks() - t0);
    const float newLoad = (float)(elapsed / blockDurationSec);
    cpuLoad.store(cpuLoad.load() * 0.9f + newLoad * 0.1f);

    // Meter the wet output
    outputLevelL.store(buffer.getRMSLevel(0, 0, numSamples));
    outputLevelR.store(numChannels > 1 ? buffer.getRMSLevel(1, 0, numSamples) : 0.0f);
}

// ─── Async UI updates (tube type + power-amp defaults) ───────────────────────
void GuitarAmpAudioProcessor::handleAsyncUpdate() {
    const int tube = pendingTubeUpdate.exchange(-1);
    if (tube >= 0)
        if (auto* p = apvts.getParameter("pamp_tube"))
            p->setValueNotifyingHost(static_cast<float>(tube) / 3.0f);

    const int paModel = pendingPADefaults.exchange(-1);
    if (paModel >= 0) {
        const auto d = PowerAmpProcessor::getDefaultsForModel(paModel);
        struct { const char* id; float val; } snap[] = {
            { "pamp_master",   d.master   },
            { "pamp_presence", d.presence },
            { "pamp_depth",    d.depth    },
            { "pamp_nfb",      d.nfb      },
            { "pamp_sag",      d.sag      },
        };
        for (auto& s : snap)
            if (auto* p = apvts.getParameter(s.id))
                p->setValueNotifyingHost(s.val);
    }
}

// ─── State persistence ────────────────────────────────────────────────────────
void GuitarAmpAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    if (currentIRFile.existsAsFile())
        state.setProperty("irFile",  currentIRFile.getFullPathName(),  nullptr);
    if (currentNamFile.existsAsFile())
        state.setProperty("namFile", currentNamFile.getFullPathName(), nullptr);
    if (currentOverdriveNamFile.existsAsFile())
        state.setProperty("overdriveNamFile", currentOverdriveNamFile.getFullPathName(), nullptr);
    {
        auto order = dsp.getLogicalOrder();
        juce::String s;
        for (int i = 0; i < 10; ++i) { if (i) s += ","; s += juce::String(order[i]); }
        state.setProperty("chainOrder", s, nullptr);
    }
    {
        juce::String s;
        for (int i = 0; i < 10; ++i) { if (i) s += ","; s += effectActive_[i] ? "1" : "0"; }
        state.setProperty("effectActive", s, nullptr);
    }
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void GuitarAmpAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml(*xml);
    const juce::String irPath           = tree.getProperty("irFile",           "").toString();
    const juce::String namPath          = tree.getProperty("namFile",          "").toString();
    const juce::String overdriveNamPath = tree.getProperty("overdriveNamFile", "").toString();
    const juce::String chainOrderStr    = tree.getProperty("chainOrder",        "").toString();

    apvts.replaceState(tree);

    if (irPath.isNotEmpty()) {
        juce::File f(irPath);
        if (f.existsAsFile()) loadIR(f);
    }
    if (namPath.isNotEmpty()) {
        juce::File f(namPath);
        if (f.existsAsFile()) loadNeuralModel(f);
    }
    if (overdriveNamPath.isNotEmpty()) {
        juce::File f(overdriveNamPath);
        if (f.existsAsFile()) loadOverdriveNam(f);
    }

    // Restore chain order (or reset to default if property absent/invalid).
    {
        bool loaded = false;
        if (chainOrderStr.isNotEmpty()) {
            auto tokens = juce::StringArray::fromTokens(chainOrderStr, ",", "");
            if (tokens.size() == 10) {
                std::vector<int> order;
                bool valid = true;
                for (const auto& t : tokens) {
                    int v = t.trim().getIntValue();
                    if (v < 0 || v > 9) { valid = false; break; }
                    order.push_back(v);
                }
                if (valid) { dsp.setLogicalOrder(order); loaded = true; }
            }
        }
        if (!loaded) dsp.resetLogicalOrder();
    }

    // Restore effectActive
    {
        const juce::String s = tree.getProperty("effectActive", "").toString();
        if (s.isNotEmpty()) {
            auto tokens = juce::StringArray::fromTokens(s, ",", "");
            if (tokens.size() == 10)
                for (int i = 0; i < 10; ++i)
                    effectActive_[i] = (tokens[i].trim() == "1");
        }
    }

    if (onFilesRestored)
        juce::MessageManager::callAsync([this] { if (onFilesRestored) onFilesRestored(); });
    if (onOverdriveNamRestored)
        juce::MessageManager::callAsync([this] { if (onOverdriveNamRestored) onOverdriveNamRestored(); });
    if (onChainReordered)
        juce::MessageManager::callAsync([this] { if (onChainReordered) onChainReordered(); });
    if (onEffectActiveChanged)
        juce::MessageManager::callAsync([this] { if (onEffectActiveChanged) onEffectActiveChanged(); });
}

// ─── IR loading ───────────────────────────────────────────────────────────────
void GuitarAmpAudioProcessor::loadIR(const juce::File& irFile) {
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(irFile));
    if (!reader) return;

    const int numSamps = (int)reader->lengthInSamples;
    juce::AudioBuffer<float> buf(reader->numChannels, numSamps);
    reader->read(&buf, 0, numSamps, 0, true, true);

    // Decode everything before suspending so the audio gap is as short as possible.
    std::vector<float> irL(buf.getReadPointer(0), buf.getReadPointer(0) + numSamps);
    const bool hasStereo = reader->numChannels > 1;
    std::vector<float> irR;
    if (hasStereo)
        irR.assign(buf.getReadPointer(1), buf.getReadPointer(1) + numSamps);

    // suspendProcessing acquires the callback lock, guaranteeing the audio
    // thread is not inside processBlock when setIR/resizeHistory mutate the
    // IR and history buffers.
    suspendProcessing(true);
    dsp.loadIR(irL, hasStereo ? &irR : nullptr);
    currentIRFile = irFile;
    suspendProcessing(false);
}

// ─── Neural model loading ─────────────────────────────────────────────────────
void GuitarAmpAudioProcessor::loadNeuralModel(const juce::File& f) {
    if (!f.existsAsFile()) return;

    suspendProcessing(true);
    const bool ok = dsp.loadNeuralModel(f.getFullPathName().toStdString());
    if (ok) currentNamFile = f;
    suspendProcessing(false);
}

void GuitarAmpAudioProcessor::loadOverdriveNam(const juce::File& f) {
    if (!f.existsAsFile()) return;
    suspendProcessing(true);
    const bool ok = dsp.loadOverdriveNam(f.getFullPathName().toStdString());
    if (ok) currentOverdriveNamFile = f;
    suspendProcessing(false);
}

void GuitarAmpAudioProcessor::reorderBlock(int fromPos, int toPos) {
    if (fromPos == toPos || fromPos < 0 || toPos < 0 ||
        fromPos >= 10  || toPos >= 10) return;
    suspendProcessing(true);
    dsp.reorderLogical(fromPos, toPos);
    suspendProcessing(false);
    if (onChainReordered)
        juce::MessageManager::callAsync([this] { if (onChainReordered) onChainReordered(); });
}

std::vector<int> GuitarAmpAudioProcessor::getLogicalOrder() const {
    return dsp.getLogicalOrder();
}

void GuitarAmpAudioProcessor::setChainOrder(const std::vector<int>& order) {
    if (order.size() != 10) return;
    suspendProcessing(true);
    dsp.setLogicalOrder(order);
    suspendProcessing(false);
    if (onChainReordered)
        juce::MessageManager::callAsync([this] { if (onChainReordered) onChainReordered(); });
}

// ─── Effect active state ──────────────────────────────────────────────────────
bool GuitarAmpAudioProcessor::getEffectActive(int origIdx) const {
    if (origIdx < 0 || origIdx >= 10) return true;
    return effectActive_[origIdx];
}

void GuitarAmpAudioProcessor::setEffectActive(int origIdx, bool active) {
    if (origIdx < 0 || origIdx >= 10) return;
    effectActive_[origIdx] = active;
    if (onEffectActiveChanged)
        juce::MessageManager::callAsync([this] { if (onEffectActiveChanged) onEffectActiveChanged(); });
}

// ─── Editor ───────────────────────────────────────────────────────────────────
juce::AudioProcessorEditor* GuitarAmpAudioProcessor::createEditor() {
    return new GuitarAmpEditor(*this);
}

// ─── JUCE plugin entry point ──────────────────────────────────────────────────
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new GuitarAmpAudioProcessor();
}
