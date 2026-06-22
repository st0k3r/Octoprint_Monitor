#pragma once

#include <furi.h>
#include <furi_hal.h>

/* The WiFi Dev Board is wired to the Flipper expansion UART:
 *   TX → pin 13 (USART1_TX)   RX → pin 14 (USART1_RX)
 * Momentum firmware exposes this as FuriHalSerialIdUsart.
 */

typedef struct UartContext UartContext;

UartContext* uart_alloc(FuriStreamBuffer* rx_stream, FuriThreadId worker_thread_id);
void         uart_free(UartContext* ctx);
void         uart_send(UartContext* ctx, const uint8_t* data, size_t len);
