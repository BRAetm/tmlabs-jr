// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL


#ifndef LABS_DISCOVERYSERVICE_H
#define LABS_DISCOVERYSERVICE_H

#include "discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*LabsDiscoveryServiceCb)(LabsDiscoveryHost *hosts, size_t hosts_count, void *user);

typedef struct labs_discovery_service_options_t
{
	size_t hosts_max;
	uint64_t host_drop_pings;
	uint64_t ping_ms;
	uint64_t ping_initial_ms;
	struct sockaddr_storage *send_addr;
	size_t send_addr_size;
	struct sockaddr_storage *broadcast_addrs;
	size_t broadcast_num;
	char *send_host;
	LabsDiscoveryServiceCb cb;
	void *cb_user;
} LabsDiscoveryServiceOptions;

typedef struct labs_discovery_service_host_discovery_info_t
{
	uint64_t last_ping_index;
} LabsDiscoveryServiceHostDiscoveryInfo;

typedef struct labs_discovery_service_t
{
	LabsLog *log;
	LabsDiscoveryServiceOptions options;
	LabsDiscovery discovery;

	uint64_t ping_index;
	LabsDiscoveryHost *hosts;
	LabsDiscoveryServiceHostDiscoveryInfo *host_discovery_infos;
	size_t hosts_count;
	LabsMutex state_mutex;

	LabsThread thread;
	LabsBoolPredCond stop_cond;
} LabsDiscoveryService;

LABS_EXPORT LabsErrorCode labs_discovery_service_init(LabsDiscoveryService *service, LabsDiscoveryServiceOptions *options, LabsLog *log);
LABS_EXPORT void labs_discovery_service_fini(LabsDiscoveryService *service);

#ifdef __cplusplus
}
#endif

#endif //LABS_DISCOVERYSERVICE_H
