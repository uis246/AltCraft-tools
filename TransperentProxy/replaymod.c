#include "replaymod.h"
#include "proxy.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/stat.h>

#include <endian.h>

static const char json_format[]="{\"singleplayer\":false,\"fileFormat\":\"MCPR\",\"fileFormatVersion\":9,\"generator\":\"ACTools proxy\",\"selfId\":-1,\"players\":[],"
				"\"serverName\":\"%s\","
//				"\"duration\":%u,"
				"\"date\":%lu,"
				"\"mcversion\":\"%s\"}\n";

void replay_init_context(struct context *ctx, const char *restrict version/*, const uint8_t *restrict buffer, uint32_t len*/) {
	replay_free_context(ctx);

	char buf[32];
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	sprintf(buf, "%d_%02d_%02d_%02d_%02d_%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	mkdir(buf, S_IRWXU);

	int dirfd=open(buf, O_DIRECTORY);

	ctx->replay.replayfileFD=openat(dirfd, "recording.tmcpr", O_CREAT|O_CLOEXEC|O_WRONLY, S_IREAD|S_IWRITE);
	ctx->replay.replayinfoFD=openat(dirfd, "metaData.json", O_CREAT|O_CLOEXEC|O_WRONLY, S_IREAD|S_IWRITE);

	if(ctx->replay.replayfileFD == -1 || ctx->replay.replayinfoFD == -1) {
		perror("openat() failed: ");
		if(ctx->replay.replayfileFD != -1)
			close(ctx->replay.replayfileFD);
		else if (ctx->replay.replayinfoFD != -1)
			close(ctx->replay.replayinfoFD);
		ctx->replay.replayfileFD = -1;
		return;
	}

	gettimeofday(&ctx->replay.startTime, NULL);//Can fail on linux with EFAULT
	ctx->replay.startTime.tv_usec/=1000;
	ctx->replay.startTime.tv_usec+=ctx->replay.startTime.tv_sec*1000;

//	uint32_t buf32[2] = {0, htobe32(len)};
//	write(ctx->replay.replayfileFD, buf32, 2*4);
//	write(ctx->replay.replayfileFD, buffer, len);

	dprintf(ctx->replay.replayinfoFD, json_format, "127.0.0.1", ctx->replay.startTime.tv_usec, version);
}
void replay_free_context(struct context *ctx) {
	if(ctx->replay.replayfileFD == -1)
		return;
	close(ctx->replay.replayfileFD);
	ctx->replay.replayfileFD = -1;
	if(ctx->replay.replayinfoFD != -1) {
		close(ctx->replay.replayinfoFD);
		ctx->replay.replayinfoFD = -1;
	}
}

void replay_write_packet(struct context *ctx, const uint8_t *restrict buffer, uint32_t len) {
	if(ctx->replay.replayfileFD == -1)
		return;

	struct timeval now;
	gettimeofday(&now, NULL);

	long diff = now.tv_usec/1000 + now.tv_sec*1000 - ctx->replay.startTime.tv_usec;//msecs in tv_usec

	uint32_t buf[2] = {htobe32(diff), htobe32(len)};

	write(ctx->replay.replayfileFD, buf, 2*4);
	write(ctx->replay.replayfileFD, buffer, len);
}
