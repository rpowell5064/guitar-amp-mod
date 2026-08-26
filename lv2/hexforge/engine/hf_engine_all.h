// ─────────────────────────────────────────────────────────────────────────────
// Hex Forge engine — umbrella header (Stage B4, M0)
//
// The COMPLETE host-API-free engine: DSP block instances, preset store +
// blob serialize/migrate, worker bodies, seamless switching, and the whole
// per-block hfEngineRun(). Everything has internal linkage, so each wrapper
// TU (the LV2 plugin today, the JUCE desktop processor next) includes this
// once and compiles its own copy of the one shared engine source — a single
// source of truth with no cross-TU symbols to manage.
//
// A wrapper provides, per instance:
//   * HfWorkerIface / HfHostIface impls installed on the struct (hf_types.inc)
//   * hostPorts[] pointers + per-block audio pointers, then hfPrime()
//   * host events delivered before each hfEngineRun() call
//   * off-RT delivery of hfWork() and audio-thread delivery of hfWorkResponse()
// See hexforge_plugin.cpp for the reference (LV2) wrapper.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include "../hexforge_ports.h"
#include "../hexforge_factory_presets.h"   // band/song factory presets, generated

#include "BiquadFilter.h"
#include "PickupVoicer.h"
#include "HumNotchComb.h"
#include "CalMeasure.h"
#include "EvhCaptureFit.h"
#include "RectoCaptureFit.h"
#include "PickupLoadSim.h"
#include "IrResample.h"
#include "AdaaSoftClip.h"
#include "NoiseGateBlock.h"
#include "CompressorBlock.h"
#include "OversamplingWrapper.h"
#include "EHXBigMuff.h"
#include "Octavia.h"
#include "ToneBenderMkII.h"
#include "ZVexFuzzFactory.h"
#include "OverdriveBlock.h"
#include "AmpBlockExtended.h"
#include "PowerAmpProcessor.h"
#include "CabinetBlock.h"
#include "DefaultCabIR.h"
#include "CabModels.h"
#include "ModulationBlock.h"
#include "ModulationFactory.h"
#include "DelayBlock.h"
#include "DelayFactory.h"
#include "PlateReverbBlock.h"
#include "WahBlock.h"
#include "OctaveBlock.h"
#include "NailDistortion.h"
#include "NamModel.h"
#include "DenormalGuard.h"

#include <new>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include "hf_platform.h"

#include "hf_types.inc"
#include "hf_presets.inc"
#include "hf_worker.inc"
#include "hf_run_core.inc"
