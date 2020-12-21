#include "sides.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <unistd.h>
#include <netdb.h>

static const value_string protocolId_version[] = {
	{340, "1.12.2"}
};

int client_packet_handler(HANDLER_ARGS) {
	int ret;
	uint32_t current=0, parsedint;

	ret=VarIntToUint(buffer + current, &parsedint, (uint8_t)len);
	current+=(uint32_t)ret;

	if(ctx->clientState == Handshake) {
		if(parsedint == 0) {
			//Protocol version
			ret = VarIntToUint(buffer + current, &parsedint, (uint8_t)(len - current));
			current += (uint32_t)ret;
			ctx->protocol_version = parsedint;

			//Server address len
			//Should be lower or euqal 255*4=1020
			ret = VarIntToUint(buffer + current, &parsedint, (uint8_t)(len - current));
			current += (uint32_t)ret;

			ctx->serveraddr = malloc(parsedint+1);//Null termination
			if(ctx->serveraddr == NULL)
				return -1;
			memcpy(ctx->serveraddr, buffer + current, parsedint);
			ctx->serveraddr[parsedint] = 0;
			current += parsedint;

			ctx->port = (uint16_t)(( *(buffer+current)) << 8) | *(buffer+current+1);
			current += 2;
			if(ctx->port == 25545)
				ctx->port = 25565;

			ret = VarIntToUint(buffer + current, &parsedint, (uint8_t)(len - current));
			current += (uint32_t)ret;
			if(parsedint == 1)
				ctx->clientState = SLP;
			else if(parsedint == 2)
				ctx->clientState = Login;

			printf("Connecting to %s:%u\nProtocol version: %u\n", ctx->serveraddr, ctx->port, ctx->protocol_version);

			int id = -1;
			for(size_t i = 0; i < sizeof(protocolId_version)/sizeof(protocolId_version[0]); i++) {
				if(protocolId_version[i].value == ctx->protocol_version)
					id = (int)i;
			}

			if(id == -1)
				printf("Unknown game version\n");
			else
				printf("Game version: %s\n", protocolId_version[id].strptr);

			struct addrinfo *ai;
			ret = getaddrinfo(ctx->serveraddr, NULL, NULL, &ai);

			if(ret) {
				printf("Client tired to connect unknown to server\n");
				freeaddrinfo(ai);
				if(ctx->clientState == Login) {
					//Send disconnect packet
					static const char *reason = "{\"text\":\"Bad address\"}";
					const size_t buflen = strlen(reason) + 3;
					uint8_t *tmpbuf = alloca(buflen);
					prepare_disconnect_buffer(tmpbuf, reason, (uint8_t)(buflen - 3));
					send(ctx->client_fd, tmpbuf, buflen, MSG_NOSIGNAL);
				}
				return -1;
			}

			if (ai->ai_family == AF_INET)
				((struct sockaddr_in*)ai->ai_addr)->sin_port=htons(ctx->port);
			else if (ai->ai_family == AF_INET6)
				((struct sockaddr_in6*)ai->ai_addr)->sin6_port=htons(ctx->port);

			ctx->server_fd = socket(ai->ai_family, SOCK_STREAM, IPPROTO_TCP);
			if(ctx->server_fd == -1) {
				goto failconnect;
			}

			ret = connect(ctx->server_fd, ai->ai_addr, ai->ai_addrlen);
			if(ret == -1) {
				failconnect:
				close(ctx->server_fd);
				printf("Failed to connect\n");
				freeaddrinfo(ai);
				if(ctx->clientState == Login) {
					//Send disconnect packet
					static const char *reason = "{\"text\":\"Failed to connect\"}";
					const size_t buflen = strlen(reason) + 3;
					uint8_t *tmpbuf = alloca(buflen);
					prepare_disconnect_buffer(tmpbuf, reason, (uint8_t)(buflen - 3));
					send(ctx->client_fd, tmpbuf, buflen, MSG_NOSIGNAL);
				}
				return -1;
			}

			freeaddrinfo(ai);
			ctx->serverState = ctx->clientState;

			//Init replay
			if(id != -1)
				replay_init_context(ctx, protocolId_version[id].strptr/*, buffer, len*/);

			return 1;
		} else {
			printf("Failed to parse handshake\n");
			return -1;
		}
	}
	return 0;
}
