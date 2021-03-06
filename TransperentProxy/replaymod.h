#pragma once

#include <stdint.h>

#include <sys/time.h>

struct replay_context {
	const char *versionName;
	uint8_t **UUIDs;
	uint32_t uuidcount, uuidused;
	int replayfileFD;
	int replayinfoFD;
	struct timeval startTime;//msecs in tv_usec
};

struct context;

void replay_init_context(struct context *ctx, const char *restrict version/*, const uint8_t *restrict buffer, uint32_t len*/);
void replay_free_context(struct context *ctx);

void replay_write_packet(struct context *ctx, const uint8_t *restrict buffer, uint32_t len);
void replay_add_uuid(struct context *ctx, const uint8_t *restrict buffer);
