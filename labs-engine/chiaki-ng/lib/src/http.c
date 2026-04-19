// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/http.h>

#include <stdbool.h>
#include <string.h>
#include <labs/remote/rudp.h>
#include <labs/log.h>
#include <labs/time.h>

#if _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

LABS_EXPORT void labs_http_header_free(LabsHttpHeader *header)
{
	while(header)
	{
		LabsHttpHeader *cur = header;
		header = header->next;
		free(cur);
	}
}

LABS_EXPORT LabsErrorCode labs_http_header_parse(LabsHttpHeader **header, char *buf, size_t buf_size)
{
	*header = NULL;
#define FAIL(reason) do { labs_http_header_free(*header); return (reason); } while(0);
	char *key_ptr = buf;
	char *value_ptr = NULL;

	for(char *end = buf + buf_size; buf<end; buf++)
	{
		char c = *buf;
		if(!c)
			break;

		if(!value_ptr)
		{
			if(c == ':')
			{
				if(key_ptr == buf)
					FAIL(LABS_ERR_INVALID_DATA);
				*buf = '\0';
				buf++;
				if(buf == end)
					FAIL(LABS_ERR_INVALID_DATA);
				if(*buf == ' ')
					buf++;
				if(buf == end)
					FAIL(LABS_ERR_INVALID_DATA);
				value_ptr = buf;
			}
			else if(c == '\r' || c == '\n')
			{
				if(key_ptr + 1 < buf) // no : encountered yet
					FAIL(LABS_ERR_INVALID_DATA);
				key_ptr = buf + 1;
			}
		}
		else
		{
			if(c == '\r' || c == '\n')
			{
				if(value_ptr == buf) // empty value
					FAIL(LABS_ERR_INVALID_DATA);

				*buf = '\0';
				LabsHttpHeader *entry = malloc(sizeof(LabsHttpHeader));
				if(!entry)
					FAIL(LABS_ERR_MEMORY);
				entry->key = key_ptr;
				entry->value = value_ptr;
				entry->next = *header;
				*header = entry;

				key_ptr = buf + 1;
				value_ptr = NULL;
			}
		}
	}
	return LABS_ERR_SUCCESS;
#undef FAIL
}

LABS_EXPORT void labs_http_response_fini(LabsHttpResponse *response)
{
	if(!response)
		return;
	labs_http_header_free(response->headers);
}

LABS_EXPORT LabsErrorCode labs_http_response_parse(LabsHttpResponse *response, char *buf, size_t buf_size)
{
	static const char *http_version = "HTTP/1.1 ";
	static const size_t http_version_size = 9;

	if(buf_size < http_version_size)
		return LABS_ERR_INVALID_DATA;

	if(strncmp(buf, http_version, http_version_size) != 0)
		return LABS_ERR_INVALID_DATA;

	buf += http_version_size;
	buf_size -= http_version_size;

	char *line_end = memchr(buf, '\n', buf_size);
	if(!line_end)
		return LABS_ERR_INVALID_DATA;
	size_t line_length = (line_end - buf) + 1;
	if(buf_size <= line_length)
		return LABS_ERR_INVALID_DATA;
	if(line_length > 1 && *(line_end - 1) == '\r')
		*(line_end - 1) = '\0';
	else
		*line_end = '\0';

	char *endptr;
	response->code = (int)strtol(buf, &endptr, 10);
	if(response->code == 0)
		return LABS_ERR_INVALID_DATA;

	buf += line_length;
	buf_size -= line_length;

	return labs_http_header_parse(&response->headers, buf, buf_size);
}

LABS_EXPORT LabsErrorCode labs_recv_http_header(int sock, char *buf, size_t buf_size, size_t *header_size, size_t *received_size, LabsStopPipe *stop_pipe, uint64_t timeout_ms)
{
	// 0 = ""
	// 1 = "\r"
	// 2 = "\r\n"
	// 3 = "\r\n\r"
	// 4 = "\r\n\r\n" (final)
	int nl_state = 0;
	static const int transitions_r[] = { 1, 1, 3, 1 };
	static const int transitions_n[] = { 0, 2, 0, 4 };
	uint64_t deadline_ms = timeout_ms == UINT64_MAX ? UINT64_MAX : labs_time_now_monotonic_ms() + timeout_ms;

	*received_size = 0;
	while(true)
	{
		if(stop_pipe)
		{
			uint64_t wait_timeout_ms = UINT64_MAX;
			if(deadline_ms != UINT64_MAX)
			{
				uint64_t now_ms = labs_time_now_monotonic_ms();
				if(now_ms >= deadline_ms)
					return LABS_ERR_TIMEOUT;
				wait_timeout_ms = deadline_ms - now_ms;
			}
			LabsErrorCode err = labs_stop_pipe_select_single(stop_pipe, sock, false, wait_timeout_ms);
			if(err != LABS_ERR_SUCCESS)
				return err;
		}

		LABS_SSIZET_TYPE received;
		do
		{
			received = recv(sock, buf, (int)buf_size, 0);
#if _WIN32
		} while(false);
#else
		} while(received < 0 && errno == EINTR);
#endif
		if(received < 0)
		{
#ifdef _WIN32
			int err = WSAGetLastError();
			if(err == WSAEWOULDBLOCK)
				continue;
#else
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
#endif
		}
		if(received <= 0)
			return received == 0 ? LABS_ERR_DISCONNECTED : LABS_ERR_NETWORK;

		*received_size += received;
		for(; received > 0; buf++, received--)
		{
			switch(*buf)
			{
				case '\r':
					nl_state = transitions_r[nl_state];
					break;
				case '\n':
					nl_state = transitions_n[nl_state];
					break;
				default:
					nl_state = 0;
					break;
			}
			if(nl_state == 4)
			{
				received--;
				break;
			}
		}

		if(nl_state == 4)
		{
			*header_size = *received_size - received;
			break;
		}
	}

	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_send_recv_http_header_psn(LabsRudp rudp, LabsLog *log,
	uint16_t *remote_counter, char *send_buf, size_t send_buf_size,
	char *buf, size_t buf_size, size_t *header_size, size_t *received_size)
{
	// 0 = ""
	// 1 = "\r"
	// 2 = "\r\n"
	// 3 = "\r\n\r"
	// 4 = "\r\n\r\n" (final)
	int nl_state = 0;
	static const int transitions_r[] = { 1, 1, 3, 1 };
	static const int transitions_n[] = { 0, 2, 0, 4 };

	*received_size = 0;
	int received;
	RudpMessage message;
	LabsErrorCode err;
	err = labs_rudp_send_recv(rudp, &message, (uint8_t *)send_buf, send_buf_size, *remote_counter, SESSION_MESSAGE, CTRL_MESSAGE, 2, 3);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(log, "Didn't receive http session message response");
		return err;
	}
	received = message.data_size - 2;
	memcpy(buf, message.data + 2, received);
	*remote_counter = message.remote_counter;
	labs_rudp_message_pointers_free(&message);

	if(received <= 0)
		return received == 0 ? LABS_ERR_DISCONNECTED : LABS_ERR_NETWORK;

	*received_size += received;
	for(; received > 0; buf++, received--)
	{
		switch(*buf)
		{
			case '\r':
				nl_state = transitions_r[nl_state];
				break;
			case '\n':
				nl_state = transitions_n[nl_state];
				break;
			default:
				nl_state = 0;
				break;
		}
		if(nl_state == 4)
		{
			received--;
			break;
		}
	}

	if(nl_state == 4)
	{
		*header_size = *received_size - received;
	}

	return LABS_ERR_SUCCESS;
}
