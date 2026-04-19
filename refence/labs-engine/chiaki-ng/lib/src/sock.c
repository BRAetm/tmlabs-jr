// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/sock.h>
#include <fcntl.h>

LABS_EXPORT LabsErrorCode labs_socket_set_nonblock(labs_socket_t sock, bool nonblock)
{
#ifdef _WIN32
	u_long nbio = nonblock ? 1 : 0;
	if(ioctlsocket(sock, FIONBIO, &nbio) != NO_ERROR)
		return LABS_ERR_UNKNOWN;
#else
	int flags = fcntl(sock, F_GETFL, 0);
	if(flags == -1)
		return LABS_ERR_UNKNOWN;
	flags = nonblock ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
	if(fcntl(sock, F_SETFL, flags) == -1)
		return LABS_ERR_UNKNOWN;
#endif
	return LABS_ERR_SUCCESS;
}