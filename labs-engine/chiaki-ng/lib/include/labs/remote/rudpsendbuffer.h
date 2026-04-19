// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_RUDPSENDBUFFER_H
#define LABS_RUDPSENDBUFFER_H

#include "../common.h"
#include "../log.h"
#include "../thread.h"
#include "../seqnum.h"
#include "../sock.h"
#include "../remote/rudp.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_rudp_send_buffer_packet_t LabsRudpSendBufferPacket;

typedef struct labs_rudp_send_buffer_t
{
	LabsLog *log;
	LabsRudp rudp;

	LabsRudpSendBufferPacket *packets;
	size_t packets_size; // allocated size
	size_t packets_count; // current count

	LabsMutex mutex;
	LabsCond cond;
	bool should_stop;
	LabsThread thread;
} LabsRudpSendBuffer;


/**
 * Init a Send Buffer and start a thread that automatically re-sends RUDP packets.
 *
 * @param sock if NULL, the Send Buffer thread will effectively do nothing (for unit testing)
 * @param size number of packet slots
 */
LABS_EXPORT LabsErrorCode labs_rudp_send_buffer_init(LabsRudpSendBuffer *send_buffer, LabsRudp rudp, LabsLog *log, size_t size);
LABS_EXPORT void labs_rudp_send_buffer_fini(LabsRudpSendBuffer *send_buffer);

/**
 * @param buf ownership of this is taken by the LabsRudpSendBuffer, which will free it automatically later!
 * On error, buf is freed immediately.
 */
LABS_EXPORT LabsErrorCode labs_rudp_send_buffer_push(LabsRudpSendBuffer *send_buffer, LabsSeqNum16 seq_num, uint8_t *buf, size_t buf_size);

/**
 * @param acked_seq_nums optional array of size of at least send_buffer->packets_size where acked seq nums will be stored
 */
LABS_EXPORT LabsErrorCode labs_rudp_send_buffer_ack(LabsRudpSendBuffer *send_buffer, LabsSeqNum16 seq_num, LabsSeqNum16 *acked_seq_nums, size_t *acked_seq_nums_count);

#ifdef __cplusplus
}
#endif

#endif // LABS_RUDPSENDBUFFER_H
