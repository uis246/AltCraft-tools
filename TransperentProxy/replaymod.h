#pragma once

#include <stdint.h>

#include <sys/time.h>

struct replay_context {
	int replayfileFD;
	int replayinfoFD;
	struct timeval startTime;
};

struct context;

void replay_init_context(struct context *ctx, const char *restrict version/*, const uint8_t *restrict buffer, uint32_t len*/);
void replay_free_context(struct context *ctx);

void replay_write_packet(struct context *ctx, const uint8_t *restrict buffer, uint32_t len);
