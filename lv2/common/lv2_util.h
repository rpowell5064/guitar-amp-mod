#pragma once
#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/patch/patch.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <cstring>
#include <cstdlib>
#include <new>

// Export a single-plugin LV2 descriptor table.
// Usage: LV2_EXPORT_DESCRIPTOR(uri, instantiate, connect_port,
//                              activate, run, deactivate, cleanup, ext_data)
#define LV2_EXPORT_DESCRIPTOR(URI_STR, ...)                          \
    static const LV2_Descriptor s_lv2_descriptor = {                 \
        URI_STR, __VA_ARGS__                                          \
    };                                                                \
    LV2_SYMBOL_EXPORT const LV2_Descriptor*                           \
    lv2_descriptor(uint32_t index) noexcept {                         \
        return (index == 0) ? &s_lv2_descriptor : nullptr;           \
    }

// Scan an LV2_Feature array for a specific URI; returns data pointer or nullptr.
inline void* lv2_find_feature(const LV2_Feature* const* features,
                               const char* uri) noexcept {
    for (int i = 0; features[i]; ++i)
        if (!strcmp(features[i]->URI, uri))
            return features[i]->data;
    return nullptr;
}
