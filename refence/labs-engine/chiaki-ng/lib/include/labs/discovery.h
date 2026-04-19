// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_DISCOVERY_H
#define LABS_DISCOVERY_H

#include "common.h"
#include "thread.h"
#include "stoppipe.h"
#include "log.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef unsigned short sa_family_t;
#else
#include <sys/types.h>
#include <sys/socket.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LABS_DISCOVERY_PORT_PS4 987
#define LABS_DISCOVERY_PROTOCOL_VERSION_PS4 "00020020"
#define LABS_DISCOVERY_PORT_PS5 9302
#define LABS_DISCOVERY_PROTOCOL_VERSION_PS5 "00030010"
#define LABS_DISCOVERY_PORT_LOCAL_MIN 9303
#define LABS_DISCOVERY_PORT_LOCAL_MAX 9319

typedef enum labs_discovery_cmd_t
{
	LABS_DISCOVERY_CMD_SRCH,
	LABS_DISCOVERY_CMD_WAKEUP
} LabsDiscoveryCmd;

typedef struct labs_discovery_packet_t
{
	LabsDiscoveryCmd cmd;
	char *protocol_version;
	uint64_t user_credential; // for wakeup, this is just the regist key interpreted as hex
} LabsDiscoveryPacket;

typedef enum labs_discovery_host_state_t
{
	LABS_DISCOVERY_HOST_STATE_UNKNOWN,
	LABS_DISCOVERY_HOST_STATE_READY,
	LABS_DISCOVERY_HOST_STATE_STANDBY
} LabsDiscoveryHostState;

const char *labs_discovery_host_state_string(LabsDiscoveryHostState state);

/**
 * Apply A on all names of string members in LabsDiscoveryHost
 */
#define LABS_DISCOVERY_HOST_STRING_FOREACH(A) \
	A(host_addr); \
	A(system_version); \
	A(device_discovery_protocol_version); \
	A(host_name); \
	A(host_type); \
	A(host_id); \
	A(running_app_titleid); \
	A(running_app_name);

typedef struct labs_discovery_host_t
{
	// All string members here must be in sync with LABS_DISCOVERY_HOST_STRING_FOREACH
	LabsDiscoveryHostState state;
	uint16_t host_request_port;
#define STRING_MEMBER(name) const char *name
	LABS_DISCOVERY_HOST_STRING_FOREACH(STRING_MEMBER)
#undef STRING_MEMBER
} LabsDiscoveryHost;

LABS_EXPORT bool labs_discovery_host_is_ps5(LabsDiscoveryHost *host);

LABS_EXPORT LabsTarget labs_discovery_host_system_version_target(LabsDiscoveryHost *host);

LABS_EXPORT int labs_discovery_packet_fmt(char *buf, size_t buf_size, LabsDiscoveryPacket *packet);

typedef struct labs_discovery_t
{
	LabsLog *log;
	labs_socket_t socket;
	struct sockaddr_storage local_addr;
} LabsDiscovery;

LABS_EXPORT LabsErrorCode labs_discovery_init(LabsDiscovery *discovery, LabsLog *log, sa_family_t family);
LABS_EXPORT void labs_discovery_fini(LabsDiscovery *discovery);
LABS_EXPORT LabsErrorCode labs_discovery_send(LabsDiscovery *discovery, LabsDiscoveryPacket *packet, struct sockaddr *addr, size_t addr_size);

typedef void (*LabsDiscoveryCb)(LabsDiscoveryHost *host, void *user);

typedef struct labs_discovery_thread_t
{
	LabsDiscovery *discovery;
	LabsThread thread;
	LabsStopPipe stop_pipe;
	LabsDiscoveryCb cb;
	void *cb_user;
} LabsDiscoveryThread;

LABS_EXPORT LabsErrorCode labs_discovery_thread_start(LabsDiscoveryThread *thread, LabsDiscovery *discovery, LabsDiscoveryCb cb, void *cb_user);
LABS_EXPORT LabsErrorCode labs_discovery_thread_start_oneshot(LabsDiscoveryThread *thread, LabsDiscovery *discovery, LabsDiscoveryCb cb, void *cb_user);
LABS_EXPORT LabsErrorCode labs_discovery_thread_stop(LabsDiscoveryThread *thread);

/**
 * Convenience function to send a wakeup packet
 * @param discovery Discovery to send the packet on. May be NULL, in which case a new temporary Discovery will be created
 */
LABS_EXPORT LabsErrorCode labs_discovery_wakeup(LabsLog *log, LabsDiscovery *discovery, const char *host, uint64_t user_credential, bool ps5);

#ifdef __cplusplus
}
#endif

#endif // LABS_VIDEORECEIVER_H
