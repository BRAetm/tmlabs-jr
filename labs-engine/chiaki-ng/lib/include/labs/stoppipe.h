// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_STOPPIPE_H
#define LABS_STOPPIPE_H

#include "sock.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_stop_pipe_t
{
#ifdef _WIN32
	WSAEVENT event;
#elif defined(__SWITCH__)
	// due to a lack pipe/event/socketpair
	// on switch env, we use a physical socket
	// to send/trigger the cancel signal
	struct sockaddr_in addr;
	// local stop socket file descriptor
	// this fd is audited by 'select' as
	// fd_set *readfds
	int fd;
#else
	int fds[2];
#endif
} LabsStopPipe;

struct sockaddr;

LABS_EXPORT LabsErrorCode labs_stop_pipe_init(LabsStopPipe *stop_pipe);
LABS_EXPORT void labs_stop_pipe_fini(LabsStopPipe *stop_pipe);
LABS_EXPORT void labs_stop_pipe_stop(LabsStopPipe *stop_pipe);
LABS_EXPORT LabsErrorCode labs_stop_pipe_select_single(LabsStopPipe *stop_pipe, labs_socket_t fd, bool write, uint64_t timeout_ms);
/**
 * Like connect(), but can be canceled by the stop pipe. Only makes sense with a non-blocking socket.
 */
LABS_EXPORT LabsErrorCode labs_stop_pipe_connect(LabsStopPipe *stop_pipe, labs_socket_t fd, struct sockaddr *addr, size_t addrlen, uint64_t timeout_ms);
static inline LabsErrorCode labs_stop_pipe_sleep(LabsStopPipe *stop_pipe, uint64_t timeout_ms) { return labs_stop_pipe_select_single(stop_pipe, LABS_INVALID_SOCKET, false, timeout_ms); }
LABS_EXPORT LabsErrorCode labs_stop_pipe_reset(LabsStopPipe *stop_pipe);

#ifdef __cplusplus
}
#endif

#endif // LABS_STOPPIPE_H
