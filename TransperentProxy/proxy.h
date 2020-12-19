#pragma once

#include <stdint.h>
#include <pthread.h>

typedef struct _value_string {
	uint32_t	value;
	const char	*strptr;
} value_string;

enum ConnectionState {
	Handshake=0,
	SLP=1,
	Login=2,
	Play=3
};

typedef struct {
	char *serveraddr;
	uint32_t protocol_version;
	pthread_t thread;
	int client_fd/*Our fd*/, server_fd/*Destination fd*/;
	uint16_t port;
	enum ConnectionState clientState, serverState;
} context;

context* allocate_context(void);
void free_context(context* ctx);

context* find_context(int fd);

void* proxy_connection(void *ctx);
