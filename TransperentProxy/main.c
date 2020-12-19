#include "platform.h"
#include "proxy.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

static int listen_fd;

static const int z=0, o=1;

static pthread_attr_t attribute;

static void sigpoll_handler(int signum _U_, siginfo_t *info, void *ucontext _U_) {
	int fd=info->si_fd;
	if(fd==listen_fd) {
		//accept
		fd=accept(fd, NULL, NULL);
		if(fd==-1) {
			perror("accept() failed: ");
			return;
		}

//		setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN_CONNECT, &o/*TCP Fast Open*/, sizeof(o));
		setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &o/*TCP Quick ACK*/, sizeof(o));
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &o/*Nagle disabled*/, sizeof(o));
		setsockopt(fd, IPPROTO_TCP, TCP_THIN_DUPACK, &o/*Thin stream enabled*/, sizeof(o));
		setsockopt(fd, IPPROTO_TCP, TCP_THIN_LINEAR_TIMEOUTS, &o/*Thin stream*/, sizeof(o));

		context *ctx=allocate_context();
		ctx->client_fd=fd;
		ctx->server_fd=0;
		ctx->clientState=Handshake;
		ctx->serverState=Handshake;

		int ret=pthread_create(&ctx->thread, &attribute, proxy_connection, ctx);
		if(ret==-1) {
			perror("pthread_create() failed: ");
			return;
		}
	}
}

int main() {
	pthread_attr_init(&attribute);
	pthread_attr_setdetachstate(&attribute, PTHREAD_CREATE_DETACHED);
	pthread_attr_setscope(&attribute, PTHREAD_SCOPE_PROCESS);

	const struct sockaddr_in in={
		.sin_family = AF_INET,
		.sin_port=htons(25545),
		.sin_addr={INADDR_ANY}
	};
	struct sigaction sa;

	listen_fd=socket(AF_INET, SOCK_STREAM, 0);
	int retval = bind(listen_fd, (const struct sockaddr*)&in, sizeof(in));
	if(retval==-1) {
		perror("bind() failed: ");
		return -1;
	}
	retval = listen(listen_fd, 10);
	if(retval==-1) {
		perror("listen() failed: ");
		return -1;
	}

	sigaction(SIGPOLL, NULL, &sa);
	sa.sa_sigaction=sigpoll_handler;
	sa.sa_flags=SA_SIGINFO|SA_RESTART;
	sigaction(SIGPOLL, &sa, NULL);

	fcntl(listen_fd, F_SETOWN, getpid());
	fcntl(listen_fd, F_SETSIG, SIGPOLL);
	fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL) | FASYNC);

	printf("Waiting for connections...\n");
	while(1)
		pause();
}
