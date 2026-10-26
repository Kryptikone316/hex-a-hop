/* Bring-up only. Writes
 * directly to a fixed on-card path instead of relying on the shell's own
 * `> log.txt` redirection + stdio buffering - confirmed necessary: even
 * setvbuf(_IOLBF) + fflush()/sync() on the shell-redirected fd produced a
 * 0-byte log.txt after every hard power-cycle, despite the process
 * provably running well past every checkpoint (the menu rendered, which
 * needs hwdisp/joykey/TextInit/InitSound/InitScreen all already done).
 * Real suspected cause: FAT-family filesystems commonly only commit a
 * file's updated SIZE into its directory entry on close(), not on fsync()
 * alone - an fd held open for the whole process life can leave data
 * blocks written but the directory entry still showing stale/zero size
 * after a crash or power loss. Opening, writing, fsyncing, AND closing on
 * every single call avoids that gap entirely, at the cost of real
 * per-call overhead - acceptable for bring-up instrumentation, not meant
 * to ship in the final build.
 *
 * Compiled out unless built with -DHEXAHOP_DEBUG_LOG, so a normal build
 * never creates or writes debug.log. */
#ifndef HEXAHOP_DEBUGLOG_H
#define HEXAHOP_DEBUGLOG_H

#ifndef HEXAHOP_DEBUG_LOG
static inline void DebugLogInit() {}
static inline void DebugLog(const char*, ...) {}
#else

#include <cstdio>
#include <cstdarg>
#include <fcntl.h>
#include <unistd.h>

/* Relative to the working directory: run.sh cd's into the port folder before
 * launching, so the log follows the port wherever it is installed. */
#define HEXAHOP_DEBUG_LOG_PATH "debug.log"

/* Call once, before any DebugLog() call, to start a fresh log for this run. */
static inline void DebugLogInit()
{
	int fd = open(HEXAHOP_DEBUG_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0)
	{
		fsync(fd);
		close(fd);
	}
}

static inline void DebugLog(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	int len = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);
	if (len <= 0)
		return;
	if (len >= (int)sizeof(buf) - 1)
		len = (int)sizeof(buf) - 2;
	buf[len++] = '\n';

	int fd = open(HEXAHOP_DEBUG_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd < 0)
		return;
	write(fd, buf, (size_t)len);
	fsync(fd);
	close(fd);
}

#endif /* HEXAHOP_DEBUG_LOG */

#endif
