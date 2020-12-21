#include "proxy.h"

#include <stdlib.h>
#include <memory.h>

struct context_list {
	struct context_list *next;
	context ctx;
};

static struct context_list *root=0;

context* allocate_context() {
	struct context_list *new=malloc(sizeof(struct context_list));
	if(new == NULL)
		abort();
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
	//Close replay
	replay_free_context(ctx);

	//Free memory
	if(ctx->serveraddr)
		free(ctx->serveraddr);

	//Find ctx in list
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

int8_t VarIntToUint(const uint8_t *varint, uint32_t *result, uint8_t maxlen){
	uint8_t i=0;
	*result=0;
	do{
		if(i>5)
			return -1;
		if(i>=maxlen)
			return -1;
		*result |= (uint32_t)(varint[i]&0x7F) << (i*7);
	}while((varint[i++]&0x80) != 0);
	return (int8_t)i;
}

void prepare_disconnect_buffer(uint8_t *restrict buffer, const char *restrict reason, const uint8_t strlen) {
	if (strlen > 127-3)
		abort();
	buffer[0] = strlen + 2;
	buffer[1] = 0;
	buffer[2] = strlen;
	memcpy(buffer + 3, reason, strlen);
}
