#include "status_view.h"

#include <gui/gui.h>
#include <gui/view.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    PrinterStatus status;
} StatusModel;

/*
 * Layout (128 × 64):
 *
 *   y= 9  State          (FontPrimary)
 *   y=11  ─ separator ─
 *   y=20  File name      (FontSecondary, truncated)
 *   y=23..29  Progress bar [0..88] + "NN%" right of bar
 *   y=38  T:215/215  B:60/60   (FontSecondary)
 *   y=47  ETA: 1h 23m          (FontSecondary)
 *   y=62  v1.9.2               (FontSecondary, bottom-right)
 */
static void draw_cb(Canvas* canvas, void* model_ptr) {
    StatusModel*   m = model_ptr;
    PrinterStatus* s = &m->status;
    char buf[48];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(!s->loaded) {
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Connecting...");
        return;
    }

    /* State */
    canvas_draw_str(canvas, 0, 9, s->state[0] ? s->state : "Offline");
    canvas_draw_line(canvas, 0, 11, 127, 11);

    canvas_set_font(canvas, FontSecondary);

    /* File name (truncated to 21 chars to fit ~128 px at FontSecondary) */
    if(s->file_name[0]) {
        char fname[22];
        strncpy(fname, s->file_name, 21);
        fname[21] = '\0';
        canvas_draw_str(canvas, 0, 20, fname);
    } else {
        canvas_draw_str(canvas, 0, 20, "No active job");
    }

    /* Progress bar: frame at (0,23) w=88 h=7; filled portion proportional */
    canvas_draw_frame(canvas, 0, 23, 88, 7);
    if(s->progress >= 0.0f) {
        int fill = (int)(84.0f * s->progress / 100.0f);
        if(fill > 0) canvas_draw_box(canvas, 1, 24, fill, 5);
        snprintf(buf, sizeof(buf), "%d%%", (int)(s->progress + 0.5f));
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    canvas_draw_str(canvas, 91, 29, buf);

    /* Temperatures — show 0 actual when target is 0 (heater off) */
    int tool_target = (int)(s->tool_target + 0.5f);
    int bed_target  = (int)(s->bed_target  + 0.5f);
    int tool_actual = tool_target == 0 ? 0 : (int)(s->tool_actual + 0.5f);
    int bed_actual  = bed_target  == 0 ? 0 : (int)(s->bed_actual  + 0.5f);
    snprintf(
        buf,
        sizeof(buf),
        "T:%d/%d  B:%d/%d",
        tool_actual,
        tool_target,
        bed_actual,
        bed_target);
    canvas_draw_str(canvas, 0, 38, buf);

    /* ETA */
    if(s->time_left_s > 0) {
        int h   = s->time_left_s / 3600;
        int min = (s->time_left_s % 3600) / 60;
        if(h > 0)
            snprintf(buf, sizeof(buf), "ETA: %dh %02dm", h, min);
        else
            snprintf(buf, sizeof(buf), "ETA: %dm", min);
        canvas_draw_str(canvas, 0, 47, buf);
    }

    /* OK button hint — bottom-left */
    canvas_draw_circle(canvas, 4, 59, 3);
    canvas_draw_dot(canvas, 4, 59);
    canvas_draw_str(canvas, 10, 62, "Refresh");

    /* OctoPrint version — bottom-right corner */
    if(s->version[0]) {
        snprintf(buf, sizeof(buf), "v%s", s->version);
        canvas_draw_str_aligned(canvas, 127, 62, AlignRight, AlignBottom, buf);
    }
}

View* status_view_alloc(void) {
    View* view = view_alloc();
    view_allocate_model(view, ViewModelTypeLocking, sizeof(StatusModel));
    view_set_draw_callback(view, draw_cb);
    return view;
}

void status_view_free(View* view) {
    view_free(view);
}

void status_view_update(View* view, const PrinterStatus* status) {
    with_view_model(view, StatusModel* m, { m->status = *status; }, true);
}
