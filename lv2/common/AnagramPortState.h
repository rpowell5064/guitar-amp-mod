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
    // Hidden ports are blocked by the host as well as hidden. If on-device testing
    // shows preset recall is blocked on a hidden port, flip kHide to INACTIVE
    // (greyed, still settable) in one place.
    static constexpr LV2_Control_Port_State kHide = LV2_CONTROL_PORT_STATE_HIDDEN;

    const LV2_Control_Port_State_Update* host = nullptr;
    int8_t sent[N];

    AnagramPortState() noexcept { invalidate(); }

    void init(const LV2_Feature* const* features) noexcept {
        host = static_cast<const LV2_Control_Port_State_Update*>(
            lv2_find_feature(features, LV2_CONTROL_PORT_STATE_UPDATE_URI));
        invalidate();
    }
    // Forget what was reported (the next set() of every port is sent again).
    void invalidate() noexcept { std::memset(sent, -1, sizeof(sent)); }

    void set(uint32_t port, LV2_Control_Port_State state) noexcept {
        if (!host || port >= static_cast<uint32_t>(N) || sent[port] == static_cast<int8_t>(state)) return;
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
