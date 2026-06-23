function (event, funcs) {
    // Nail has no conditional control visibility (all four knobs always shown),
    // so there is nothing to refresh on change in phase 1. Mode-dependent knob
    // relabeling (e.g. Filter → "Sweep" in Dahnward) can be added here when the
    // Broke/Dahnward topologies land in later phases.
    if (event.type == 'start') {
        // initial port values arrive as 'change' events after start
    } else if (event.type == 'change') {
        // no-op
    }
}
