#pragma once

#include "types.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

struct X11Context {
    Display *display = nullptr;
    Window window;
    int screen;
    int width = 900, height = 600;

    cairo_surface_t *surface = nullptr;
    cairo_t *cr = nullptr;
    PangoLayout *layout = nullptr;
    PangoFontDescription *font_desc = nullptr;
    
    GC gc = nullptr; // useful for clearing the screen
    XIM xim = nullptr;
    XIC xic = nullptr;
    
    Atom clipboard_atom;
    Atom paste_property_atom;
};

bool setup_x11(X11Context &ctx);
void draw_frame(X11Context &ctx, bool cursorVisible);
void cleanup_x11(X11Context &ctx);