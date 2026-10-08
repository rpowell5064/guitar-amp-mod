function (event, funcs) {
    // Claw has no conditional control visibility (every knob is live in every
    // mode), so there is nothing to refresh on change. Mode-dependent knob
    // relabeling (Feed -> "Loop" in Howl, Pitch -> "Chaos" in Butterfly) can
    // be added here once the modes are ear-tuned.
    if (event.type == 'start') {
        // initial port values arrive as 'change' events after start
    } else if (event.type == 'change') {
        // no-op
    }
}
