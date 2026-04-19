// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_HTTP_H
#define LABS_HTTP_H

#include "common.h"
#include "stoppipe.h"
#include "remote/rudp.h"

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_http_header_t
{
	const char *key;
	const char *value;
	struct labs_http_header_t *next;
} LabsHttpHeader;

typedef struct labs_http_response_t
{
	int code;
	LabsHttpHeader *headers;
} LabsHttpResponse;

LABS_EXPORT void labs_http_header_free(LabsHttpHeader *header);
LABS_EXPORT LabsErrorCode labs_http_header_parse(LabsHttpHeader **header, char *buf, size_t buf_size);

LABS_EXPORT void labs_http_response_fini(LabsHttpResponse *response);
LABS_EXPORT LabsErrorCode labs_http_response_parse(LabsHttpResponse *response, char *buf, size_t buf_size);

/**
 * @param stop_pipe optional
 * @param timeout_ms only used if stop_pipe is not NULL
 */
LABS_EXPORT LabsErrorCode labs_recv_http_header(int sock, char *buf, size_t buf_size, size_t *header_size, size_t *received_size, LabsStopPipe *stop_pipe, uint64_t timeout_ms);

LABS_EXPORT LabsErrorCode labs_send_recv_http_header_psn(LabsRudp rudp, LabsLog *log,
	uint16_t *remote_counter, char *send_buf, size_t send_buf_size, char *buf, size_t buf_size,
	size_t *header_size, size_t *received_size);

#ifdef __cplusplus
}
#endif

#endif // LABS_HTTP_H
