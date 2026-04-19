// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/reorderqueue.h>

#include <assert.h>

#define gt(a, b) (queue->seq_num_gt((a), (b)))
#define lt(a, b) (queue->seq_num_lt((a), (b)))
#define ge(a, b) ((a) == (b) || gt((a), (b)))
#define le(a, b) ((a) == (b) || lt((a), (b)))
#define add(a, b) (queue->seq_num_add((a), (b)))
#define QUEUE_SIZE (1 << queue->size_exp)
#define IDX_MASK ((1 << queue->size_exp) - 1)
#define idx(seq_num) ((seq_num) & IDX_MASK)

LABS_EXPORT LabsErrorCode labs_reorder_queue_init(LabsReorderQueue *queue, size_t size_exp,
		uint64_t seq_num_start, LabsReorderQueueSeqNumGt seq_num_gt, LabsReorderQueueSeqNumLt seq_num_lt, LabsReorderQueueSeqNumAdd seq_num_add)
{
	queue->size_exp = size_exp;
	queue->begin = seq_num_start;
	queue->count = 0;
	queue->seq_num_gt = seq_num_gt;
	queue->seq_num_lt = seq_num_lt;
	queue->seq_num_add = seq_num_add;
	queue->drop_strategy = LABS_REORDER_QUEUE_DROP_STRATEGY_END;
	queue->drop_cb = NULL;
	queue->drop_cb_user = NULL;
	queue->queue = calloc(1 << size_exp, sizeof(LabsReorderQueueEntry));
	if(!queue->queue)
		return LABS_ERR_MEMORY;
	return LABS_ERR_SUCCESS;
}

#define REORDER_QUEUE_INIT(bits) \
static bool seq_num_##bits##_gt(uint64_t a, uint64_t b) { return labs_seq_num_##bits##_gt((LabsSeqNum##bits)a, (LabsSeqNum##bits)b); } \
static bool seq_num_##bits##_lt(uint64_t a, uint64_t b) { return labs_seq_num_##bits##_lt((LabsSeqNum##bits)a, (LabsSeqNum##bits)b); } \
static uint64_t seq_num_##bits##_add(uint64_t a, uint64_t b) { return (uint64_t)((LabsSeqNum##bits)a + (LabsSeqNum##bits)b); } \
\
LABS_EXPORT LabsErrorCode labs_reorder_queue_init_##bits(LabsReorderQueue *queue, size_t size_exp, LabsSeqNum##bits seq_num_start) \
{ \
	return labs_reorder_queue_init(queue, size_exp, (uint64_t)seq_num_start, \
			seq_num_##bits##_gt, seq_num_##bits##_lt, seq_num_##bits##_add); \
}

REORDER_QUEUE_INIT(16)
REORDER_QUEUE_INIT(32)

LABS_EXPORT void labs_reorder_queue_fini(LabsReorderQueue *queue)
{
	if(queue->drop_cb)
	{
		for(uint64_t i=0; i<queue->count; i++)
		{
			uint64_t seq_num = add(queue->begin, i);
			LabsReorderQueueEntry *entry = &queue->queue[idx(seq_num)];
			if(entry->set)
				queue->drop_cb(seq_num, entry->user, queue->drop_cb_user);
		}
	}
	free(queue->queue);
}

LABS_EXPORT void labs_reorder_queue_push(LabsReorderQueue *queue, uint64_t seq_num, void *user)
{
	assert(queue->count <= QUEUE_SIZE);
	uint64_t end = add(queue->begin, queue->count);

	if(ge(seq_num, queue->begin) && lt(seq_num, end))
	{
		LabsReorderQueueEntry *entry = &queue->queue[idx(seq_num)];
		if(entry->set) // received twice
			goto drop_it;
		entry->user = user;
		entry->set = true;
		return;
	}

	if(lt(seq_num, queue->begin))
		goto drop_it;

	// => ge(seq_num, queue->end) == 1
	if(!ge(seq_num, end))
	{
		// Sequence comparisons are undefined at half the serial-number space.
		// If the queue is empty and callers opted into dropping from the begin,
		// rebase to the new packet; otherwise drop it rather than aborting.
		if(queue->count == 0 && queue->drop_strategy == LABS_REORDER_QUEUE_DROP_STRATEGY_BEGIN)
		{
			queue->begin = seq_num;
			end = seq_num;
		}
		else
			goto drop_it;
	}

	uint64_t free_elems = QUEUE_SIZE - queue->count;
	uint64_t total_end = add(end, free_elems);
	uint64_t new_end = add(seq_num, 1);
	if(lt(total_end, new_end))
	{
		if(queue->drop_strategy == LABS_REORDER_QUEUE_DROP_STRATEGY_END)
			goto drop_it;

		// drop first until empty or enough space
		while(queue->count > 0 && lt(total_end, new_end))
		{
			LabsReorderQueueEntry *entry = &queue->queue[idx(queue->begin)];
			if(entry->set && queue->drop_cb)
				queue->drop_cb(queue->begin, entry->user, queue->drop_cb_user);
			queue->begin = add(queue->begin, 1);
			queue->count--;
			free_elems = QUEUE_SIZE - queue->count;
			total_end = add(end, free_elems);
		}

		// empty, just shift to the seq_num
		if(queue->count == 0)
			queue->begin = seq_num;
	}

	// move end until new_end
	end = add(queue->begin, queue->count);
	while(lt(end, new_end))
	{
		queue->count++;
		queue->queue[idx(end)].set = false;
		end = add(queue->begin, queue->count);
		assert(queue->count <= QUEUE_SIZE);
	}

	LabsReorderQueueEntry *entry = &queue->queue[idx(seq_num)];
	entry->set = true;
	entry->user = user;

	return;
drop_it:
	if(queue->drop_cb)
		queue->drop_cb(seq_num, user, queue->drop_cb_user);
}

LABS_EXPORT bool labs_reorder_queue_pull(LabsReorderQueue *queue, uint64_t *seq_num, void **user)
{
	assert(queue->count <= QUEUE_SIZE);
	if(queue->count == 0)
		return false;

	LabsReorderQueueEntry *entry = &queue->queue[idx(queue->begin)];
	if(!entry->set)
		return false;

	if(seq_num)
		*seq_num = queue->begin;
	if(user)
		*user = entry->user;
	queue->begin = add(queue->begin, 1);
	queue->count--;
	return true;
}

LABS_EXPORT bool labs_reorder_queue_peek(LabsReorderQueue *queue, uint64_t index, uint64_t *seq_num, void **user)
{
	if(index >= queue->count)
		return false;

	uint64_t seq_num_val = add(queue->begin, index);
	LabsReorderQueueEntry *entry = &queue->queue[idx(seq_num_val)];
	if(!entry->set)
		return false;

	*seq_num = seq_num_val;
	*user = entry->user;
	return true;
}

LABS_EXPORT void labs_reorder_queue_drop(LabsReorderQueue *queue, uint64_t index)
{
	if(index >= queue->count)
		return;

	uint64_t seq_num = add(queue->begin, index);
	LabsReorderQueueEntry *entry = &queue->queue[idx(seq_num)];
	if(!entry->set)
		return;

	if(queue->drop_cb)
		queue->drop_cb(seq_num, entry->user, queue->drop_cb_user);

	// reduce count if necessary
	if(index == queue->count - 1)
	{
		while(!entry->set)
		{
			queue->count--;
			if(queue->count == 0)
				break;
			seq_num = add(queue->begin, queue->count - 1);
			entry = &queue->queue[idx(seq_num)];
		}
	}
}
