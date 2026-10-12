// Hex Chain native X11 UI — the LV2 UI entry point.
//
// One binary (hexchain_ui.so) serves every plugin: lv2ui_descriptor(i) hands out one
// descriptor per generated PluginInfo, all sharing these callbacks. The window is a
// pugl view (X11 + cairo) embedded in the host's ui:parent; the host drives it through
// ui:idleInterface (jalv.gtk3, which Zynthian launches for an X11UI, does).
//
// Shipped only by the -DHEXCHAIN_X11UI=ON build. The DSP plugins know nothing of it.
#include "hc_panel.h"
#include "hc_registry.h"

#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/patch/patch.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <pugl/cairo.h>
#include <pugl/pugl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Ui {
    PuglWorld*                 world = nullptr;
    PuglView*                  view  = nullptr;
    std::unique_ptr<hc::Panel> panel;
    LV2UI_Write_Function       writeFn    = nullptr;
    LV2UI_Controller           controller = nullptr;
    double                     scale = 1.0, offX = 0, offY = 0;   // logical -> window
    bool                       closed = false;

    // File parameters: patch:Set / patch:Get over the plugin's atom ports. Needs the
    // host's urid:map (every host that runs an X11 UI provides it); without it the
    // File controls show but do nothing.
    const hc::PluginInfo* info = nullptr;
    LV2_URID_Map*         map  = nullptr;
    LV2_URID_Unmap*       unmap = nullptr;   // optional: names undeclared properties (#tuner)
    struct {
        LV2_URID eventTransfer, object, urid, path, string;
        LV2_URID set, get, property, value;
        LV2_URID flt;   // atom:Float (Scratch Pad's #seek)
    } u {};
    std::vector<LV2_URID> fileUrids;   // parallel to info->files

    void toLogical(double wx, double wy, double& x, double& y) const
    {
        x = (wx - offX) / scale;
        y = (wy - offY) / scale;
    }
};

void fit(Ui* ui, double w, double h)
{
    const double pw = ui->panel->width(), ph = ui->panel->height();
    ui->scale = std::max(0.25, std::min(w / pw, h / ph));
    ui->offX  = std::round((w - pw * ui->scale) / 2);
    ui->offY  = std::round((h - ph * ui->scale) / 2);
}

PuglStatus onEvent(PuglView* view, const PuglEvent* ev)
{
    Ui* ui = static_cast<Ui*>(puglGetHandle(view));
    bool redraw = false;
    double x = 0, y = 0;
    switch (ev->type) {
    case PUGL_CONFIGURE:
        fit(ui, ev->configure.width, ev->configure.height);
        redraw = true;
        break;
    case PUGL_EXPOSE: {
        cairo_t* cr = static_cast<cairo_t*>(puglGetContext(view));
        cairo_set_source_rgb(cr, 0.03, 0.035, 0.045);   // letterbox around the panel
        cairo_paint(cr);
        cairo_save(cr);
        cairo_translate(cr, ui->offX, ui->offY);
        cairo_scale(cr, ui->scale, ui->scale);
        ui->panel->draw(cr);
        cairo_restore(cr);
        break;
    }
    case PUGL_BUTTON_PRESS:
        ui->toLogical(ev->button.x, ev->button.y, x, y);
        redraw = ui->panel->press(x, y, ev->button.button,
                                  (ev->button.state & PUGL_MOD_SHIFT) != 0, ev->button.time);
        break;
    case PUGL_BUTTON_RELEASE:
        ui->toLogical(ev->button.x, ev->button.y, x, y);
        redraw = ui->panel->release(x, y, ev->button.button);
        break;
    case PUGL_MOTION:
        ui->toLogical(ev->motion.x, ev->motion.y, x, y);
        redraw = ui->panel->motion(x, y, (ev->motion.state & PUGL_MOD_SHIFT) != 0);
        break;
    case PUGL_SCROLL: {
        ui->toLogical(ev->scroll.x, ev->scroll.y, x, y);
        double dy = ev->scroll.dy;
        if (dy == 0.0)   // discrete wheels report only the direction
            dy = ev->scroll.direction == PUGL_SCROLL_UP ? 1.0
               : ev->scroll.direction == PUGL_SCROLL_DOWN ? -1.0 : 0.0;
        redraw = ui->panel->scroll(x, y, dy, (ev->scroll.state & PUGL_MOD_SHIFT) != 0);
        break;
    }
    case PUGL_KEY_PRESS: {
        hc::Key k = hc::Key::None;
        switch (ev->key.key) {
        case PUGL_KEY_BACKSPACE: k = hc::Key::Backspace; break;
        case PUGL_KEY_DELETE:    k = hc::Key::Delete; break;
        case PUGL_KEY_ENTER:     k = hc::Key::Enter; break;
        case PUGL_KEY_ESCAPE:    k = hc::Key::Escape; break;
        case PUGL_KEY_TAB:       k = hc::Key::Tab; break;
        case PUGL_KEY_LEFT:      k = hc::Key::Left; break;
        case PUGL_KEY_RIGHT:     k = hc::Key::Right; break;
        case PUGL_KEY_UP:        k = hc::Key::Up; break;
        case PUGL_KEY_DOWN:      k = hc::Key::Down; break;
        case PUGL_KEY_HOME:      k = hc::Key::Home; break;
        case PUGL_KEY_END:       k = hc::Key::End; break;
        default: break;
        }
        if (k != hc::Key::None)
            redraw = ui->panel->key(k, {}, (ev->key.state & PUGL_MOD_SHIFT) != 0);
        break;
    }
    case PUGL_TEXT:
        if (ev->text.character >= 0x20 && ev->text.character != 0x7f)
            redraw = ui->panel->key(hc::Key::None, ev->text.string,
                                    (ev->text.state & PUGL_MOD_SHIFT) != 0);
        break;
    case PUGL_CLOSE:
        ui->closed = true;
        break;
    default:
        break;
    }
    if (redraw)
        puglObscureView(view);
    return PUGL_SUCCESS;
}

// Send one patch:Set (value != nullptr) or patch:Get to the plugin's control port. The
// value goes as an atom:Path, or an atom:String for a text parameter (preset JSON can run
// to tens of kB, so the buffer is sized to it).
void sendPatch(Ui* ui, const hc::FileParam* param, const char* value)
{
    if (!ui->map || ui->info->atomIn < 0)
        return;
    const size_t len = value ? std::strlen(value) : 0;
    std::vector<uint64_t> buf((len + 1 + 256) / sizeof(uint64_t) + 1);
    LV2_Atom_Forge forge;
    lv2_atom_forge_init(&forge, ui->map);
    lv2_atom_forge_set_buffer(&forge, reinterpret_cast<uint8_t*>(buf.data()),
                              buf.size() * sizeof(uint64_t));
    LV2_Atom_Forge_Frame frame;
    const LV2_Atom_Forge_Ref ref =
        lv2_atom_forge_object(&forge, &frame, 0, value ? ui->u.set : ui->u.get);
    if (value) {
        lv2_atom_forge_key(&forge, ui->u.property);
        lv2_atom_forge_urid(&forge, ui->map->map(ui->map->handle, param->uri));
        lv2_atom_forge_key(&forge, ui->u.value);
        if (param->text)
            lv2_atom_forge_string(&forge, value, uint32_t(len));
        else
            lv2_atom_forge_path(&forge, value, uint32_t(len) + 1);
    }
    lv2_atom_forge_pop(&forge, &frame);
    const LV2_Atom* msg = lv2_atom_forge_deref(&forge, ref);
    if (!msg)
        return;
    ui->writeFn(ui->controller, uint32_t(ui->info->atomIn), lv2_atom_total_size(msg),
                ui->u.eventTransfer, msg);
}

// patch:Set { property <plugin>#key, value atom:Float } (Scratch Pad's #seek).
void sendFloat(Ui* ui, const char* key, float v)
{
    if (!ui->map || ui->info->atomIn < 0)
        return;
    alignas(8) uint8_t buf[256];
    LV2_Atom_Forge forge;
    lv2_atom_forge_init(&forge, ui->map);
    lv2_atom_forge_set_buffer(&forge, buf, sizeof buf);
    const std::string uri = std::string(ui->info->uri) + "#" + key;
    LV2_Atom_Forge_Frame frame;
    const LV2_Atom_Forge_Ref ref = lv2_atom_forge_object(&forge, &frame, 0, ui->u.set);
    lv2_atom_forge_key(&forge, ui->u.property);
    lv2_atom_forge_urid(&forge, ui->map->map(ui->map->handle, uri.c_str()));
    lv2_atom_forge_key(&forge, ui->u.value);
    lv2_atom_forge_float(&forge, v);
    lv2_atom_forge_pop(&forge, &frame);
    if (const LV2_Atom* msg = lv2_atom_forge_deref(&forge, ref))
        ui->writeFn(ui->controller, uint32_t(ui->info->atomIn), lv2_atom_total_size(msg),
                    ui->u.eventTransfer, msg);
}

LV2UI_Handle instantiate(const LV2UI_Descriptor* desc, const char* /*pluginUri*/,
                         const char* bundlePath, LV2UI_Write_Function writeFn,
                         LV2UI_Controller controller, LV2UI_Widget* widget,
                         const LV2_Feature* const* features)
{
    const hc::PluginInfo* info = hc::pluginByUiUri(desc->URI);
    if (!info)
        return nullptr;
    hc::PanelSpec spec;
    if (!hc::makeSpec(*info, spec))
        return nullptr;

    void*              parent = nullptr;
    const LV2UI_Resize* resize = nullptr;
    LV2_URID_Map*      map    = nullptr;
    LV2_URID_Unmap*    unmap  = nullptr;
    for (int i = 0; features && features[i]; ++i) {
        if (!std::strcmp(features[i]->URI, LV2_UI__parent))
            parent = features[i]->data;
        else if (!std::strcmp(features[i]->URI, LV2_URID__map))
            map = static_cast<LV2_URID_Map*>(features[i]->data);
        else if (!std::strcmp(features[i]->URI, LV2_URID__unmap))
            unmap = static_cast<LV2_URID_Unmap*>(features[i]->data);
        else if (!std::strcmp(features[i]->URI, LV2_UI__resize))
            resize = static_cast<const LV2UI_Resize*>(features[i]->data);
    }

    std::string bundle = bundlePath ? bundlePath : "";
    if (!bundle.empty() && bundle.back() != '/')
        bundle += '/';

    auto ui        = std::make_unique<Ui>();
    ui->writeFn    = writeFn;
    ui->controller = controller;
    ui->panel      = std::make_unique<hc::Panel>(std::move(spec), *info,
                                                 bundle + "modgui-" + info->key + "/",
                                                 bundle + "x11ui-res/logo.png");
    Ui* raw = ui.get();
    ui->panel->onWrite = [raw](uint32_t index, float v) {
        raw->writeFn(raw->controller, index, sizeof(float), 0, &v);
    };
    ui->info  = info;
    ui->map   = map;
    ui->unmap = unmap;
    if (map) {
        auto m = [map](const char* uri) { return map->map(map->handle, uri); };
        ui->u = { m(LV2_ATOM__eventTransfer), m(LV2_ATOM__Object), m(LV2_ATOM__URID),
                  m(LV2_ATOM__Path), m(LV2_ATOM__String), m(LV2_PATCH__Set),
                  m(LV2_PATCH__Get), m(LV2_PATCH__property), m(LV2_PATCH__value),
                  m(LV2_ATOM__Float) };
        for (int i = 0; i < info->numFiles; ++i)
            ui->fileUrids.push_back(m(info->files[i].uri));
    }
    ui->panel->onFileSet = [raw](const hc::FileParam& param, const std::string& path) {
        sendPatch(raw, &param, path.c_str());
    };
    ui->panel->onSendFloat = [raw](const char* key, float v) { sendFloat(raw, key, v); };
    ui->panel->onRequestAll = [raw] { sendPatch(raw, nullptr, nullptr); };
    ui->panel->onGrabKeyboard = [raw] {
        if (raw->view)
            puglGrabFocus(raw->view);
    };

    ui->world = puglNewWorld(PUGL_MODULE, 0);
    if (!ui->world)
        return nullptr;
    puglSetWorldString(ui->world, PUGL_CLASS_NAME, "HexChain");
    ui->view = puglNewView(ui->world);
    const int w = ui->panel->width(), h = ui->panel->height();
    puglSetBackend(ui->view, puglCairoBackend());
    puglSetSizeHint(ui->view, PUGL_DEFAULT_SIZE, PuglSpan(w), PuglSpan(h));
    puglSetSizeHint(ui->view, PUGL_MIN_SIZE, PuglSpan(w / 2), PuglSpan(h / 2));
    puglSetViewHint(ui->view, PUGL_RESIZABLE, 1);
    puglSetViewString(ui->view, PUGL_WINDOW_TITLE, info->key);
    if (parent)
        puglSetParent(ui->view, reinterpret_cast<PuglNativeView>(parent));
    puglSetHandle(ui->view, raw);
    puglSetEventFunc(ui->view, onEvent);
    if (puglRealize(ui->view) != PUGL_SUCCESS) {
        puglFreeView(ui->view);
        puglFreeWorld(ui->world);
        return nullptr;
    }
    fit(raw, w, h);
    puglShow(ui->view, PUGL_SHOW_RAISE);
    if (resize)
        resize->ui_resize(resize->handle, w, h);

    // Ask for the current file paths; the plugin answers on its notify port.
    if (info->numFiles)
        sendPatch(raw, nullptr, nullptr);

    *widget = reinterpret_cast<LV2UI_Widget>(puglGetNativeView(ui->view));
    return ui.release();
}

void cleanup(LV2UI_Handle handle)
{
    Ui* ui = static_cast<Ui*>(handle);
    if (ui->view) {
        puglUnrealize(ui->view);
        puglFreeView(ui->view);
    }
    if (ui->world)
        puglFreeWorld(ui->world);
    delete ui;
}

void portEvent(LV2UI_Handle handle, uint32_t index, uint32_t size, uint32_t format,
               const void* buffer)
{
    Ui* ui = static_cast<Ui*>(handle);
    if (format == 0) {
        if (size == sizeof(float))
            ui->panel->setPortValue(index, *static_cast<const float*>(buffer));
    } else if (ui->map && format == ui->u.eventTransfer && int(index) == ui->info->atomOut) {
        // patch:Set { property <param>, value "path" } from the plugin's notify port.
        const auto* atom = static_cast<const LV2_Atom*>(buffer);
        if (atom->type != ui->u.object)
            return;
        const auto* obj = reinterpret_cast<const LV2_Atom_Object*>(atom);
        if (obj->body.otype != ui->u.set)
            return;
        const LV2_Atom *prop = nullptr, *val = nullptr;
        lv2_atom_object_get(obj, ui->u.property, &prop, ui->u.value, &val, 0);
        if (!prop || prop->type != ui->u.urid || !val ||
            (val->type != ui->u.path && val->type != ui->u.string))
            return;
        const LV2_URID key = reinterpret_cast<const LV2_Atom_URID*>(prop)->body;
        const char* s = static_cast<const char*>(LV2_ATOM_BODY_CONST(val));
        const std::string text(s, strnlen(s, val->size));
        bool known = false;
        for (size_t i = 0; i < ui->fileUrids.size(); ++i)
            if (ui->fileUrids[i] == key) {
                ui->panel->setFilePath(ui->info->files[i].uri, text);
                known = true;
            }
        // A property the TTL doesn't declare (Hex Forge's #tuner, #cal): by name, when
        // the host can unmap it and it lives under the plugin's URI.
        if (!known && ui->unmap)
            if (const char* uri = ui->unmap->unmap(ui->unmap->handle, key)) {
                const std::string base = std::string(ui->info->uri) + "#";
                if (!std::strncmp(uri, base.c_str(), base.size()))
                    ui->panel->paramFromHost(uri + base.size(), text);
            }
    } else {
        return;
    }
    puglObscureView(ui->view);
}

int idle(LV2UI_Handle handle)
{
    Ui* ui = static_cast<Ui*>(handle);
    if (ui->panel->tick())   // timers (trigger pulses) and App animation
        puglObscureView(ui->view);
    puglUpdate(ui->world, 0.0);
    return ui->closed ? 1 : 0;
}

int uiResize(LV2UI_Feature_Handle handle, int w, int h)
{
    Ui* ui = static_cast<Ui*>(handle);
    puglSetSizeHint(ui->view, PUGL_CURRENT_SIZE, PuglSpan(w), PuglSpan(h));
    return 0;
}

const void* extensionData(const char* uri)
{
    static const LV2UI_Idle_Interface idleIface   = { idle };
    static const LV2UI_Resize         resizeIface = { nullptr, uiResize };
    if (!std::strcmp(uri, LV2_UI__idleInterface))
        return &idleIface;
    if (!std::strcmp(uri, LV2_UI__resize))
        return &resizeIface;
    return nullptr;
}

std::vector<LV2UI_Descriptor>& descriptors()
{
    static std::vector<LV2UI_Descriptor> d = [] {
        std::vector<LV2UI_Descriptor> v;
        for (int i = 0; i < hc::numPlugins(); ++i)
            v.push_back({ hc::plugin(i).uiUri, instantiate, cleanup, portEvent, extensionData });
        return v;
    }();
    return d;
}

} // namespace

extern "C" LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index)
{
    auto& d = descriptors();
    return index < d.size() ? &d[index] : nullptr;
}
