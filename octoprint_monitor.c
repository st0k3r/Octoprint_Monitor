#include "octoprint_monitor.h"
#include "config.h"
#include "uart.h"
#include "http.h"
#include "parse.h"
#include "status_view.h"

#include <gui/modules/text_box.h>
#include <gui/modules/popup.h>
#include <string.h>

/* ─── Worker thread ──────────────────────────────────────────────────────── */

typedef struct {
    OctoPrintApp* app;
    UartContext*  uart;
} WorkerCtx;

static WorkerCtx s_wctx;

static int32_t worker_thread_func(void* param) {
    WorkerCtx*    wctx = param;
    OctoPrintApp* app  = wctx->app;
    UartContext*  uart = wctx->uart;

    FuriString* response = furi_string_alloc();
    char url[320];

    /* Step 1: verify the ESP32 is alive */
    HttpResult r = http_ping(uart, app->rx_stream, 3000);
    if(r != HttpResultOk) {
        strncpy(
            app->error_text,
            "ESP32 not responding.\nCheck WiFi Dev Board.",
            sizeof(app->error_text) - 1);
        app->worker_running = false;
        view_dispatcher_send_custom_event(app->view_dispatcher, ViewError);
        furi_string_free(response);
        return 0;
    }

    /* Step 2: /api/version — no auth required */
    snprintf(url, sizeof(url), "https://%s/api/version", app->ip);
    r = http_get(uart, app->rx_stream, url, response, HTTP_TIMEOUT_MS);
    if(r == HttpResultOk) parse_version_json(furi_string_get_cstr(response), &app->status);

    /* Step 3: /api/printer — requires API key */
    if(app->apikey[0])
        snprintf(url, sizeof(url), "https://%s/api/printer?apikey=%s", app->ip, app->apikey);
    else
        snprintf(url, sizeof(url), "https://%s/api/printer", app->ip);
    r = http_get(uart, app->rx_stream, url, response, HTTP_TIMEOUT_MS);
    if(r == HttpResultOk) {
        parse_printer_json(furi_string_get_cstr(response), &app->status);
        if(app->status.state[0] == '\0')
            strncpy(app->status.state, "Offline", sizeof(app->status.state) - 1);
    } else {
        strncpy(app->status.state, "Timeout", sizeof(app->status.state) - 1);
    }

    /* Step 4: /api/job — requires API key */
    if(app->apikey[0])
        snprintf(url, sizeof(url), "https://%s/api/job?apikey=%s", app->ip, app->apikey);
    else
        snprintf(url, sizeof(url), "https://%s/api/job", app->ip);
    r = http_get(uart, app->rx_stream, url, response, HTTP_TIMEOUT_MS);
    if(r == HttpResultOk) parse_job_json(furi_string_get_cstr(response), &app->status);

    app->status.loaded  = true;
    app->worker_running = false;
    view_dispatcher_send_custom_event(app->view_dispatcher, ViewStatus);

    furi_string_free(response);
    return 0;
}

/* ─── Worker helpers ─────────────────────────────────────────────────────── */

static void start_worker(OctoPrintApp* app) {
    s_wctx.app = app;

    app->worker_running = true;
    app->worker_thread  = furi_thread_alloc_ex(
        "OPMonWorker", 4096, worker_thread_func, &s_wctx);

    app->uart      = uart_alloc(app->rx_stream, furi_thread_get_id(app->worker_thread));
    s_wctx.uart    = app->uart;

    furi_thread_start(app->worker_thread);
}

static bool status_input_cb(InputEvent* event, void* ctx) {
    OctoPrintApp* app = ctx;
    if(event->key == InputKeyOk && event->type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ViewRefresh);
        return true;
    }
    return false;
}

/* ─── Custom event handler ───────────────────────────────────────────────── */

static bool custom_event_callback(void* ctx, uint32_t event) {
    OctoPrintApp* app = ctx;

    if(event == ViewStatus) {
        status_view_update(app->status_view, &app->status);
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewStatus);
        return true;
    }
    if(event == ViewRefresh) {
        if(!app->worker_running) {
            furi_thread_join(app->worker_thread);
            furi_thread_free(app->worker_thread);
            app->worker_thread = NULL;

            uart_free(app->uart);
            app->uart = NULL;

            app->status.loaded      = false;
            app->status.progress    = -1.0f;
            app->status.time_left_s = -1;
            status_view_update(app->status_view, &app->status);

            start_worker(app);
        }
        return true;
    }
    if(event == ViewError) {
        popup_set_header(app->popup_error, "Error", 64, 10, AlignCenter, AlignTop);
        popup_set_text(
            app->popup_error, app->error_text, 64, 32, AlignCenter, AlignCenter);
        popup_set_timeout(app->popup_error, 4000);
        popup_enable_timeout(app->popup_error);
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewError);
        return true;
    }
    return false;
}

/* ─── Navigation callbacks ───────────────────────────────────────────────── */

static uint32_t view_exit_callback(void* ctx) {
    UNUSED(ctx);
    return VIEW_NONE;
}

/* ─── App alloc / free ───────────────────────────────────────────────────── */

static OctoPrintApp* app_alloc(void) {
    OctoPrintApp* app = malloc(sizeof(OctoPrintApp));
    memset(app, 0, sizeof(OctoPrintApp));

    app->rx_stream          = furi_stream_buffer_alloc(UART_RX_BUF_SIZE, 1);
    app->status.progress    = -1.0f;
    app->status.time_left_s = -1;

    /* GUI */
    app->gui             = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, custom_event_callback);
    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    /* Canvas status view (normal operation) */
    app->status_view = status_view_alloc();
    view_set_previous_callback(app->status_view, view_exit_callback);
    view_set_input_callback(app->status_view, status_input_cb);
    view_set_context(app->status_view, app);
    view_dispatcher_add_view(app->view_dispatcher, ViewStatus, app->status_view);

    /* Text box (config-missing error) */
    app->text_box_main = text_box_alloc();
    text_box_set_focus(app->text_box_main, TextBoxFocusStart);
    View* v_text = text_box_get_view(app->text_box_main);
    view_set_previous_callback(v_text, view_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, ViewText, v_text);

    /* Error popup */
    app->popup_error = popup_alloc();
    View* v_err = popup_get_view(app->popup_error);
    view_set_previous_callback(v_err, view_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, ViewError, v_err);

    return app;
}

static void app_free(OctoPrintApp* app) {
    if(app->worker_thread) {
        furi_thread_join(app->worker_thread);
        furi_thread_free(app->worker_thread);
    }

    view_dispatcher_remove_view(app->view_dispatcher, ViewStatus);
    view_dispatcher_remove_view(app->view_dispatcher, ViewText);
    view_dispatcher_remove_view(app->view_dispatcher, ViewError);
    status_view_free(app->status_view);
    text_box_free(app->text_box_main);
    popup_free(app->popup_error);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    furi_stream_buffer_free(app->rx_stream);
    free(app);
}

/* ─── Entry point ────────────────────────────────────────────────────────── */

int32_t octoprint_monitor_app(void* p) {
    UNUSED(p);

    OctoPrintApp* app = app_alloc();

    if(!config_load(app)) {
        strncpy(app->ip, "192.168.1.100", CONFIG_MAX_LEN - 1);
        strncpy(app->apikey, "", CONFIG_MAX_LEN - 1);
        config_save(app);

        text_box_set_text(
            app->text_box_main,
            "No config found.\n"
            "Edit:\n"
            CONFIG_PATH "\n\n"
            "ip=<octoprint_ip>\n"
            "apikey=<api_key>");
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewText);
        view_dispatcher_run(app->view_dispatcher);
        app_free(app);
        return 0;
    }

    /* Show status view immediately — draw_cb renders "Connecting..." */
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewStatus);

    start_worker(app);

    /* Blocks until the user presses Back */
    view_dispatcher_run(app->view_dispatcher);

    furi_thread_join(app->worker_thread);
    furi_thread_free(app->worker_thread);
    app->worker_thread = NULL;

    uart_free(app->uart);
    app->uart = NULL;

    app_free(app);

    return 0;
}
