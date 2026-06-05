function (event, funcs) {
    // No model-aware show/hide or path persistence needed: all controls are
    // always visible and the Era dropdown is a plain enumerated control port,
    // which mod-ui's custom-select widget restores from scale points on load.
    if (event.type == 'start') {
    } else if (event.type == 'change') {
    }
}
