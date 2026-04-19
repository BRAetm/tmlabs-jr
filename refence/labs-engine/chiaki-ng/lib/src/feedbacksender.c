// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/feedbacksender.h>
#include <labs/time.h>

#include <string.h>

#define FEEDBACK_STATE_TIMEOUT_MIN_MS 8 // minimum time to wait between sending 2 packets
#define FEEDBACK_STATE_TIMEOUT_MAX_MS 200 // maximum time to wait between sending 2 packets

#define FEEDBACK_HISTORY_BUFFER_SIZE 0x10
#define FEEDBACK_HISTORY_RESEND_EVENT_COUNT 0x4

static void *feedback_sender_thread_func(void *user);
static void feedback_sender_send_state(LabsFeedbackSender *feedback_sender, const LabsControllerState *state);
static void feedback_sender_send_history_packet(LabsFeedbackSender *feedback_sender, const uint8_t *buf, size_t buf_size);
static void feedback_sender_flush_history_locked(LabsFeedbackSender *feedback_sender);
static void feedback_sender_record_history(LabsFeedbackSender *feedback_sender, const LabsControllerState *state_prev, const LabsControllerState *state_now);

LABS_EXPORT LabsErrorCode labs_feedback_sender_init(LabsFeedbackSender *feedback_sender, LabsTakion *takion)
{
	feedback_sender->log = takion->log;
	feedback_sender->takion = takion;

	labs_controller_state_set_idle(&feedback_sender->controller_state_prev);
	labs_controller_state_set_idle(&feedback_sender->controller_state_history_prev);
	labs_controller_state_set_idle(&feedback_sender->controller_state);

	feedback_sender->state_seq_num = 0;

	feedback_sender->history_seq_num = 0;
	feedback_sender->history_packet_begin = 0;
	feedback_sender->history_packet_len = 0;
	feedback_sender->should_stop = false;
	feedback_sender->controller_state_changed = false;
	feedback_sender->history_dirty = false;
	LabsErrorCode err = labs_feedback_history_buffer_init(&feedback_sender->history_buf, FEEDBACK_HISTORY_BUFFER_SIZE);
	if(err != LABS_ERR_SUCCESS)
		return err;

	err = labs_mutex_init(&feedback_sender->state_mutex, false);
	if(err != LABS_ERR_SUCCESS)
		goto error_history_buffer;

	err = labs_cond_init(&feedback_sender->state_cond);
	if(err != LABS_ERR_SUCCESS)
		goto error_mutex;

	err = labs_thread_create(&feedback_sender->thread, feedback_sender_thread_func, feedback_sender);
	if(err != LABS_ERR_SUCCESS)
		goto error_cond;

	labs_thread_set_name(&feedback_sender->thread, "Labs Feedback Sender");

	return LABS_ERR_SUCCESS;
error_cond:
	labs_cond_fini(&feedback_sender->state_cond);
error_mutex:
	labs_mutex_fini(&feedback_sender->state_mutex);
error_history_buffer:
	labs_feedback_history_buffer_fini(&feedback_sender->history_buf);
	return err;
}

LABS_EXPORT void labs_feedback_sender_fini(LabsFeedbackSender *feedback_sender)
{
	labs_mutex_lock(&feedback_sender->state_mutex);
	feedback_sender->should_stop = true;
	labs_mutex_unlock(&feedback_sender->state_mutex);
	labs_cond_signal(&feedback_sender->state_cond);
	labs_thread_join(&feedback_sender->thread, NULL);
	labs_cond_fini(&feedback_sender->state_cond);
	labs_mutex_fini(&feedback_sender->state_mutex);
	labs_feedback_history_buffer_fini(&feedback_sender->history_buf);
}

LABS_EXPORT LabsErrorCode labs_feedback_sender_set_controller_state(LabsFeedbackSender *feedback_sender, LabsControllerState *state)
{
	LabsErrorCode err = labs_mutex_lock(&feedback_sender->state_mutex);
	if(err != LABS_ERR_SUCCESS)
		return err;

	if(labs_controller_state_equals(&feedback_sender->controller_state, state))
	{
		labs_mutex_unlock(&feedback_sender->state_mutex);
		return LABS_ERR_SUCCESS;
	}

	feedback_sender->controller_state = *state;
	feedback_sender_record_history(feedback_sender, &feedback_sender->controller_state_history_prev, &feedback_sender->controller_state);
	feedback_sender_flush_history_locked(feedback_sender);
	feedback_sender->controller_state_history_prev = feedback_sender->controller_state;
	feedback_sender->controller_state_changed = true;

	labs_mutex_unlock(&feedback_sender->state_mutex);
	labs_cond_signal(&feedback_sender->state_cond);

	return LABS_ERR_SUCCESS;
}

static bool controller_state_equals_for_feedback_state(LabsControllerState *a, LabsControllerState *b)
{
	if(!(a->left_x == b->left_x
		&& a->left_y == b->left_y
		&& a->right_x == b->right_x
		&& a->right_y == b->right_y))
		return false;
#define CHECKF(n) if(a->n < b->n - 0.0000001f || a->n > b->n + 0.0000001f) return false
	CHECKF(gyro_x);
	CHECKF(gyro_y);
	CHECKF(gyro_z);
	CHECKF(accel_x);
	CHECKF(accel_y);
	CHECKF(accel_z);
	CHECKF(orient_x);
	CHECKF(orient_y);
	CHECKF(orient_z);
	CHECKF(orient_w);
#undef CHECKF
	return true;
}

static void feedback_sender_send_state(LabsFeedbackSender *feedback_sender, const LabsControllerState *state)
{
	LabsFeedbackState feedback_state;
	feedback_state.left_x = state->left_x;
	feedback_state.left_y = state->left_y;
	feedback_state.right_x = state->right_x;
	feedback_state.right_y = state->right_y;
	feedback_state.gyro_x = state->gyro_x;
	feedback_state.gyro_y = state->gyro_y;
	feedback_state.gyro_z = state->gyro_z;
	feedback_state.accel_x = state->accel_x;
	feedback_state.accel_y = state->accel_y;
	feedback_state.accel_z = state->accel_z;

	feedback_state.orient_x = state->orient_x;
	feedback_state.orient_y = state->orient_y;
	feedback_state.orient_z = state->orient_z;
	feedback_state.orient_w = state->orient_w;

	LabsErrorCode err = labs_takion_send_feedback_state(feedback_sender->takion, feedback_sender->state_seq_num++, &feedback_state);
	if(err != LABS_ERR_SUCCESS)
		LABS_LOGE(feedback_sender->log, "FeedbackSender failed to send Feedback State");
}

static void feedback_sender_send_history_packet(LabsFeedbackSender *feedback_sender, const uint8_t *buf, size_t buf_size)
{
	//LABS_LOGD(feedback_sender->log, "Feedback History:");
	//labs_log_hexdump(feedback_sender->log, LABS_LOG_DEBUG, buf, buf_size);
	labs_takion_send_feedback_history(feedback_sender->takion, feedback_sender->history_seq_num++, (uint8_t *)buf, buf_size);
}

static void feedback_sender_flush_history_locked(LabsFeedbackSender *feedback_sender)
{
	if(!feedback_sender->history_dirty)
		return;

	size_t packet_index = (feedback_sender->history_packet_begin + feedback_sender->history_packet_len)
		% LABS_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE;
	size_t packet_size = LABS_FEEDBACK_HISTORY_PACKET_BUF_SIZE;
	LabsErrorCode err = labs_feedback_history_buffer_format(
			&feedback_sender->history_buf,
			feedback_sender->history_packets[packet_index],
			&packet_size);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(feedback_sender->log, "Feedback Sender failed to format history buffer");
		return;
	}

	if(feedback_sender->history_packet_len < LABS_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE)
	{
		feedback_sender->history_packet_sizes[packet_index] = packet_size;
		feedback_sender->history_packet_len++;
	}
	else
	{
		feedback_sender->history_packet_sizes[feedback_sender->history_packet_begin] = packet_size;
		memcpy(
			feedback_sender->history_packets[feedback_sender->history_packet_begin],
			feedback_sender->history_packets[packet_index],
			packet_size);
		feedback_sender->history_packet_begin = (feedback_sender->history_packet_begin + 1)
			% LABS_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE;
		LABS_LOGW(feedback_sender->log, "Feedback Sender history packet queue overflow");
	}

	if(feedback_sender->history_buf.len > FEEDBACK_HISTORY_RESEND_EVENT_COUNT)
		feedback_sender->history_buf.len = FEEDBACK_HISTORY_RESEND_EVENT_COUNT;
	feedback_sender->history_dirty = false;
}

static void feedback_sender_record_history(LabsFeedbackSender *feedback_sender, const LabsControllerState *state_prev, const LabsControllerState *state_now)
{
	uint64_t buttons_prev = state_prev->buttons;
	uint64_t buttons_now = state_now->buttons;
	for(uint8_t i=0; i<LABS_CONTROLLER_BUTTONS_COUNT; i++)
	{
		uint64_t button_id = 1 << i;
		bool prev = buttons_prev & button_id;
		bool now = buttons_now & button_id;
		if(prev != now)
		{
			LabsFeedbackHistoryEvent event;
			LabsErrorCode err = labs_feedback_history_event_set_button(&event, button_id, now ? 0xff : 0);
			if(err != LABS_ERR_SUCCESS)
			{
				LABS_LOGE(feedback_sender->log, "Feedback Sender failed to format button history event for button id %llu", (unsigned long long)button_id);
				continue;
			}
			labs_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender->history_dirty = true;
		}
	}

	if(state_prev->l2_state != state_now->l2_state)
	{
		LabsFeedbackHistoryEvent event;
		LabsErrorCode err = labs_feedback_history_event_set_button(&event, LABS_CONTROLLER_ANALOG_BUTTON_L2, state_now->l2_state);
		if(err == LABS_ERR_SUCCESS)
		{
			labs_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender->history_dirty = true;
		}
		else
			LABS_LOGE(feedback_sender->log, "Feedback Sender failed to format button history event for L2");
	}

	if(state_prev->r2_state != state_now->r2_state)
	{
		LabsFeedbackHistoryEvent event;
		LabsErrorCode err = labs_feedback_history_event_set_button(&event, LABS_CONTROLLER_ANALOG_BUTTON_R2, state_now->r2_state);
		if(err == LABS_ERR_SUCCESS)
		{
			labs_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender->history_dirty = true;
		}
		else
			LABS_LOGE(feedback_sender->log, "Feedback Sender failed to format button history event for R2");
	}

	for(size_t i=0; i<LABS_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(state_prev->touches[i].id != state_now->touches[i].id && state_prev->touches[i].id >= 0)
		{
			LabsFeedbackHistoryEvent event;
			labs_feedback_history_event_set_touchpad(&event, false, (uint8_t)state_prev->touches[i].id,
					state_prev->touches[i].x, state_prev->touches[i].y);
			labs_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender->history_dirty = true;
		}
		else if(state_now->touches[i].id >= 0
				&& (state_prev->touches[i].id != state_now->touches[i].id
					|| state_prev->touches[i].x != state_now->touches[i].x
					|| state_prev->touches[i].y != state_now->touches[i].y))
		{
			LabsFeedbackHistoryEvent event;
			labs_feedback_history_event_set_touchpad(&event, true, (uint8_t)state_now->touches[i].id,
					state_now->touches[i].x, state_now->touches[i].y);
			labs_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender->history_dirty = true;
		}
	}
}

static bool state_cond_check(void *user)
{
	LabsFeedbackSender *feedback_sender = user;
	return feedback_sender->should_stop
		|| feedback_sender->controller_state_changed;
}

static void *feedback_sender_thread_func(void *user)
{
	LabsFeedbackSender *feedback_sender = user;
	labs_thread_set_affinity(LABS_THREAD_NAME_FEEDBACK);

	LabsErrorCode err = labs_mutex_lock(&feedback_sender->state_mutex);
	if(err != LABS_ERR_SUCCESS)
		return NULL;

	uint64_t last_feedback_state_ms = labs_time_now_monotonic_ms();
	while(true)
	{
		if(feedback_sender->history_packet_len == 0)
		{
			uint64_t now_ms = labs_time_now_monotonic_ms();
			uint64_t next_timeout = FEEDBACK_STATE_TIMEOUT_MAX_MS;
			if(now_ms - last_feedback_state_ms < FEEDBACK_STATE_TIMEOUT_MAX_MS)
				next_timeout = FEEDBACK_STATE_TIMEOUT_MAX_MS - (now_ms - last_feedback_state_ms);

			err = labs_cond_timedwait_pred(&feedback_sender->state_cond, &feedback_sender->state_mutex, next_timeout, state_cond_check, feedback_sender);
			if(err != LABS_ERR_SUCCESS && err != LABS_ERR_TIMEOUT)
				break;
		}
		else
		{
			err = LABS_ERR_SUCCESS;
		}

		if(feedback_sender->should_stop)
			break;

		uint64_t now_ms = labs_time_now_monotonic_ms();
		bool send_feedback_state = now_ms - last_feedback_state_ms >= FEEDBACK_STATE_TIMEOUT_MAX_MS;
		LabsControllerState state_now = feedback_sender->controller_state;
		bool send_feedback_history = false;
		uint8_t history_buf[LABS_FEEDBACK_HISTORY_PACKET_BUF_SIZE];
		size_t history_buf_size = 0;

		if(feedback_sender->controller_state_changed)
		{
			// TODO: FEEDBACK_STATE_TIMEOUT_MIN_MS
			feedback_sender->controller_state_changed = false;
			send_feedback_state = true;

			// don't need to send feedback state if nothing relevant changed
			if(controller_state_equals_for_feedback_state(&state_now, &feedback_sender->controller_state_prev))
				send_feedback_state = false;
		} // else: timeout

		if(feedback_sender->history_packet_len > 0)
		{
			size_t packet_index = feedback_sender->history_packet_begin;
			history_buf_size = feedback_sender->history_packet_sizes[packet_index];
			memcpy(history_buf, feedback_sender->history_packets[packet_index], history_buf_size);
			feedback_sender->history_packet_begin = (feedback_sender->history_packet_begin + 1)
				% LABS_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE;
			feedback_sender->history_packet_len--;
			send_feedback_history = true;
		}
		labs_mutex_unlock(&feedback_sender->state_mutex);

		if(send_feedback_state)
			feedback_sender_send_state(feedback_sender, &state_now);

		if(send_feedback_history)
			feedback_sender_send_history_packet(feedback_sender, history_buf, history_buf_size);

		err = labs_mutex_lock(&feedback_sender->state_mutex);
		if(err != LABS_ERR_SUCCESS)
			return NULL;
		if(send_feedback_state)
		{
			feedback_sender->controller_state_prev = state_now;
			last_feedback_state_ms = labs_time_now_monotonic_ms();
		}
	}

	labs_mutex_unlock(&feedback_sender->state_mutex);

	return NULL;
}
