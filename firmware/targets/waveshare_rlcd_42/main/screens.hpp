#pragma once
#include "canvas.hpp"
#include "panel_logic.hpp"
#include "view.hpp"
namespace usage_panel::rlcd {
/** Composes one complete frame for a view. Pure drawing; no hardware access. */
class Screens {
  public:
    explicit Screens(Canvas &canvas) : c_(canvas) {}
    void render(const View &view);

  private:
    Canvas &c_;
    bool portrait_ = false;
    /** Page title (or the time), link status chip, page position and battery above a rule. */
    void header(const View &v, const char *title, int page = -1);
    void battery(const View &v, int x, Ink ink = Ink::Black);
    /**
     * Inverted pixel-font label with rounded corners. The text is placed at (`x`, `top`) and
     * the chip extends 4 px around it. @return text width.
     */
    int chip(int x, int top, const char *s, Ink ink = Ink::Black);
    /**
     * 2 px outlined bar filled with up to twenty 5 % cells inset by 2 px; a negative `percent`
     * leaves it empty. `marker` is the elapsed fraction of the window, or negative to omit it.
     */
    void bar(int x, int y, int w, int h, int percent, float marker = -1.f);
    /**
     * Seven-segment percentage `h` pixels high in a fixed field: a hundreds half digit and two
     * right-aligned digit cells, then a percent sign of the same stroke weight at a fixed
     * position. A negative `percent` shows dashes. @return field width.
     */
    int big_percent(int x, int top, int percent, int h, Ink ink = Ink::Black);
    static int big_percent_width(int h);
    void home(const View &v);
    void home_row(const View &v, int provider, int top);
    void home_portrait(const View &v);
    void focus(const View &v, int provider);
    void alert(const View &v);
    void trend(const View &v);
    void clock(const View &v);
    void settings(const View &v);
    void pairing(const View &v);
    void ota(const View &v);
};

/** Status text for the header: PAIRING, UPDATING, LINKED or OFFLINE. */
const char *link_status(const View &view);
} // namespace usage_panel::rlcd
