#include "x11.hpp"
#include "app_logic.hpp"
#include <iostream>
#include <algorithm>
#include <vector>

using namespace std;

bool setup_x11(X11Context &ctx)
{
    ctx.display = XOpenDisplay(nullptr);
    if (!ctx.display) {
        cerr << "Failed to open X11 display" << endl;
        return false;
    }
    ctx.screen = DefaultScreen(ctx.display);
    ctx.window = XCreateSimpleWindow(ctx.display, RootWindow(ctx.display, ctx.screen), 100, 100, ctx.width, ctx.height, 1, 0, 0);

    XStoreName(ctx.display, ctx.window, "Terminal");
    XSelectInput(ctx.display, ctx.window, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    XMapWindow(ctx.display, ctx.window);

    ctx.surface = cairo_xlib_surface_create(ctx.display, ctx.window, DefaultVisual(ctx.display, ctx.screen), ctx.width, ctx.height);
    ctx.cr = cairo_create(ctx.surface);

    ctx.layout = pango_cairo_create_layout(ctx.cr);
    ctx.font_desc = pango_font_description_from_string("Noto Mono 12");
    pango_layout_set_font_description(ctx.layout, ctx.font_desc);

    ctx.gc = XCreateGC(ctx.display, ctx.window, 0, nullptr);

    if (!setlocale(LC_ALL, ""))
        return false;
    XSetLocaleModifiers("");
    ctx.xim = XOpenIM(ctx.display, nullptr, nullptr, nullptr);
    if (!ctx.xim)
        return false;
    ctx.xic = XCreateIC(ctx.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, ctx.window, NULL);
    if (!ctx.xic)
        return false;

    ctx.clipboard_atom = XInternAtom(ctx.display, "CLIPBOARD", False);
    ctx.paste_property_atom = XInternAtom(ctx.display, "PASTE_PROPERTY", False);
    return true;
}

void cleanup_x11(X11Context &ctx)
{
    if (ctx.display)
    {
        pango_font_description_free(ctx.font_desc);
        g_object_unref(ctx.layout);
        cairo_destroy(ctx.cr);
        cairo_surface_destroy(ctx.surface);

        if (ctx.xic)
            XDestroyIC(ctx.xic);
        if (ctx.xim)
            XCloseIM(ctx.xim);
        if (ctx.gc)
            XFreeGC(ctx.display, ctx.gc);
        XDestroyWindow(ctx.display, ctx.window);
        XCloseDisplay(ctx.display);
    }
}

static void get_text_size(PangoLayout *layout, const string &text, int &width, int &height)
{
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);
}


void draw_frame(X11Context &ctx, bool cursorVisible)
{
    cairo_xlib_surface_set_size(ctx.surface, ctx.width, ctx.height);
    cairo_set_source_rgb(ctx.cr, 0.117, 0.117, 0.117);
    cairo_paint(ctx.cr);

    int text_width, text_height;
    get_text_size(ctx.layout, "W", text_width, text_height);
    const int lineHeight = text_height + 4;
    int char_width = text_width;

    string headerTitle = "MY TERM";
    get_text_size(ctx.layout, headerTitle, text_width, text_height);
    int paddingX = 4, paddingY = 2;
    int rectY = (lineHeight - text_height) / 2 - paddingY;
    int rectWidth = text_width + 2 * paddingX;
    int rectHeight = text_height + 2 * paddingY;
    cairo_set_source_rgb(ctx.cr, 0.913, 0.329, 0.125);
    cairo_rectangle(ctx.cr, 10, rectY, rectWidth, rectHeight);
    cairo_fill(ctx.cr);
    cairo_set_source_rgb(ctx.cr, 1.0, 1.0, 1.0);
    cairo_move_to(ctx.cr, 10 + paddingX, rectY + paddingY + (lineHeight - text_height) / 2);
    pango_layout_set_text(ctx.layout, headerTitle.c_str(), -1);
    pango_cairo_show_layout(ctx.cr, ctx.layout);
    int tab_start_x = 250;
    int current_x = tab_start_x;
    int tab_y_pos = rectY + paddingY + (lineHeight - text_height) / 2;
    if (tabScrollOffset > 0)
    {
        string left_indicator = "< ";
        cairo_move_to(ctx.cr, current_x, tab_y_pos);
        pango_layout_set_text(ctx.layout, left_indicator.c_str(), -1);
        pango_cairo_show_layout(ctx.cr, ctx.layout);
        get_text_size(ctx.layout, left_indicator, text_width, text_height);
        current_x += text_width;
    }
    bool more_tabs_on_right = false;
    for (int i = tabScrollOffset; i < (int)tabs.size(); i++)
    {
        string next_tab_str = (i == activeTab) ? "[Tab" + to_string(i + 1) + "] " : " Tab" + to_string(i + 1) + "  ";
        get_text_size(ctx.layout, next_tab_str, text_width, text_height);
        if (current_x + text_width > ctx.width - 30)
        {
            more_tabs_on_right = true;
            break;
        }
        cairo_move_to(ctx.cr, current_x, tab_y_pos);
        pango_layout_set_text(ctx.layout, next_tab_str.c_str(), -1);
        pango_cairo_show_layout(ctx.cr, ctx.layout);
        current_x += text_width;
    }
    if (more_tabs_on_right)
    {
        string right_indicator = " >";
        cairo_move_to(ctx.cr, current_x, tab_y_pos);
        pango_layout_set_text(ctx.layout, right_indicator.c_str(), -1);
        pango_cairo_show_layout(ctx.cr, ctx.layout);
    }
    int headerLineY = lineHeight + 2;
    cairo_set_source_rgb(ctx.cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(ctx.cr, 1.0);
    cairo_move_to(ctx.cr, 0, headerLineY);
    cairo_line_to(ctx.cr, ctx.width, headerLineY);
    cairo_stroke(ctx.cr);

    if (activeTab == -1)
        return;

    const ShellTab &currentTab = tabs[activeTab];
    int x_start_pos = 10;
    double y = headerLineY + lineHeight;

    
    int visibleLines = (ctx.height - y) / lineHeight;
    if (visibleLines <= 0) visibleLines = 1;

    int totalLines = currentTab.outputLines.size();
    int firstLineToDraw = max(0, totalLines - (visibleLines-1) - currentTab.scrollOffset);

    for (int i = firstLineToDraw; i < totalLines; ++i) {
        if (y > ctx.height) break;

        const auto &line = currentTab.outputLines[i];
        
        double current_line_x = x_start_pos - (currentTab.scrollX * char_width);

        const string promptStr = linux_username + "@my_term:";
        if (line.compare(0, promptStr.size(), promptStr) == 0) {
            pango_layout_set_width(ctx.layout, -1);
            size_t posCwdStart = promptStr.size();
            size_t posPromptEnd = line.find(" $ ", posCwdStart);
            if (posPromptEnd == string::npos) posPromptEnd = line.size();

            string p1 = line.substr(0, posCwdStart);
            string p2 = line.substr(posCwdStart, posPromptEnd - posCwdStart);
            string p3_and_cmd = line.substr(posPromptEnd);

            cairo_set_source_rgb(ctx.cr, 0.415, 0.6, 0.333);
            cairo_move_to(ctx.cr, current_line_x, y);
            pango_layout_set_text(ctx.layout, p1.c_str(), -1);
            pango_cairo_show_layout(ctx.cr, ctx.layout);
            get_text_size(ctx.layout, p1, text_width, text_height);
            current_line_x += text_width;

            cairo_set_source_rgb(ctx.cr, 0.337, 0.611, 0.839);
            cairo_move_to(ctx.cr, current_line_x, y);
            pango_layout_set_text(ctx.layout, p2.c_str(), -1);
            pango_cairo_show_layout(ctx.cr, ctx.layout);
            get_text_size(ctx.layout, p2, text_width, text_height);
            current_line_x += text_width;

            cairo_set_source_rgb(ctx.cr, 1.0, 1.0, 1.0);
            cairo_move_to(ctx.cr, current_line_x, y);
            pango_layout_set_text(ctx.layout, p3_and_cmd.c_str(), -1);
            pango_cairo_show_layout(ctx.cr, ctx.layout);

            y += lineHeight;
        }
        else {
            pango_layout_set_width(ctx.layout, -1);
            cairo_move_to(ctx.cr, current_line_x, y);
            pango_layout_set_text(ctx.layout, line.c_str(), -1);
            cairo_set_source_rgb(ctx.cr, 1.0, 1.0, 1.0);
            pango_cairo_show_layout(ctx.cr, ctx.layout);
            y += lineHeight;
        }
    }

    pango_layout_set_width(ctx.layout, -1);

    if (!currentTab.is_busy)
    {
        if (currentTab.searchMode) {
            string searchPrompt = "Enter search term: " + currentTab.searchTerm;
            cairo_set_source_rgb(ctx.cr, 1.0, 1.0, 1.0);
            cairo_move_to(ctx.cr, x_start_pos, y);
            pango_layout_set_text(ctx.layout, searchPrompt.c_str(), -1);
            pango_cairo_show_layout(ctx.cr, ctx.layout);
            
            if (cursorVisible) {
                get_text_size(ctx.layout, searchPrompt, text_width, text_height);
                double cursorX = x_start_pos + text_width;
                cairo_rectangle(ctx.cr, cursorX, y, 2, text_height);
                cairo_fill(ctx.cr);
            }
        } else if (currentTab.completionMode) {
            string prompt = "Choose (1-" + to_string(currentTab.completionOptions.size()) + ") or any other key to cancel: " + currentTab.completionInput;
            cairo_set_source_rgb(ctx.cr, 1.0, 1.0, 1.0);
            cairo_move_to(ctx.cr, x_start_pos, y);
            pango_layout_set_text(ctx.layout, prompt.c_str(), -1);
            pango_cairo_show_layout(ctx.cr, ctx.layout);

            if (cursorVisible) {
                get_text_size(ctx.layout, prompt, text_width, text_height);
                double cursorX = x_start_pos + text_width;
                cairo_rectangle(ctx.cr, cursorX, y, 2, text_height);
                cairo_fill(ctx.cr);
            }
        }
        else{
            double offsetX = x_start_pos - (currentTab.scrollX * char_width);
            string prompt_part3 = " $ ";

            if (currentTab.multilineBuffer.empty())
            {
                string p1 = "rahul@my_term:";
                string p2 = currentTab.currentDir;

                cairo_set_source_rgb(ctx.cr, 0.415, 0.6, 0.333);
                cairo_move_to(ctx.cr, offsetX, y);
                pango_layout_set_text(ctx.layout, p1.c_str(), -1);
                pango_cairo_show_layout(ctx.cr, ctx.layout);
                get_text_size(ctx.layout, p1, text_width, text_height);
                offsetX += text_width;

                cairo_set_source_rgb(ctx.cr, 0.337, 0.611, 0.839);
                cairo_move_to(ctx.cr, offsetX, y);
                pango_layout_set_text(ctx.layout, p2.c_str(), -1);
                pango_cairo_show_layout(ctx.cr, ctx.layout);
                get_text_size(ctx.layout, p2, text_width, text_height);
                offsetX += text_width;
            }
            else
            {
                prompt_part3 = "> ";
            }

            cairo_set_source_rgb(ctx.cr, 1.0, 1.0, 1.0);
            cairo_move_to(ctx.cr, offsetX, y);
            pango_layout_set_text(ctx.layout, prompt_part3.c_str(), -1);
            pango_cairo_show_layout(ctx.cr, ctx.layout);
            get_text_size(ctx.layout, prompt_part3, text_width, text_height);
            offsetX += text_width;

            cairo_move_to(ctx.cr, offsetX, y);
            pango_layout_set_text(ctx.layout, currentTab.input.c_str(), -1);
            pango_cairo_show_layout(ctx.cr, ctx.layout);

            if (cursorVisible)
            {
                string beforeCursor = currentTab.input.substr(0, currentTab.cursorPos);
                get_text_size(ctx.layout, beforeCursor, text_width, text_height);
                double cursorX = offsetX + text_width;
                cairo_rectangle(ctx.cr, cursorX, y, 2, text_height);
                cairo_fill(ctx.cr);
            }
        }
    }
}