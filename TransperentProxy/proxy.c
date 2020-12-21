#include "proxy.h"
#include "sides.h"

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>

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





static int recive_packet(struct pktbuf *restrict buffer, context *restrict ctx, int sourcefd, int destfd, int (*handler)(HANDLER_ARGS), const char *restrict sideName) {
	int ret;
	ret = read_packet(buffer, sourcefd);
	if(ret == -2) {
		printf("%s closed connection\n", sideName);
		return -1;
	} else if(ret == -3) {
		printf("%s network error\n", sideName);
		return -1;
	} else if(ret == 0) {
		uint32_t current=0, parsedint;
		//Read packet len
		ret=VarIntToUint(buffer->buffer, &parsedint, (uint8_t)buffer->offset);
		current+=(uint32_t)ret;

		printf("%d: [%s->Proxy] %d bytes, ", ctx->client_fd, sideName, buffer->offset);

		//Print packetid
		ret=VarIntToUint(buffer->buffer + current, &parsedint, (uint8_t)(buffer->offset - current));
		if(ret==-1) {
			printf("failed to parse PacketID\n");
			abort();
		}
		printf("0x%.2X\n", parsedint);

		ret = handler(ctx, buffer->buffer + current, buffer->offset - current);
		if(ret == -1)
			return -1;
		else if(ret == 1)
			destfd = ctx->server_fd;

		send(destfd, buffer->buffer, buffer->offset, MSG_DONTWAIT | MSG_NOSIGNAL);

		buffer->offset = 0;
		return ret;
	}
	return 0;
}



void* proxy_connection(void *cont) {
	__label__ closeconn;

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
			ret = recive_packet(&clientbuf, ctx, pfd[0].fd, pfd[1].fd, client_packet_handler, "Client");
			if(ret == 1)
				pfd[1].fd = ctx->server_fd;
			else if(ret == -1)
				goto closeconn;
		}
		if(pfd[1].revents&POLLIN) {
			ret = recive_packet(&serverbuf, ctx, pfd[1].fd, pfd[0].fd, server_packet_handler, "Server");
			if(ret == -1)
				goto closeconn;
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
