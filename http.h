#pragma once

#include <furi.h>
#include "uart.h"

typedef enum {
    HttpResultOk = 0,
    HttpResultTimeout,
    HttpResultNoPong,
    HttpResultError,
} HttpResult;

/* Ping the ESP32 — returns HttpResultOk if [PONG] received within timeout_ms */
HttpResult http_ping(UartContext* uart, FuriStreamBuffer* rx, uint32_t timeout_ms);

/* Send GET request; response body written into out (caller owns it).
 * Returns HttpResultOk on success. */
HttpResult http_get(
    UartContext*      uart,
    FuriStreamBuffer* rx,
    const char*       url,
    FuriString*       out,
    uint32_t          timeout_ms);
