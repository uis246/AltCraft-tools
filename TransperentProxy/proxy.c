#include "proxy.h"

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

static const value_string protocolId_version[] = {
	{340, "1.12.2"}
};

struct pktbuf {
	void *buffer;//Packet buffer
	uint32_t size;//Size of packet buffer
	uint32_t offset, left;
	uint8_t lenleft;
	uint8_t padding[3];
};

static int8_t VarIntToUint(const uint8_t *varint, uint32_t *result, uint8_t maxlen){
	uint8_t i=0;
	*result=0;
	do{
		if(i>5)
			return -1;
		if(i>maxlen)
			return -1;
		*result |= (uint32_t)(varint[i]&0x7F) << (i*7);
	}while((varint[i++]&0x80) != 0);
	return (int8_t)i;
}

//int read_packet(struct pktbuf *buf, int fd) {
//	int ret;
//	if(!buf->left) {
//		if(!buf->lenleft)
//			abort();

//		ret=recv(fd, buf->lenbuf+5-buf->lenleft, buf->lenleft, MSG_DONTWAIT);
//		if(ret==-1)
//			return -1;

//		uint32_t len;
//		ret=VarIntToUint(buf->lenbuf, &len, ret);
//		if(ret==-1) {
//			buf->lenleft-=ret;
//			return -1;
//		}
//		len+=ret;

//		if(len>buf->size) {
//			free(buf->buffer);
//			buf->buffer=malloc(len);
//			buf->size=len;
//		}
//		buf->offset=5-buf->lenleft;
//		buf->left=len;

//		memcpy(buf->buffer, buf->lenbuf, 5-buf->lenleft);
//	} else {
//		ret=recv(fd, buf->buffer+buf->offset, buf->left, MSG_DONTWAIT);
//		if(ret==-1)
//			return -1;
//		buf->left-=ret;
//		if(buf->left==0){
//			//Parse
//		}
//	}
//}

static int read_packet(struct pktbuf *buf, int fd) {
	ssize_t ret;
	if(!buf->left) {//If read new packet
		if(!buf->lenleft)//Varint overflow
			abort();

		ret=recv(fd, buf->buffer+5-buf->lenleft, buf->lenleft, MSG_DONTWAIT);
		if(ret == -1)
			return -1;//Waaaait
		else if(ret == 0)
			return -2;//Connection closed

		buf->lenleft-=ret;
		uint32_t len;
		ret=VarIntToUint(buf->buffer, &len, 5-buf->lenleft);
		if(ret==-1)//FIXME: split wait for buffer and failed to parse
			return -1;//Not enough readed bytes, I think
		len+=(uint32_t)ret;

		if(len + ret > buf->size) {
			buf->buffer=realloc(buf->buffer, len);
			buf->size=len;
		}
		buf->offset	= 5 - buf->lenleft;
		buf->left	= len - (uint32_t)ret;
		buf->lenleft	= 5;
		return -1;
	} else {//Continue buffer reading
		ret=recv(fd, buf->buffer+buf->offset, buf->left, MSG_DONTWAIT);
		if(ret==-1)
			return -1;
		else if(ret == 0)
			return -2;
		buf->offset+=(uint32_t)ret;
		buf->left-=(uint32_t)ret;
		if(buf->left==0){
			return 0;
		} else
			return -1;
	}
}

void* proxy_connection(void *cont) {
	context *ctx = (context*)cont;
	struct pollfd pfd[2];
	int ret;

	struct pktbuf clientbuf, serverbuf={0};
	clientbuf.lenleft=5;
	clientbuf.buffer=malloc(5);
	clientbuf.size=5;
	serverbuf.lenleft=5;
	serverbuf.buffer=malloc(5);
	serverbuf.size=5;

	pfd[0].fd=ctx->client_fd;
	pfd[0].events=POLLIN;
	pfd[1].events=POLLIN;
	pfd[1].revents=0;
	while(1) {
		ret=poll(pfd, 1+(!!ctx->server_fd), -1);
		if(ret==-1) {
			perror("poll() failed: ");
			continue;
		}

		if(pfd[0].revents&POLLIN) {
			ret=read_packet(&clientbuf, pfd[0].fd);
			if(ret==0) {
				uint32_t current=0, parsedint;
				ret=VarIntToUint(clientbuf.buffer, &parsedint, (uint8_t)clientbuf.offset);
				current+=(uint32_t)ret;

				printf("[C->P] %d bytes, ", clientbuf.offset);
				ret=VarIntToUint(clientbuf.buffer + current, &parsedint, (uint8_t)(clientbuf.offset - current));
				if(ret==-1) {
					printf("failed to parse PacketID\n");
					abort();
				}
				printf("0x%.2X\n", parsedint);
				if(ctx->clientState==Handshake) {
				}
			} else if(ret == -2) {
				printf("Connection closed by client\n");
				close(pfd[0].fd);
				if(pfd[1].fd)
					close(pfd[1].fd);
			}
		}
		if(pfd[1].revents&POLLIN) {
			ret=read_packet(&serverbuf, pfd[0].fd);
		}
	}
}













struct context_list {
	struct context_list *next;
	context ctx;
};

static struct context_list *root=0;

context* allocate_context() {
	struct context_list *new=malloc(sizeof(struct context_list));
	memset(&new->ctx, 0, sizeof(new->ctx));
	if(!root)
		return &(root=new)->ctx;
	else {
		struct context_list *ctx=root;
		for(;ctx->next!=0;)//Select last list entry
			ctx=ctx->next;
		new->next=0;
		ctx->next=new;//Add to list
		return &new->ctx;
	}
}

void free_context(context* ctx) {
	struct context_list *this=root, *prev=0;

	for(;&this->ctx!=ctx;) {
		prev=this;
		this=prev->next;
	}

	//Remove ctx from list
	if(prev&&this->next)
		prev->next=this->next;

	free(this);

	if(this==root)
		root=0;
}

context* find_context(int fd) {
	struct context_list *ctx=root;
	for(;ctx->next!=0;) {
		if(ctx->ctx.client_fd==fd||ctx->ctx.server_fd==fd)
			return &ctx->ctx;
		ctx=ctx->next;
	}

	return NULL;
}
