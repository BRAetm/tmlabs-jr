// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_SOCK_H
#define LABS_SOCK_H

#include "common.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET labs_socket_t;
#define LABS_SOCKET_IS_INVALID(s) ((s) == INVALID_SOCKET)
#define LABS_INVALID_SOCKET INVALID_SOCKET
#define LABS_SOCKET_CLOSE(s) closesocket(s)
#define LABS_SOCKET_ERROR_FMT "%d"
#define LABS_SOCKET_ERROR_VALUE (WSAGetLastError())
#define LABS_SOCKET_EINPROGRESS (WSAGetLastError() == WSAEWOULDBLOCK)
#define LABS_SOCKET_BUF_TYPE char*
#else
#include <unistd.h>
#include <errno.h>
typedef int labs_socket_t;
#define LABS_SOCKET_IS_INVALID(s) ((s) < 0)
#define LABS_INVALID_SOCKET (-1)
#define LABS_SOCKET_CLOSE(s) close(s)
#define LABS_SOCKET_ERROR_FMT "%s"
#define LABS_SOCKET_ERROR_VALUE (strerror(errno))
#define LABS_SOCKET_EINPROGRESS (errno == EINPROGRESS)
#define LABS_SOCKET_BUF_TYPE void *
#endif

LABS_EXPORT LabsErrorCode labs_socket_set_nonblock(labs_socket_t sock, bool nonblock);

#ifdef __cplusplus
}
#endif

#endif //LABS_SOCK_H
