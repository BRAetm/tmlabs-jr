// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "utils.h"

#include <labs/discovery.h>
#include <labs/http.h>
#include <labs/log.h>

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

const char *labs_discovery_host_state_string(LabsDiscoveryHostState state)
{
	switch(state)
	{
		case LABS_DISCOVERY_HOST_STATE_READY:
			return "ready";
		case LABS_DISCOVERY_HOST_STATE_STANDBY:
			return "standby";
		default:
			return "unknown";
	}
}

LABS_EXPORT bool labs_discovery_host_is_ps5(LabsDiscoveryHost *host)
{
	return host->device_discovery_protocol_version
		&& !strcmp(host->device_discovery_protocol_version, LABS_DISCOVERY_PROTOCOL_VERSION_PS5);
}

LABS_EXPORT LabsTarget labs_discovery_host_system_version_target(LabsDiscoveryHost *host)
{
	// traslate discovered system_version into LabsTarget

	int version = atoi(host->system_version);
	bool is_ps5 = labs_discovery_host_is_ps5(host);

	if(version >= 8050001 && is_ps5)
		// PS5 >= 1.0
		return LABS_TARGET_PS5_1;
	if(version >= 8050000 && is_ps5)
		// PS5 >= 0
		return LABS_TARGET_PS5_UNKNOWN;

	if(version >= 8000000)
		// PS4 >= 8.0
		return LABS_TARGET_PS4_10;
	if(version >= 7000000)
		// PS4 >= 7.0
		return LABS_TARGET_PS4_9;
	if(version > 0)
		return LABS_TARGET_PS4_8;

	return LABS_TARGET_PS4_UNKNOWN;
}

LABS_EXPORT int labs_discovery_packet_fmt(char *buf, size_t buf_size, LabsDiscoveryPacket *packet)
{
	if(!packet->protocol_version)
		return -1;
	switch(packet->cmd)
	{
		case LABS_DISCOVERY_CMD_SRCH:
			return snprintf(buf, buf_size, "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:%s\n",
							packet->protocol_version);
		case LABS_DISCOVERY_CMD_WAKEUP:
			return snprintf(buf, buf_size,
				"WAKEUP * HTTP/1.1\n"
				"client-type:vr\n"
				"auth-type:R\n"
				"model:w\n"
				"app-type:r\n"
				"user-credential:%llu\n"
				"device-discovery-protocol-version:%s\n",
				(unsigned long long)packet->user_credential, packet->protocol_version);
		default:
			return -1;
	}
}

LABS_EXPORT LabsErrorCode labs_discovery_srch_response_parse(LabsDiscoveryHost *response, struct sockaddr *addr, char *addr_buf, size_t addr_buf_size, char *buf, size_t buf_size)
{
	LabsHttpResponse http_response;
	LabsErrorCode err = labs_http_response_parse(&http_response, buf, buf_size);
	if(err != LABS_ERR_SUCCESS)
		return err;

	memset(response, 0, sizeof(*response));

	response->host_addr = sockaddr_str(addr, addr_buf, addr_buf_size);

	switch(http_response.code)
	{
		case 200:
			response->state = LABS_DISCOVERY_HOST_STATE_READY;
			break;
		case 620:
			response->state = LABS_DISCOVERY_HOST_STATE_STANDBY;
			break;
		default:
			response->state = LABS_DISCOVERY_HOST_STATE_UNKNOWN;
			break;
	}

	for(LabsHttpHeader *header = http_response.headers; header; header=header->next)
	{
		if(strcmp(header->key, "system-version") == 0)
			response->system_version = header->value;
		else if(strcmp(header->key, "device-discovery-protocol-version") == 0)
			response->device_discovery_protocol_version = header->value;
		else if(strcmp(header->key, "host-request-port") == 0)
			response->host_request_port = (uint16_t)strtoul(header->value, NULL, 0);
		else if(strcmp(header->key, "host-name") == 0)
			response->host_name = header->value;
		else if(strcmp(header->key, "host-type") == 0)
			response->host_type = header->value;
		else if(strcmp(header->key, "host-id") == 0)
			response->host_id = header->value;
		else if(strcmp(header->key, "running-app-titleid") == 0)
			response->running_app_titleid = header->value;
		else if(strcmp(header->key, "running-app-name") == 0)
			response->running_app_name = header->value;
		//else
		//	printf("unknown %s: %s\n", header->key, header->value);
	}

	labs_http_response_fini(&http_response);
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_discovery_init(LabsDiscovery *discovery, LabsLog *log, sa_family_t family)
{
	if(family != AF_INET && family != AF_INET6)
		return LABS_ERR_INVALID_DATA;

	discovery->log = log;

	discovery->socket = socket(family, SOCK_DGRAM, IPPROTO_UDP);
	if(LABS_SOCKET_IS_INVALID(discovery->socket))
	{
		LABS_LOGE(discovery->log, "Discovery failed to create socket");
		return LABS_ERR_NETWORK;
	}

	// First try LABS_DISCOVERY_PORT_LOCAL_MIN..<MAX, then 0 (random)
	uint16_t port = LABS_DISCOVERY_PORT_LOCAL_MIN;
	int r;
	while(true)
	{
		memset(&discovery->local_addr, 0, sizeof(discovery->local_addr));
		((struct sockaddr *)&discovery->local_addr)->sa_family = family;
		socklen_t len = 0;
		if(family == AF_INET6)
		{
#ifndef __SWITCH__
			struct in6_addr anyaddr = IN6ADDR_ANY_INIT;
#endif
			struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&discovery->local_addr;
#ifndef __SWITCH__
			addr->sin6_addr = anyaddr;
#else
			addr->sin6_addr = in6addr_any;
#endif
			addr->sin6_port = htons(port);
			len = sizeof(struct sockaddr_in6);
		}
		else // AF_INET
		{
			struct sockaddr_in *addr = (struct sockaddr_in *)&discovery->local_addr;
			addr->sin_addr.s_addr = htonl(INADDR_ANY);
			addr->sin_port = htons(port);
			len = sizeof(struct sockaddr_in);
		}

		r = bind(discovery->socket, (struct sockaddr *)&discovery->local_addr, len);
		if(r >= 0 || !port)
			break;
		if(port == LABS_DISCOVERY_PORT_LOCAL_MAX)
		{
			port = 0;
			LABS_LOGI(discovery->log, "Discovery failed to bind port %u, trying random",
					(unsigned int)port);
		}
		else
		{
			port++;
			LABS_LOGI(discovery->log, "Discovery failed to bind port %u, trying one higher",
					(unsigned int)port);
		}
	}

	if(r < 0)
	{
		LABS_LOGE(discovery->log, "Discovery failed to bind");
		if(!LABS_SOCKET_IS_INVALID(discovery->socket))
		{
			LABS_SOCKET_CLOSE(discovery->socket);
			discovery->socket = LABS_INVALID_SOCKET;
		}
		return LABS_ERR_NETWORK;
	}

	const int broadcast = 1;
	r = setsockopt(discovery->socket, SOL_SOCKET, SO_BROADCAST, (const void *)&broadcast, sizeof(broadcast));
	if(r < 0)
		LABS_LOGE(discovery->log, "Discovery failed to setsockopt SO_BROADCAST");

//#ifdef __FreeBSD__
//	const int onesbcast = 1;
//	r = setsockopt(discovery->socket, IPPROTO_IP, IP_ONESBCAST, &onesbcast, sizeof(onesbcast));
//	if(r < 0)
//		LABS_LOGE(discovery->log, "Discovery failed to setsockopt IP_ONESBCAST");
//#endif

	return LABS_ERR_SUCCESS;
}

LABS_EXPORT void labs_discovery_fini(LabsDiscovery *discovery)
{
	if(!LABS_SOCKET_IS_INVALID(discovery->socket))
	{
		LABS_SOCKET_CLOSE(discovery->socket);
		discovery->socket = LABS_INVALID_SOCKET;
	}
}

LABS_EXPORT LabsErrorCode labs_discovery_send(LabsDiscovery *discovery, LabsDiscoveryPacket *packet, struct sockaddr *addr, size_t addr_size)
{
	if(addr->sa_family != ((struct sockaddr *)&discovery->local_addr)->sa_family)
		return LABS_ERR_INVALID_DATA;

	char buf[512];
	int len = labs_discovery_packet_fmt(buf, sizeof(buf), packet);
	if(len < 0)
		return LABS_ERR_UNKNOWN;
	if((size_t)len >= sizeof(buf))
		return LABS_ERR_BUF_TOO_SMALL;

	// LABS_LOGV(discovery->log, "Discovery sending:");
	// labs_log_hexdump(discovery->log, LABS_LOG_VERBOSE, (const uint8_t *)buf, (size_t)len + 1);
	int rc = sendto_broadcast(discovery->log, discovery->socket, buf, (size_t)len + 1, 0, addr, addr_size);
	if(rc < 0 && addr->sa_family == AF_INET)
	{
		LABS_LOGE(discovery->log, "Discovery failed to send: %s", strerror(errno));
		return LABS_ERR_NETWORK;
	}

	return LABS_ERR_SUCCESS;
}

static void *discovery_thread_func(void *user);
static void *discovery_thread_func_oneshot(void *user);

LABS_EXPORT LabsErrorCode labs_discovery_thread_start(LabsDiscoveryThread *thread, LabsDiscovery *discovery, LabsDiscoveryCb cb, void *cb_user)
{
	thread->discovery = discovery;
	thread->cb = cb;
	thread->cb_user = cb_user;

	LabsErrorCode err = labs_stop_pipe_init(&thread->stop_pipe);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(discovery->log, "Discovery (thread) failed to create pipe");
		return err;
	}

	err = labs_thread_create(&thread->thread, discovery_thread_func, thread);
	if(err != LABS_ERR_SUCCESS)
	{
		labs_stop_pipe_fini(&thread->stop_pipe);
		return err;
	}

	labs_thread_set_name(&thread->thread, "Labs Discovery");

	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_discovery_thread_start_oneshot(LabsDiscoveryThread *thread, LabsDiscovery *discovery, LabsDiscoveryCb cb, void *cb_user)
{
	thread->discovery = discovery;
	thread->cb = cb;
	thread->cb_user = cb_user;

	LabsErrorCode err = labs_stop_pipe_init(&thread->stop_pipe);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(discovery->log, "Discovery (thread) failed to create pipe");
		return err;
	}

	err = labs_thread_create(&thread->thread, discovery_thread_func_oneshot, thread);
	if(err != LABS_ERR_SUCCESS)
	{
		labs_stop_pipe_fini(&thread->stop_pipe);
		return err;
	}

	labs_thread_set_name(&thread->thread, "Labs Discovery");

	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_discovery_thread_stop(LabsDiscoveryThread *thread)
{
	labs_stop_pipe_stop(&thread->stop_pipe);
	LabsErrorCode err = labs_thread_join(&thread->thread, NULL);
	if(err != LABS_ERR_SUCCESS)
		return err;

	labs_stop_pipe_fini(&thread->stop_pipe);
	return LABS_ERR_SUCCESS;
}

static void *discovery_thread_func(void *user)
{
	LabsDiscoveryThread *thread = user;
	labs_thread_set_affinity(LABS_THREAD_NAME_DISCOVERY);
	LabsDiscovery *discovery = thread->discovery;

	while(1)
	{
		LabsErrorCode err = labs_stop_pipe_select_single(&thread->stop_pipe, discovery->socket, false, UINT64_MAX);
		if(err == LABS_ERR_CANCELED)
			break;
		if(err != LABS_ERR_SUCCESS)
		{
			LABS_LOGE(discovery->log, "Discovery thread failed to select");
			break;
		}

		char buf[512];
		struct sockaddr client_addr;
		socklen_t client_addr_size = sizeof(client_addr);
		LABS_SSIZET_TYPE n = recvfrom(discovery->socket, buf, sizeof(buf) - 1, 0, &client_addr, &client_addr_size);
		if(n < 0)
		{
			LABS_LOGE(discovery->log, "Discovery thread failed to read from socket");
			break;
		}

		if(n == 0)
			continue;

		if(n > sizeof(buf) - 1)
			n = sizeof(buf) - 1;

		buf[n] = '\00';

		//LABS_LOGV(discovery->log, "Discovery received:\n%s", buf);
		//labs_log_hexdump_raw(discovery->log, LABS_LOG_VERBOSE, (const uint8_t *)buf, n);

		char addr_buf[64];
		LabsDiscoveryHost response;
		err = labs_discovery_srch_response_parse(&response, &client_addr, addr_buf, sizeof(addr_buf), buf, n);
		if(err != LABS_ERR_SUCCESS)
		{
			LABS_LOGI(discovery->log, "Discovery Response invalid");
			continue;
		}

		if(thread->cb)
			thread->cb(&response, thread->cb_user);
	}

	return NULL;
}

static void *discovery_thread_func_oneshot(void *user)
{
	LabsDiscoveryThread *thread = user;
	labs_thread_set_affinity(LABS_THREAD_NAME_DISCOVERY);
	LabsDiscovery *discovery = thread->discovery;

	while(1)
	{
		LabsErrorCode err = labs_stop_pipe_select_single(&thread->stop_pipe, discovery->socket, false, UINT64_MAX);
		if(err == LABS_ERR_CANCELED)
			break;
		if(err != LABS_ERR_SUCCESS)
		{
			LABS_LOGE(discovery->log, "Discovery thread failed to select");
			break;
		}

		char buf[512];
		struct sockaddr client_addr;
		socklen_t client_addr_size = sizeof(client_addr);
		LABS_SSIZET_TYPE n = recvfrom(discovery->socket, buf, sizeof(buf) - 1, 0, &client_addr, &client_addr_size);
		if(n < 0)
		{
			LABS_LOGE(discovery->log, "Discovery thread failed to read from socket");
			break;
		}

		if(n == 0)
			continue;

		if(n > sizeof(buf) - 1)
			n = sizeof(buf) - 1;

		buf[n] = '\00';

		//LABS_LOGV(discovery->log, "Discovery received:\n%s", buf);
		//labs_log_hexdump_raw(discovery->log, LABS_LOG_VERBOSE, (const uint8_t *)buf, n);

		char addr_buf[64];
		LabsDiscoveryHost response;
		err = labs_discovery_srch_response_parse(&response, &client_addr, addr_buf, sizeof(addr_buf), buf, n);
		if(err != LABS_ERR_SUCCESS)
		{
			LABS_LOGI(discovery->log, "Discovery Response invalid");
			continue;
		}

		if(thread->cb)
		{
			thread->cb(&response, thread->cb_user);
			break;
		}
	}

	return NULL;
}

LABS_EXPORT LabsErrorCode labs_discovery_wakeup(LabsLog *log, LabsDiscovery *discovery, const char *host, uint64_t user_credential, bool ps5)
{
	struct addrinfo *addrinfos;
	// make hostname use ipv4 for now
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;
	char *ipv6 = strchr(host, ':');
	if(ipv6)
		hints.ai_family = AF_INET6;
	else
		hints.ai_family = AF_INET;
	int r = getaddrinfo(host, NULL, &hints, &addrinfos); // TODO: this blocks, use something else
	if(r != 0)
	{
		LABS_LOGE(log, "DiscoveryManager failed to getaddrinfo for wakeup");
		return LABS_ERR_NETWORK;
	}
	struct sockaddr_in6 addr = { 0 };
	socklen_t addr_len = 0;
	for(struct addrinfo *ai=addrinfos; ai; ai=ai->ai_next)
	{
		if(ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
			continue;
		if(ai->ai_addrlen > sizeof(addr))
			continue;
		memcpy(&addr, ai->ai_addr, ai->ai_addrlen);
		addr_len = ai->ai_addrlen;
		break;
	}
	freeaddrinfo(addrinfos);

	if(!addr_len)
	{
		LABS_LOGE(log, "DiscoveryManager failed to get suitable address from getaddrinfo for wakeup");
		return LABS_ERR_UNKNOWN;
	}
	if(((struct sockaddr *)&addr)->sa_family == AF_INET)
		((struct sockaddr_in *)&addr)->sin_port = htons(ps5 ? LABS_DISCOVERY_PORT_PS5 : LABS_DISCOVERY_PORT_PS4);
	else
		addr.sin6_port = htons(ps5 ? LABS_DISCOVERY_PORT_PS5 : LABS_DISCOVERY_PORT_PS4);

	LabsDiscoveryPacket packet = { 0 };
	packet.cmd = LABS_DISCOVERY_CMD_WAKEUP;
	packet.protocol_version = ps5 ? LABS_DISCOVERY_PROTOCOL_VERSION_PS5 : LABS_DISCOVERY_PROTOCOL_VERSION_PS4;
	packet.user_credential = user_credential;

	LabsErrorCode err;
	if(discovery)
		err = labs_discovery_send(discovery, &packet, (struct sockaddr *)&addr, addr_len);
	else
	{
		LabsDiscovery tmp_discovery;
		err = labs_discovery_init(&tmp_discovery, log, ((struct sockaddr *)&addr)->sa_family);
		if(err != LABS_ERR_SUCCESS)
		{
			LABS_LOGE(log, "Failed to init temporary discovery for wakeup: %s", labs_error_string(err));
			return err;
		}
		err = labs_discovery_send(&tmp_discovery, &packet, (struct sockaddr *)&addr, addr_len);
		labs_discovery_fini(&tmp_discovery);
	}

	return err;
}
