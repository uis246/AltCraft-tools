#include "replaymod.h"
#include "proxy.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/stat.h>

#include <endian.h>

static const char json_format[]="{\"singleplayer\":false,\"fileFormat\":\"MCPR\",\"fileFormatVersion\":9,\"generator\":\"ACTools proxy\",\"selfId\":-1,"
				"\"serverName\":\"%s\","
//				"\"duration\":%u,"
				"\"date\":%lu,"
				"\"mcversion\":\"%s\"}\n";

void replay_init_context(struct context *ctx, const char *restrict version/*, const uint8_t *restrict buffer, uint32_t len*/) {
	replay_free_context(ctx);

	char buf[32];
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	sprintf(buf, "%02d.%02d.%d_%02d:%02d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900, tm->tm_hour, tm->tm_min);
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

//	uint32_t buf32[2] = {0, htobe32(len)};
//	write(ctx->replay.replayfileFD, buf32, 2*4);
//	write(ctx->replay.replayfileFD, buffer, len);

	dprintf(ctx->replay.replayinfoFD, json_format, "127.0.0.1", ctx->replay.startTime.tv_sec, version);
}
void replay_free_context(struct context *ctx) {
	if(ctx->replay.replayfileFD == -1)
		return;
	close(ctx->replay.replayfileFD);
	if(ctx->replay.replayinfoFD != -1) {
		close(ctx->replay.replayinfoFD);
	}
}

void replay_write_packet(struct context *ctx, const uint8_t *restrict buffer, uint32_t len) {
	if(ctx->replay.replayfileFD == -1)
		return;

	struct timeval now;
	gettimeofday(&now, NULL);

	time_t seconddiff = now.tv_sec - ctx->replay.startTime.tv_sec;
	suseconds_t diff = now.tv_usec - ctx->replay.startTime.tv_usec;
	if(diff < 0) {
		seconddiff--;
		diff+=1000;
	}

	uint32_t buf[2] = {htobe32(seconddiff*1000 + diff), htobe32(len)};

	write(ctx->replay.replayfileFD, buf, 2*4);
	write(ctx->replay.replayfileFD, buffer, len);
}
