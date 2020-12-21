#include "sides.h"

#include <stdint.h>

int server_packet_handler(HANDLER_ARGS) {
	int ret;
	uint32_t current=0, parsedint;

	ret=VarIntToUint(buffer + current, &parsedint, (uint8_t)(len - current));
	current+=(uint32_t)ret;

	if(ctx->clientState == Login) {
		if(parsedint == 0x02) {//Login success
			ctx->clientState = Play;
			ctx->serverState = Play;
		}
	} else if(ctx->clientState == Play) {
		replay_write_packet(ctx, buffer, len);
	}

	return 0;
}
