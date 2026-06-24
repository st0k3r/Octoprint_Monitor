#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_box.h>
#include <gui/modules/popup.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>
#include "parse.h"
#include "uart.h"

#define APP_NAME            "OctoPrint Monitor"
#define CONFIG_DIR          EXT_PATH("apps_data/octoprint_monitor")
#define CONFIG_PATH         CONFIG_DIR "/config.txt"
#define CONFIG_IP_KEY       "ip="
#define CONFIG_APIKEY_KEY   "apikey="
#define CONFIG_MAX_LEN      128

/* UART / FlipperHTTP */
#define FLIPPER_HTTP_BAUD   115200
#define UART_RX_BUF_SIZE    2048
#define HTTP_TIMEOUT_MS     10000

/* FlipperHTTP framing */
#define FH_PING             "[PING]\n"
#define FH_PONG             "[PONG]"
#define FH_GET_END          "[GET/END]"

/* Worker thread event flags */
typedef enum {
    WorkerEvtStop   = (1 << 0),
    WorkerEvtRxDone = (1 << 1),
} WorkerEvtFlags;
#define WORKER_ALL_EVENTS (WorkerEvtStop | WorkerEvtRxDone)

/* View IDs / custom events */
typedef enum {
    ViewStatus  = 0, /* canvas status display — normal operation */
    ViewText,        /* text box for config-missing error        */
    ViewError,       /* popup for runtime errors                 */
    ViewRefresh,     /* user pressed OK to re-fetch data         */
} AppView;

/* App state */
typedef struct {
    /* GUI */
    Gui*            gui;
    ViewDispatcher* view_dispatcher;
    View*           status_view;   /* formatted printer status canvas */
    TextBox*        text_box_main; /* config-missing error text       */
    Popup*          popup_error;

    /* Config */
    char ip[CONFIG_MAX_LEN];
    char apikey[CONFIG_MAX_LEN];

    /* UART */
    FuriStreamBuffer* rx_stream;
    UartContext*      uart;
    FuriThread*       worker_thread;
    bool              worker_running;

    /* Data */
    PrinterStatus status;
    char          error_text[128];
} OctoPrintApp;
