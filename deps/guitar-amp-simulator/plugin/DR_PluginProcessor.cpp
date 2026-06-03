#include "DR_PluginProcessor.h"

// ── APVTS parameter layout ─────────────────────────────────────────────────
juce::AudioProcessorValueTreeState::ParameterLayout
DR_PluginProcessor::createParameterLayout() {
    using Parameter = juce::AudioParameterFloat;
    using Toggle    = juce::AudioParameterBool;
    using NRange    = juce::NormalisableRange<float>;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Volume knob — no Gain knob on the AB763.
    params.push_back(std::make_unique<Parameter>(
        "volume", "Volume",
        NRange(0.0f, 1.0f, 0.001f), 0.5f,
        juce::AudioParameterFloatAttributes().withLabel("").withStringFromValueFunction(
            [](float v, int) { return juce::String(static_cast<int>(v * 10 + 0.5f)); })
    ));

    params.push_back(std::make_unique<Parameter>(
        "bass", "Bass",
        NRange(0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back(std::make_unique<Parameter>(
        "mid", "Middle",
        NRange(0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back(std::make_unique<Parameter>(
        "treble", "Treble",
        NRange(0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back(std::make_unique<Parameter>(
        "reverb", "Reverb",
        NRange(0.0f, 1.0f, 0.001f), 0.25f));

    // Tremolo Speed: display as "1–10" with one decimal.
    params.push_back(std::make_unique<Parameter>(
        "trem_speed", "Tremolo Speed",
        NRange(0.0f, 1.0f, 0.001f), 0.3f,
        juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction(
            [](float v, int) {
                const float hz = 1.0f + v * 9.0f;
                return juce::String(hz, 1);
            })
    ));

    params.push_back(std::make_unique<Parameter>(
        "trem_intensity", "Tremolo Intensity",
        NRange(0.0f, 1.0f, 0.001f), 0.0f));

    // Bright switch: shown as a boolean but stored as float (0/1) in APVTS.
    params.push_back(std::make_unique<Toggle>(
        "bright", "Bright Switch", false));

    // Master volume (non-original — clearly labelled).
    params.push_back(std::make_unique<Parameter>(
        "master", "Master Vol",
        NRange(0.0f, 1.0f, 0.001f), 0.7f));

    params.push_back(std::make_unique<Parameter>(
        "sag", "Sag",
        NRange(0.0f, 1.0f, 0.001f), 0.35f));

    return { params.begin(), params.end() };
}

// ── Constructor ───────────────────────────────────────────────────────────────
DR_PluginProcessor::DR_PluginProcessor()
    : AudioProcessor(BusesProperties()
                        .withInput ("Input",  juce::AudioChannelSet::mono(), true)
                        .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "DR_AB763_STATE", createParameterLayout())
{
    // Cache raw parameter pointers — never queries APVTS in the audio thread.
    pVolume_     = apvts.getRawParameterValue("volume");
    pBass_       = apvts.getRawParameterValue("bass");
    pMid_        = apvts.getRawParameterValue("mid");
    pTreble_     = apvts.getRawParameterValue("treble");
    pReverb_     = apvts.getRawParameterValue("reverb");
    pTremSpeed_  = apvts.getRawParameterValue("trem_speed");
    pTremIntens_ = apvts.getRawParameterValue("trem_intensity");
    pBright_     = apvts.getRawParameterValue("bright");
    pMaster_     = apvts.getRawParameterValue("master");
    pSag_        = apvts.getRawParameterValue("sag");
}

// ── prepareToPlay ─────────────────────────────────────────────────────────────
void DR_PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    amp_.prepare(sampleRate, samplesPerBlock);

    // Force all parameters to be synced on first processBlock.
    prevVolume_ = prevBass_ = prevMid_ = prevTreble_ = prevReverb_
                = prevTremSpeed_ = prevTremIntens_ = prevBright_
                = prevMaster_ = prevSag_ = -1.0f;
}

// ── processBlock ──────────────────────────────────────────────────────────────
void DR_PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& /*midi*/) {
    juce::ScopedNoDenormals nodenormals;

    // ── Sync parameters (change detection avoids redundant setter calls) ──────
    const float vol      = pVolume_->load();
    const float bass     = pBass_->load();
    const float mid      = pMid_->load();
    const float treble   = pTreble_->load();
    const float reverb   = pReverb_->load();
    const float tremSpd  = pTremSpeed_->load();
    const float tremInt  = pTremIntens_->load();
    const float bright   = pBright_->load();
    const float master   = pMaster_->load();
    const float sag      = pSag_->load();

    if (vol     != prevVolume_)    { amp_.setVolume      (vol);           prevVolume_    = vol;     }
    if (bass    != prevBass_)      { amp_.setBass         (bass);          prevBass_      = bass;    }
    if (mid     != prevMid_)       { amp_.setMid          (mid);           prevMid_       = mid;     }
    if (treble  != prevTreble_)    { amp_.setTreble       (treble);        prevTreble_    = treble;  }
    if (reverb  != prevReverb_)    { amp_.setReverb       (reverb);        prevReverb_    = reverb;  }
    if (tremSpd != prevTremSpeed_) { amp_.setTremSpeed    (tremSpd);       prevTremSpeed_ = tremSpd; }
    if (tremInt != prevTremIntens_){ amp_.setTremIntensity(tremInt);       prevTremIntens_= tremInt; }
    if (bright  != prevBright_)    { amp_.setBright       (bright > 0.5f); prevBright_    = bright;  }
    if (master  != prevMaster_)    { amp_.setMasterVol    (master);        prevMaster_    = master;  }
    if (sag     != prevSag_)       { amp_.setSag          (sag);           prevSag_       = sag;     }

    // ── Audio processing ──────────────────────────────────────────────────────
    // The amp is mono-in / mono-out.  Use the first input channel for source;
    // write to both output channels if stereo output is requested.
    const int numSamples  = buffer.getNumSamples();
    const int numOutCh    = buffer.getNumChannels();

    if (numSamples == 0 || numOutCh == 0) return;

    // Read input from channel 0 (mono) or sum L+R to mono.
    const float* inL = buffer.getReadPointer(0);
    float*       outL = buffer.getWritePointer(0);

    if (numOutCh >= 2) {
        // Mix stereo input to mono before amp; write mono output to both sides.
        const float* inR = buffer.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i)
            outL[i] = (inL[i] + inR[i]) * 0.5f;
    }

    // Process the mono signal through the complete DR chain.
    amp_.processBlock(outL, outL, numSamples);

    // Copy mono output to right channel if stereo.
    if (numOutCh >= 2) {
        float* outR = buffer.getWritePointer(1);
        std::copy(outL, outL + numSamples, outR);
    }
}

// ── State persistence ─────────────────────────────────────────────────────────
void DR_PluginProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void DR_PluginProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// ── Editor (generic JUCE knob UI — replace with custom DR panel as desired) ──
juce::AudioProcessorEditor* DR_PluginProcessor::createEditor() {
    return new juce::GenericAudioProcessorEditor(*this);
}

// ── Plugin entry point ────────────────────────────────────────────────────────
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new DR_PluginProcessor();
}
