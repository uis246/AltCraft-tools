#pragma once

#include "proxy.h"

#define HANDLER_ARGS context *restrict ctx, const uint8_t *restrict buffer, const uint32_t len

int server_packet_handler(HANDLER_ARGS);
int client_packet_handler(HANDLER_ARGS);
