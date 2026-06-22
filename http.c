#include "http.h"
#include "octoprint_monitor.h"
#include <string.h>

/* Read from the stream until needle is found or timeout expires.
 * Sleeps on WorkerEvtRxDone between drain passes — no spin-polling.
 * Returns true if needle was found. */
static bool wait_for_token(
    FuriStreamBuffer* rx,
    FuriString*       buf,
    const char*       needle,
    uint32_t          timeout_ms)
{
    uint32_t deadline_ticks = furi_get_tick() + furi_ms_to_ticks(timeout_ms);
    uint8_t  tmp[64];

    while(furi_get_tick() < deadline_ticks) {
        uint32_t remaining = deadline_ticks - furi_get_tick();

        /* Sleep until a byte arrives or we run out of time */
        furi_thread_flags_wait(WorkerEvtRxDone, FuriFlagWaitAny, remaining);

        /* Drain everything available right now */
        size_t got;
        while((got = furi_stream_buffer_receive(rx, tmp, sizeof(tmp), 0)) > 0) {
            for(size_t i = 0; i < got; i++) {
                furi_string_push_back(buf, (char)tmp[i]);
            }
        }

        if(furi_string_search_str(buf, needle, 0) != FURI_STRING_FAILURE) {
            return true;
        }
    }
    return false;
}

HttpResult http_ping(UartContext* uart, FuriStreamBuffer* rx, uint32_t timeout_ms) {
    /* Drain stale bytes */
    uint8_t drain[16];
    while(furi_stream_buffer_receive(rx, drain, sizeof(drain), 0) > 0) {}

    uart_send(uart, (const uint8_t*)FH_PING, strlen(FH_PING));

    FuriString* tmp = furi_string_alloc();
    bool found = wait_for_token(rx, tmp, FH_PONG, timeout_ms);
    furi_string_free(tmp);

    return found ? HttpResultOk : HttpResultNoPong;
}

HttpResult http_get(
    UartContext*      uart,
    FuriStreamBuffer* rx,
    const char*       url,
    FuriString*       out,
    uint32_t          timeout_ms)
{
    /* FlipperHTTP GET command: "[GET]<url>\n" */
    char cmd[512];
    int  len = snprintf(cmd, sizeof(cmd), "[GET]%s\n", url);
    if(len <= 0 || (size_t)len >= sizeof(cmd)) return HttpResultError;

    /* Drain stale bytes */
    uint8_t drain[16];
    while(furi_stream_buffer_receive(rx, drain, sizeof(drain), 0) > 0) {}

    uart_send(uart, (const uint8_t*)cmd, (size_t)len);

    /* Collect response until [GET/END] */
    furi_string_reset(out);
    if(!wait_for_token(rx, out, FH_GET_END, timeout_ms)) {
        return HttpResultTimeout;
    }

    /* Strip the trailing [GET/END] sentinel and any surrounding whitespace */
    size_t end_pos = furi_string_search_str(out, FH_GET_END, 0);
    if(end_pos != FURI_STRING_FAILURE) {
        furi_string_left(out, end_pos);
    }

    /* Strip the leading [GET/SUCCESS] acknowledgment line if present */
    const char* success_marker = "[GET/SUCCESS]";
    size_t suc_pos = furi_string_search_str(out, success_marker, 0);
    if(suc_pos != FURI_STRING_FAILURE) {
        /* Skip past the marker and the newline that follows it */
        size_t skip = suc_pos + strlen(success_marker);
        const char* raw = furi_string_get_cstr(out);
        if(raw[skip] == '\r') skip++;
        if(raw[skip] == '\n') skip++;
        FuriString* body = furi_string_alloc_set(raw + skip);
        furi_string_set(out, body);
        furi_string_free(body);
    }

    furi_string_trim(out);
    return HttpResultOk;
}
