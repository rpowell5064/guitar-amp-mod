// ═══════════════════════════════════════════════════════════════════════════════
// PluginProcessor additions for SunnModelT and OrangeRockerverb50.
//
// HOW TO APPLY
// ────────────
// 1. In PluginProcessor.cpp :: createParamLayout()
//    Replace the existing amp_model APC entry and add the blocks below.
//
// 2. In PluginProcessor.cpp :: cacheRawParamPointers()
//    Add the grab() calls from section B.
//
// 3. In PluginProcessor.cpp :: syncDSPParameters()
//    Add the forwarding calls from section C.
//
// 4. In AmpBlock.h extend the AmpModel enum (see AmpBlockExtended.h).
//
// 5. In GuitarAmpProcessor.cpp replace AmpBlock with AmpBlockExtended
//    and add the "sunn." / "rvb." parameter registrations.
// ═══════════════════════════════════════════════════════════════════════════════

#include "PluginProcessor.h"

// ── A. APVTS parameter layout additions ──────────────────────────────────────
// Paste into createParamLayout() REPLACING the existing amp_model line and
// adding the two new blocks after the existing amp block entries.

static void addNewAmpParams(std::vector<std::unique_ptr<juce::RangedAudioParameter>>& p) {
    using PID   = juce::ParameterID;
    using APF   = juce::AudioParameterFloat;
    using APB   = juce::AudioParameterBool;
    using APC   = juce::AudioParameterChoice;
    using Range = juce::NormalisableRange<float>;

    // ── Extend amp_model choice to include the two new amps (indices 4, 5) ──
    // Replace the existing APC{"amp_model", ...} line with:
    p.push_back(std::make_unique<APC>(PID{"amp_model", 1}, "Amp Model",
        juce::StringArray{
            "Fender Deluxe",          // 0
            "Marshall JCM800",        // 1
            "EVH 5150 III",           // 2
            "Neural",                 // 3
            "Sunn Model T",           // 4
            "Orange Rockerverb 50",   // 5
        }, 1));

    // ── Sunn Model T parameters ────────────────────────────────────────────
    p.push_back(std::make_unique<APF>(PID{"sunn_gain",   1}, "Sunn Gain",   Range{0.f, 1.f}, 0.5f));
    p.push_back(std::make_unique<APF>(PID{"sunn_bass",   1}, "Sunn Bass",   Range{0.f, 1.f}, 0.5f));
    p.push_back(std::make_unique<APF>(PID{"sunn_mid",    1}, "Sunn Mid",    Range{0.f, 1.f}, 0.6f));
    p.push_back(std::make_unique<APF>(PID{"sunn_treble", 1}, "Sunn Treble", Range{0.f, 1.f}, 0.5f));
    p.push_back(std::make_unique<APF>(PID{"sunn_master", 1}, "Sunn Master", Range{0.f, 1.f}, 0.7f));
    p.push_back(std::make_unique<APF>(PID{"sunn_sag",    1}, "Sunn Sag",    Range{0.f, 1.f}, 0.3f));
    p.push_back(std::make_unique<APB>(PID{"sunn_bright", 1}, "Sunn Bright", false));

    // ── Orange Rockerverb 50 MKII parameters ──────────────────────────────
    p.push_back(std::make_unique<APB>(PID{"rvb_channel", 1}, "RVB Channel", false)); // false=dirty
    p.push_back(std::make_unique<APF>(PID{"rvb_gain",    1}, "RVB Gain",    Range{0.f, 1.f}, 0.5f));
    p.push_back(std::make_unique<APF>(PID{"rvb_bass",    1}, "RVB Bass",    Range{0.f, 1.f}, 0.5f));
    p.push_back(std::make_unique<APF>(PID{"rvb_mid",     1}, "RVB Mid",     Range{0.f, 1.f}, 0.5f));
    p.push_back(std::make_unique<APF>(PID{"rvb_treble",  1}, "RVB Treble",  Range{0.f, 1.f}, 0.5f));
    p.push_back(std::make_unique<APF>(PID{"rvb_master",  1}, "RVB Master",  Range{0.f, 1.f}, 0.7f));
    p.push_back(std::make_unique<APF>(PID{"rvb_sag",     1}, "RVB Sag",     Range{0.f, 1.f}, 0.3f));
}

// ── B. cacheRawParamPointers() additions ─────────────────────────────────────
// Append these grab() calls at the end of cacheRawParamPointers():
//
//   grab("sunn_gain");   grab("sunn_bass");  grab("sunn_mid");
//   grab("sunn_treble"); grab("sunn_master"); grab("sunn_sag");
//   grab("sunn_bright");
//   grab("rvb_channel"); grab("rvb_gain");   grab("rvb_bass");
//   grab("rvb_mid");     grab("rvb_treble"); grab("rvb_master");
//   grab("rvb_sag");

// ── C. syncDSPParameters() additions ─────────────────────────────────────────
// Inside syncDSPParameters(), replace the amp_model block with this version:
//
//   const int ampModelIdx = static_cast<int>(rv(rawParams, "amp_model"));
//   if (ampModelIdx != lastAmpModel) {
//       // Cast safely; AmpBlockExtended handles indices 4 and 5.
//       dsp.selectAmpModel(static_cast<AmpModel>(ampModelIdx));
//       lastAmpModel = ampModelIdx;
//
//       // When switching to a new amp, suggest the matching tube type.
//       if (ampModelIdx == 4) // SunnModelT
//           dsp.selectTubeType(TubeType::Tube_6L6GC);
//       else if (ampModelIdx == 5) // Rockerverb50
//           dsp.selectTubeType(TubeType::Tube_EL34);
//   }
//
//   // Shared knobs remain active for existing models (indices 0-3).
//   dsp.setParameter("amp.gain",     rv(rawParams, "amp_gain"));
//   dsp.setParameter("amp.bass",     rv(rawParams, "amp_bass"));
//   dsp.setParameter("amp.mid",      rv(rawParams, "amp_mid"));
//   dsp.setParameter("amp.treble",   rv(rawParams, "amp_treble"));
//   dsp.setParameter("amp.presence", rv(rawParams, "amp_presence"));
//   dsp.setParameter("amp.master",   rv(rawParams, "amp_master"));
//   dsp.setParameter("amp.sag",      rv(rawParams, "amp_sag"));
//   dsp.setParameter("amp.namGain",  rv(rawParams, "amp_nam_gain"));
//
//   // Sunn Model T parameters (active when amp_model == 4)
//   dsp.setParameter("sunn.gain",   rv(rawParams, "sunn_gain"));
//   dsp.setParameter("sunn.bass",   rv(rawParams, "sunn_bass"));
//   dsp.setParameter("sunn.mid",    rv(rawParams, "sunn_mid"));
//   dsp.setParameter("sunn.treble", rv(rawParams, "sunn_treble"));
//   dsp.setParameter("sunn.master", rv(rawParams, "sunn_master"));
//   dsp.setParameter("sunn.sag",    rv(rawParams, "sunn_sag"));
//   dsp.setParameter("sunn.bright", rv(rawParams, "sunn_bright"));
//
//   // Orange Rockerverb 50 parameters (active when amp_model == 5)
//   dsp.setParameter("rvb.channel", rv(rawParams, "rvb_channel"));
//   dsp.setParameter("rvb.gain",    rv(rawParams, "rvb_gain"));
//   dsp.setParameter("rvb.bass",    rv(rawParams, "rvb_bass"));
//   dsp.setParameter("rvb.mid",     rv(rawParams, "rvb_mid"));
//   dsp.setParameter("rvb.treble",  rv(rawParams, "rvb_treble"));
//   dsp.setParameter("rvb.master",  rv(rawParams, "rvb_master"));
//   dsp.setParameter("rvb.sag",     rv(rawParams, "rvb_sag"));

// ── D. GuitarAmpProcessor.cpp — registerAllParameters() additions ─────────────
// Append after the existing amp.* registrations:
//
//   params.registerParameter("sunn.gain",   0.5f, 0.0f, 1.0f);
//   params.registerParameter("sunn.bass",   0.5f, 0.0f, 1.0f);
//   params.registerParameter("sunn.mid",    0.6f, 0.0f, 1.0f);
//   params.registerParameter("sunn.treble", 0.5f, 0.0f, 1.0f);
//   params.registerParameter("sunn.master", 0.7f, 0.0f, 1.0f);
//   params.registerParameter("sunn.sag",    0.3f, 0.0f, 1.0f);
//   params.registerParameter("sunn.bright", 0.0f, 0.0f, 1.0f);
//   params.registerParameter("rvb.channel", 0.0f, 0.0f, 1.0f);
//   params.registerParameter("rvb.gain",    0.5f, 0.0f, 1.0f);
//   params.registerParameter("rvb.bass",    0.5f, 0.0f, 1.0f);
//   params.registerParameter("rvb.mid",     0.5f, 0.0f, 1.0f);
//   params.registerParameter("rvb.treble",  0.5f, 0.0f, 1.0f);
//   params.registerParameter("rvb.master",  0.7f, 0.0f, 1.0f);
//   params.registerParameter("rvb.sag",     0.3f, 0.0f, 1.0f);

// ── E. GuitarAmpProcessor.cpp — parameter routing callback additions ──────────
// In the lambda passed to params.setChangeCallback(), add:
//
//   else if (!(local = stripPrefix("sunn.")).empty() && amp)
//       amp->setParameter(local, value);
//   else if (!(local = stripPrefix("rvb.")).empty() && amp)
//       amp->setParameter(local, value);
//
// (AmpBlockExtended::setParameter forwards these to the active extModel_.)
