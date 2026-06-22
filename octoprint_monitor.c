#include "octoprint_monitor.h"
#include "config.h"
#include "uart.h"
#include "http.h"

#include <gui/modules/text_box.h>
#include <gui/modules/popup.h>

/* ─── Worker thread ──────────────────────────────────────────────────────── */

typedef struct {
    OctoPrintApp* app;
    UartContext*  uart;
} WorkerCtx;

static int32_t worker_thread_func(void* param) {
    WorkerCtx* wctx = param;
    OctoPrintApp* app  = wctx->app;
    UartContext*  uart = wctx->uart;

    FuriString* result = furi_string_alloc();

    /* Step 1: ping the ESP32 */
    HttpResult r = http_ping(uart, app->rx_stream, 3000);
    if(r != HttpResultOk) {
        furi_string_set_str(app->result_text, "ESP32 not responding.\nCheck WiFi Dev Board.");
        view_dispatcher_send_custom_event(app->view_dispatcher, ViewError);
        furi_string_free(result);
        return 0;
    }

    /* Step 2: GET /api/version */
    char url[256];
    snprintf(url, sizeof(url), "https://%s/api/version", app->ip);

    r = http_get(uart, app->rx_stream, url, result, HTTP_TIMEOUT_MS);

    if(r == HttpResultOk) {
        furi_string_set(app->result_text, result);
        view_dispatcher_send_custom_event(app->view_dispatcher, ViewMain);
    } else {
        furi_string_set_str(app->result_text, "Request timed out.\nCheck IP / network.");
        view_dispatcher_send_custom_event(app->view_dispatcher, ViewError);
    }

    furi_string_free(result);
    return 0;
}

/* ─── Custom event handler ───────────────────────────────────────────────── */

static bool custom_event_callback(void* ctx, uint32_t event) {
    OctoPrintApp* app = ctx;
    if(event == ViewMain) {
        text_box_set_text(app->text_box_main, furi_string_get_cstr(app->result_text));
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewMain);
        return true;
    }
    if(event == ViewError) {
        popup_set_header(app->popup_error, "Error", 64, 10, AlignCenter, AlignTop);
        popup_set_text(
            app->popup_error,
            furi_string_get_cstr(app->result_text),
            64, 32, AlignCenter, AlignCenter);
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
    return VIEW_NONE; /* exit the app */
}

/* ─── App alloc / free ───────────────────────────────────────────────────── */

static OctoPrintApp* app_alloc(void) {
    OctoPrintApp* app = malloc(sizeof(OctoPrintApp));

    app->result_text = furi_string_alloc_set("Loading...");
    app->rx_stream   = furi_stream_buffer_alloc(UART_RX_BUF_SIZE, 1);
    app->worker_running = false;
    app->worker_thread  = NULL;

    /* GUI */
    app->gui             = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, custom_event_callback);
    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    /* Main text box view */
    app->text_box_main = text_box_alloc();
    text_box_set_text(app->text_box_main, "Connecting...");
    text_box_set_focus(app->text_box_main, TextBoxFocusStart);
    View* v_main = text_box_get_view(app->text_box_main);
    view_set_previous_callback(v_main, view_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, ViewMain, v_main);

    /* Error popup view */
    app->popup_error = popup_alloc();
    popup_set_callback(app->popup_error, NULL);
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

    view_dispatcher_remove_view(app->view_dispatcher, ViewMain);
    view_dispatcher_remove_view(app->view_dispatcher, ViewError);
    text_box_free(app->text_box_main);
    popup_free(app->popup_error);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    furi_stream_buffer_free(app->rx_stream);
    furi_string_free(app->result_text);
    free(app);
}

/* ─── Entry point ────────────────────────────────────────────────────────── */

int32_t octoprint_monitor_app(void* p) {
    UNUSED(p);

    OctoPrintApp* app = app_alloc();

    /* Load config first; show error immediately if missing */
    if(!config_load(app)) {
        /* Write a default config so the user knows what to edit */
        strncpy(app->ip,     "192.168.1.100", CONFIG_MAX_LEN - 1);
        strncpy(app->apikey, "", CONFIG_MAX_LEN - 1);
        config_save(app);

        furi_string_set_str(
            app->result_text,
            "No config found.\n"
            "Edit:\n"
            CONFIG_PATH "\n\n"
            "ip=<octoprint_ip>\n"
            "apikey=<api_key>");

        text_box_set_text(app->text_box_main, furi_string_get_cstr(app->result_text));
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewMain);
        view_dispatcher_run(app->view_dispatcher);
        app_free(app);
        return 0;
    }

    /* Show the main view immediately with "Connecting..." */
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewMain);

    /* Allocate worker thread first so we have its ID for the UART callback */
    WorkerCtx* wctx = malloc(sizeof(WorkerCtx));
    wctx->app = app;

    app->worker_thread = furi_thread_alloc_ex(
        "OPMonWorker", 4096, worker_thread_func, wctx);

    /* Init UART with the worker thread ID so the RX callback can signal it */
    UartContext* uart = uart_alloc(
        app->rx_stream, furi_thread_get_id(app->worker_thread));
    wctx->uart = uart;

    furi_thread_start(app->worker_thread);

    /* Run GUI — blocks until view_exit_callback returns VIEW_NONE */
    view_dispatcher_run(app->view_dispatcher);

    /* Cleanup */
    furi_thread_join(app->worker_thread);
    furi_thread_free(app->worker_thread);
    app->worker_thread = NULL;

    uart_free(uart);
    free(wctx);
    app_free(app);

    return 0;
}
