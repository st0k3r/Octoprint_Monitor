#include "uart.h"
#include "octoprint_monitor.h"
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>

#define UART_ID     FuriHalSerialIdUsart
#define BAUD_RATE   115200

struct UartContext {
    FuriHalSerialHandle* handle;
    FuriStreamBuffer*    rx_stream;
    FuriThreadId         worker_thread_id;
};

/* IRQ callback — runs in interrupt context */
static void uart_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* ctx)
{
    UNUSED(handle);
    UartContext* u = ctx;
    if(event == FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(u->handle);
        furi_stream_buffer_send(u->rx_stream, &byte, 1, 0);
        furi_thread_flags_set(u->worker_thread_id, WorkerEvtRxDone);
    }
}

UartContext* uart_alloc(FuriStreamBuffer* rx_stream, FuriThreadId worker_thread_id) {
    UartContext* u = malloc(sizeof(UartContext));
    u->rx_stream        = rx_stream;
    u->worker_thread_id = worker_thread_id;

    u->handle = furi_hal_serial_control_acquire(UART_ID);
    furi_hal_serial_init(u->handle, BAUD_RATE);
    furi_hal_serial_async_rx_start(u->handle, uart_rx_callback, u, false);

    return u;
}

void uart_free(UartContext* ctx) {
    furi_hal_serial_async_rx_stop(ctx->handle);
    furi_hal_serial_deinit(ctx->handle);
    furi_hal_serial_control_release(ctx->handle);
    free(ctx);
}

void uart_send(UartContext* ctx, const uint8_t* data, size_t len) {
    furi_hal_serial_tx(ctx->handle, data, len);
}
