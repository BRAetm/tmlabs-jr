// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_TAKIONSENDBUFFER_H
#define LABS_TAKIONSENDBUFFER_H

#include "common.h"
#include "log.h"
#include "thread.h"
#include "seqnum.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_takion_t LabsTakion;

typedef struct labs_takion_send_buffer_packet_t LabsTakionSendBufferPacket;

typedef struct labs_takion_send_buffer_t
{
	LabsLog *log;
	LabsTakion *takion;

	LabsTakionSendBufferPacket *packets;
	size_t packets_size; // allocated size
	size_t packets_count; // current count

	LabsMutex mutex;
	LabsCond cond;
	bool should_stop;
	LabsThread thread;
} LabsTakionSendBuffer;


/**
 * Init a Send Buffer and start a thread that automatically re-sends packets on takion.
 *
 * @param takion if NULL, the Send Buffer thread will effectively do nothing (for unit testing)
 * @param size number of packet slots
 */
LABS_EXPORT LabsErrorCode labs_takion_send_buffer_init(LabsTakionSendBuffer *send_buffer, LabsTakion *takion, size_t size);
LABS_EXPORT void labs_takion_send_buffer_fini(LabsTakionSendBuffer *send_buffer);

/**
 * @param buf ownership of this is taken by the LabsTakionSendBuffer, which will free it automatically later!
 * On error, buf is freed immediately.
 */
LABS_EXPORT LabsErrorCode labs_takion_send_buffer_push(LabsTakionSendBuffer *send_buffer, LabsSeqNum32 seq_num, uint8_t *buf, size_t buf_size);

/**
 * @param acked_seq_nums optional array of size of at least send_buffer->packets_size where acked seq nums will be stored
 */
LABS_EXPORT LabsErrorCode labs_takion_send_buffer_ack(LabsTakionSendBuffer *send_buffer, LabsSeqNum32 seq_num, LabsSeqNum32 *acked_seq_nums, size_t *acked_seq_nums_count);

#ifdef __cplusplus
}
#endif

#endif // LABS_TAKIONSENDBUFFER_H
