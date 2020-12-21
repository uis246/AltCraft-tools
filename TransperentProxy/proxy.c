#include "proxy.h"

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>

static const value_string protocolId_version[] = {
	{340, "1.12.2"}
};

struct pktbuf {
	uint8_t *buffer;//Packet buffer
	uint32_t size;//Size of packet buffer
	uint32_t offset, left;
	uint8_t lenleft;
	uint8_t padding[3];
};

//0 - full packet avaliable
//-1 - read more
//-2 - connection closed
//-3 - an error occured
static int read_packet(struct pktbuf *buf, int fd) {
	ssize_t ret;
	if(!buf->left) {//If read new packet
		if(!buf->lenleft)//Varint overflow
			abort();

		ret=recv(fd, buf->buffer+5-buf->lenleft, 1, MSG_DONTWAIT);
		if(ret == -1)
			return -3;//Let's close connection
		else if(ret == 0)
			return -2;//Connection closed

		buf->lenleft-=ret;
		uint32_t len;
		ret=VarIntToUint(buf->buffer, &len, 5-buf->lenleft);
		if(ret==-1)//FIXME: split wait for buffer and failed to parse
			return -1;//Not enough readed bytes, I think
		len+=(uint32_t)ret;

		if(len > buf->size) {
			buf->buffer=realloc(buf->buffer, len);
			buf->size=len;
		}
		buf->offset	= 5 - buf->lenleft;
		buf->left	= len - buf->offset;
		buf->lenleft	= 5;
	}
	//Continue buffer reading
	ret=recv(fd, buf->buffer+buf->offset, buf->left, MSG_DONTWAIT);
	if(ret==-1)
		return -3;
	else if(ret == 0)
		return -2;
	buf->offset+=(uint32_t)ret;
	buf->left-=(uint32_t)ret;
	if(buf->left==0){
		return 0;
	} else
		return -1;
}

void* proxy_connection(void *cont) {
	__label__ closeconn, failconnect;
	context *ctx = (context*)cont;
	struct pollfd pfd[2] = {0};
	int ret;

	struct pktbuf clientbuf = {0}, serverbuf = {0};
	clientbuf.lenleft=5;
	clientbuf.buffer=malloc(64);
	clientbuf.size=64;
	serverbuf.lenleft=5;
	serverbuf.buffer=malloc(64);
	serverbuf.size=64;

	pfd[0].fd=ctx->client_fd;
	pfd[0].events=POLLIN;
	pfd[1].events=POLLIN;
	while(1) {
		ret=poll(pfd, 1+(!!ctx->server_fd), -1);
		if(ret==-1) {
			perror("poll() failed: ");
			continue;
		}

		if(pfd[0].revents&POLLIN) {
			ret=read_packet(&clientbuf, pfd[0].fd);
			if(ret == -2) {
				printf("Client closed connection\n");
				goto closeconn;
			} else if(ret == -3) {
				printf("Client network error\n");
				goto closeconn;
			} else if(ret == 0) {
				uint32_t current=0, parsedint;
				//Read packet len
				ret=VarIntToUint(clientbuf.buffer, &parsedint, (uint8_t)clientbuf.offset);
				current+=(uint32_t)ret;

				printf("[C->P] %d bytes, ", clientbuf.offset);

				//Read packetid
				ret=VarIntToUint(clientbuf.buffer + current, &parsedint, (uint8_t)(clientbuf.offset - current));
				current+=(uint32_t)ret;
				if(ret==-1) {
					printf("failed to parse PacketID\n");
					abort();
				}
				printf("0x%.2X\n", parsedint);

				if(ctx->clientState == Handshake) {
					if(parsedint == 0) {
						//Protocol version
						ret = VarIntToUint(clientbuf.buffer + current, &parsedint, (uint8_t)(clientbuf.offset - current));
						current += (uint32_t)ret;
						ctx->protocol_version = parsedint;

						//Server address len
						//Should be lower or euqal 255*4=1020
						ret = VarIntToUint(clientbuf.buffer + current, &parsedint, (uint8_t)(clientbuf.offset - current));
						current += (uint32_t)ret;

						ctx->serveraddr = malloc(parsedint+1);//Null termination
						memcpy(ctx->serveraddr, clientbuf.buffer + current, parsedint);
						ctx->serveraddr[parsedint] = 0;
						current += parsedint;

						ctx->port = (uint16_t)(( *(clientbuf.buffer+current)) << 8) | *(clientbuf.buffer+current+1);
						current += 2;
						if(ctx->port == 25545)
							ctx->port = 25565;

						ret = VarIntToUint(clientbuf.buffer + current, &parsedint, (uint8_t)(clientbuf.offset - current));
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
							goto closeconn;
						}

						if (ai->ai_family == AF_INET)
							((struct sockaddr_in*)ai->ai_addr)->sin_port=htons(ctx->port);
						else if (ai->ai_family == AF_INET6)
							((struct sockaddr_in6*)ai->ai_addr)->sin6_port=htons(ctx->port);

						ctx->server_fd = socket(ai->ai_family, SOCK_STREAM, IPPROTO_TCP);
						pfd[1].fd = ctx->server_fd;
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
							goto closeconn;
						}

						freeaddrinfo(ai);
						ctx->serverState = ctx->clientState;
					} else {
						printf("Failed to parse handshake\n");
						goto closeconn;
					}
				}

				send(ctx->server_fd, clientbuf.buffer, clientbuf.offset, MSG_DONTWAIT | MSG_NOSIGNAL);

				clientbuf.offset = 0;
			}
		}
		if(pfd[1].revents&POLLIN) {
			ret = read_packet(&serverbuf, pfd[1].fd);
			if(ret == -2) {
				printf("Server closed connection\n");
				goto closeconn;
			} else if(ret == -3) {
				printf("Server network error\n");
				goto closeconn;
			} else if(ret == 0) {
				uint32_t current=0, parsedint;
				//Read packet len
				ret=VarIntToUint(serverbuf.buffer, &parsedint, (uint8_t)serverbuf.offset);
				current+=(uint32_t)ret;

				printf("[S->P] %d bytes, ", serverbuf.offset);

				//Read packetid
				ret=VarIntToUint(serverbuf.buffer + current, &parsedint, (uint8_t)(serverbuf.offset - current));
				current+=(uint32_t)ret;
				if(ret==-1) {
					printf("failed to parse PacketID\n");
					abort();
				}
				printf("0x%.2X\n", parsedint);

				if(ctx->clientState == Login) {
					if(parsedint == 0x02) {//Login success
						ctx->clientState = Play;
						ctx->serverState = Play;
					}
				}
				send(ctx->client_fd, serverbuf.buffer, serverbuf.offset, MSG_DONTWAIT | MSG_NOSIGNAL);

				serverbuf.offset = 0;
			}

		}

		pfd[0].revents=0;
		pfd[1].revents=0;
	}

	closeconn:
	close(pfd[0].fd);
	if(pfd[1].fd)
		close(pfd[1].fd);

	free(clientbuf.buffer);
	free(serverbuf.buffer);

	free_context(ctx);//TODO: use mutex to prevent data race

	return NULL;
}
