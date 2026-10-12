// Hex Chain native X11 UI — application panels.
//
// Most plugins describe their panel declaratively (hc_spec.h). Scratch Pad and Hex Forge
// are applications in their modguis (a step sequencer, looper lanes, a node chain, a
// preset browser), so their native panels are written as an App: it draws the whole
// window and handles input itself, and reaches the plugin through an AppHost (the
// Panel), which owns the ports, the patch parameters, timers and images.
#pragma once

#include "hc_types.h"

#include <cairo.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace hc {

class AppHost {
public:
    virtual ~AppHost() = default;

    // Ports by symbol. set() writes like a user edit; pulse() writes 1 then 0 after
    // `ms` (the plugins edge-detect their trigger ports).
    virtual float get(const char* symbol) const              = 0;
    virtual void  set(const char* symbol, float value)       = 0;
    virtual void  pulse(const char* symbol, double ms = 60)  = 0;
    virtual const PortInfo* port(const char* symbol) const   = 0;

    // patch:writable parameters by key (the URI fragment): the last value the plugin
    // sent (or the UI set), and patch:Set from the UI as an atom:String / atom:Path.
    virtual std::string param(const char* key) const                    = 0;
    virtual void        setParam(const char* key, const std::string& v) = 0;
    // patch:Set of an atom:Float to <plugin URI>#key (an undeclared property, e.g. the
    // Scratch Pad's #seek), and a patch:Get that asks the plugin to re-send everything.
    virtual void sendFloat(const char* key, float value) = 0;
    virtual void requestAll()                            = 0;

    // A film strip or picture from the plugin's modgui folder (cached), seconds on a
    // monotonic clock, a one-shot timer, and a redraw request.
    virtual cairo_surface_t* image(const std::string& rel) = 0;
    virtual double           now() const                   = 0;
    virtual void after(double ms, std::function<void()> fn) = 0;
    virtual void redraw()                                   = 0;
    // Keyboard focus for text entry (X11 gives an embedded window keys only when asked).
    virtual void grabKeyboard() = 0;
};

// Special keys for App::key (printable text arrives as UTF-8 in `text`).
enum class Key { None, Backspace, Delete, Enter, Escape, Left, Right, Home, End, Tab, Up, Down };

class App {
public:
    virtual ~App() = default;

    virtual int  width() const  = 0;
    virtual int  height() const = 0;
    virtual void draw(cairo_t* cr) = 0;

    // Input in logical window coordinates. Each returns true when a redraw is needed.
    virtual bool press(double x, double y, int button, bool shift, double time) = 0;
    virtual bool release(double x, double y, int button)                        = 0;
    virtual bool motion(double x, double y, bool shift)                         = 0;
    virtual bool scroll(double x, double y, double dy, bool shift)              = 0;
    virtual bool key(Key k, const std::string& text, bool shift)                = 0;

    // Host -> UI.
    virtual void portChanged(const PortInfo& p, float value) = 0;
    virtual void paramChanged(const std::string& key, const std::string& value) = 0;

    // Called at the host's UI rate (about 30 Hz): animations. True = redraw.
    virtual bool tick(double now) { (void)now; return false; }

    // Headless review renders (hc_render --app key=value): put the panel in a state
    // (a tab, an open popup) that would otherwise take clicks.
    virtual void review(const std::string& key, const std::string& value) { (void)key; (void)value; }
};

using AppFactory = std::function<std::unique_ptr<App>(AppHost&)>;

} // namespace hc
