// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_REORDERQUEUE_H
#define LABS_REORDERQUEUE_H

#include <stdlib.h>

#include "seqnum.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum labs_reorder_queue_drop_strategy_t {
	LABS_REORDER_QUEUE_DROP_STRATEGY_BEGIN, // drop packet with lowest number
	LABS_REORDER_QUEUE_DROP_STRATEGY_END // drop packet with highest number
} LabsReorderQueueDropStrategy;

typedef struct labs_reorder_queue_entry_t
{
	void *user;
	bool set;
} LabsReorderQueueEntry;

typedef void (*LabsReorderQueueDropCb)(uint64_t seq_num, void *elem_user, void *cb_user);
typedef bool (*LabsReorderQueueSeqNumGt)(uint64_t a, uint64_t b);
typedef bool (*LabsReorderQueueSeqNumLt)(uint64_t a, uint64_t b);
typedef uint64_t (*LabsReorderQueueSeqNumAdd)(uint64_t a, uint64_t b);
typedef uint64_t (*LabsReorderQueueSeqNumSub)(uint64_t a, uint64_t b);

typedef struct labs_reorder_queue_t
{
	size_t size_exp; // real size = 2^size * sizeof(LabsReorderQueueEntry)
	LabsReorderQueueEntry *queue;
	uint64_t begin;
	uint64_t count;
	LabsReorderQueueSeqNumGt seq_num_gt;
	LabsReorderQueueSeqNumLt seq_num_lt;
	LabsReorderQueueSeqNumAdd seq_num_add;
	LabsReorderQueueSeqNumSub seq_num_sub;
	LabsReorderQueueDropStrategy drop_strategy;
	LabsReorderQueueDropCb drop_cb;
	void *drop_cb_user;
} LabsReorderQueue;

/**
 * @param size exponent for 2
 * @param seq_num_start sequence number of the first expected element
 */
LABS_EXPORT LabsErrorCode labs_reorder_queue_init(LabsReorderQueue *queue, size_t size_exp,
		uint64_t seq_num_start, LabsReorderQueueSeqNumGt seq_num_gt, LabsReorderQueueSeqNumLt seq_num_lt, LabsReorderQueueSeqNumAdd seq_num_add);

/**
 * Helper to initialize a queue using LabsSeqNum16 sequence numbers
 */
LABS_EXPORT LabsErrorCode labs_reorder_queue_init_16(LabsReorderQueue *queue, size_t size_exp, LabsSeqNum16 seq_num_start);

/**
 * Helper to initialize a queue using LabsSeqNum32 sequence numbers
 */
LABS_EXPORT LabsErrorCode labs_reorder_queue_init_32(LabsReorderQueue *queue, size_t size_exp, LabsSeqNum32 seq_num_start);

LABS_EXPORT void labs_reorder_queue_fini(LabsReorderQueue *queue);

static inline void labs_reorder_queue_set_drop_strategy(LabsReorderQueue *queue, LabsReorderQueueDropStrategy drop_strategy)
{
	queue->drop_strategy = drop_strategy;
}

static inline void labs_reorder_queue_set_drop_cb(LabsReorderQueue *queue, LabsReorderQueueDropCb cb, void *user)
{
	queue->drop_cb = cb;
	queue->drop_cb_user = user;
}

static inline size_t labs_reorder_queue_size(LabsReorderQueue *queue)
{
	return ((size_t)1) << queue->size_exp;
}

static inline uint64_t labs_reorder_queue_count(LabsReorderQueue *queue)
{
	return queue->count;
}

/**
 * Push a packet into the queue.
 *
 * Depending on the set drop strategy, this might drop elements and call the drop callback with the dropped elements.
 * The callback will also be called with the new element íf there is already an element with the same sequence number
 * or if the sequence number is less than queue->begin, i.e. the next element to be pulled.
 *
 * @param seq_num
 * @param user pointer to be associated with the element
 */
LABS_EXPORT void labs_reorder_queue_push(LabsReorderQueue *queue, uint64_t seq_num, void *user);

/**
 * Pull the next element in order from the queue.
 *
 * Call this repeatedly until it returns false to get all subsequently available elements.
 *
 * @param seq_num pointer where the sequence number of the pulled packet is written, undefined contents if false is returned
 * @param user pointer where the user pointer of the pulled packet is written, undefined contents if false is returned
 * @return true if an element was pulled in order
 */
LABS_EXPORT bool labs_reorder_queue_pull(LabsReorderQueue *queue, uint64_t *seq_num, void **user);

/**
 * Peek the element at a specific index inside the queue.
 *
 * @param index Offset to be added to the begin sequence number, this is NOT a sequence number itself! (0 <= index < count)
 * @param seq_num pointer where the sequence number of the peeked packet is written, undefined contents if false is returned
 * @param user pointer where the user pointer of the pulled packet is written, undefined contents if false is returned
 * @return true if an element was peeked, false if there is no element at index.
 */
LABS_EXPORT bool labs_reorder_queue_peek(LabsReorderQueue *queue, uint64_t index, uint64_t *seq_num, void **user);

/**
 * Drop a specific element from the queue.
 * begin will not be changed.
 * @param index Offset to be added to the begin sequence number, this is NOT a sequence number itself! (0 <= index < count)
 */
LABS_EXPORT void labs_reorder_queue_drop(LabsReorderQueue *queue, uint64_t index);

#ifdef __cplusplus
}
#endif

#endif // LABS_REORDERQUEUE_H
