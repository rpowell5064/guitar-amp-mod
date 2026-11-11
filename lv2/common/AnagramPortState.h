#pragma once
// Darkglass KosmOS (HEXCHAIN_ANAGRAM) only: per-port UI state reported to the host
// through the control-port-state-update feature (dg/control-port-state-update.h,
// ISC, vendored verbatim). The device has no plugin UI of its own, so this is how a
// block shows only the controls the selected model really has (the modgui rule).
//
// The feature is OPTIONAL: when the host does not offer it every call is a no-op.
// Each port's last reported state is cached, so calling set() for every port on
// every run() costs a compare, and the host hears only real changes. The host
// requires update_state() to be called from run().
#ifdef HEXCHAIN_ANAGRAM
#include "lv2_util.h"
#include "dg/control-port-state-update.h"
#include <cstdint>
#include <cstring>

template <int N>
struct AnagramPortState {
    // A control that does not exist on the current model is reported INACTIVE (greyed, still
    // settable, still in Scenes). Measured on the device (2026-11-09 dev image): the host accepts
    // HIDDEN but the settings screen never removes a control, and HIDDEN/BLOCKED are honoured
    // only on ports declaring dg:mayUpdateBlockedState, which also drops them from Scenes. The
    // specs lay each model's controls out on their own pages instead.
    static constexpr LV2_Control_Port_State kHide = LV2_CONTROL_PORT_STATE_INACTIVE;

    const LV2_Control_Port_State_Update* host = nullptr;
    int8_t sent[N];
    // Ports from here on are past what the host tracks (KosmOS keeps a block's first 60 control
    // ports): never reported -- a report for one trips the UI's "paramIndex != UINT8_MAX" assertion.
    uint32_t limit = N;

    AnagramPortState() noexcept { invalidate(); }

    void init(const LV2_Feature* const* features) noexcept {
        host = static_cast<const LV2_Control_Port_State_Update*>(
            lv2_find_feature(features, LV2_CONTROL_PORT_STATE_UPDATE_URI));
        invalidate();
    }
    // Forget what was reported (the next set() of every port is sent again).
    void invalidate() noexcept { std::memset(sent, -1, sizeof(sent)); }

    void set(uint32_t port, LV2_Control_Port_State state) noexcept {
        if (!host || port >= static_cast<uint32_t>(N) || port >= limit || sent[port] == static_cast<int8_t>(state)) return;
        if (host->update_state(host->handle, port, state) == LV2_CONTROL_PORT_STATE_UPDATE_SUCCESS)
            sent[port] = static_cast<int8_t>(state);
    }
    // visible = the control exists on this model; active = it currently does something
    // (e.g. Room Mix while Room is off): shown greyed when visible but inactive.
    void ui(uint32_t port, bool visible, bool active = true) noexcept {
        set(port, !visible ? kHide : (active ? LV2_CONTROL_PORT_STATE_NONE : LV2_CONTROL_PORT_STATE_INACTIVE));
    }
};
#endif
