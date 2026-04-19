// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_CTRL_H
#define LABS_CTRL_H

#include "common.h"
#include "thread.h"
#include "stoppipe.h"

#include <stdint.h>
#include <stdbool.h>

#if _WIN32
#include <winsock2.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*LabsCantDisplayCb)(void *user, bool cant_display);

typedef struct labs_ctrl_message_queue_t LabsCtrlMessageQueue;

typedef struct labs_ctrl_display_sink_t
{
	void *user;
	LabsCantDisplayCb cantdisplay_cb;
} LabsCtrlDisplaySink;

typedef struct labs_ctrl_t
{
	struct labs_session_t *session;
	LabsThread thread;

	bool should_stop;
	bool login_pin_entered;
	uint8_t *login_pin;
	size_t login_pin_size;
	LabsCtrlMessageQueue *msg_queue;
	LabsStopPipe stop_pipe;
	LabsStopPipe notif_pipe;
	LabsMutex notif_mutex;

	bool login_pin_requested;
	bool cant_displaya;
	bool cant_displayb;

	labs_socket_t sock;

#ifdef __GNUC__
	__attribute__((aligned(__alignof__(uint32_t))))
#endif
	uint8_t recv_buf[512];
	uint8_t rudp_recv_buf[520];

	size_t recv_buf_size;
	uint64_t crypt_counter_local;
	uint64_t crypt_counter_remote;
	uint32_t keyboard_text_counter;
} LabsCtrl;

LABS_EXPORT LabsErrorCode labs_ctrl_init(LabsCtrl *ctrl, struct labs_session_t *session);
LABS_EXPORT LabsErrorCode labs_ctrl_start(LabsCtrl *ctrl);
LABS_EXPORT void labs_ctrl_stop(LabsCtrl *ctrl);
LABS_EXPORT LabsErrorCode labs_ctrl_join(LabsCtrl *ctrl);
LABS_EXPORT void labs_ctrl_fini(LabsCtrl *ctrl);
LABS_EXPORT LabsErrorCode labs_ctrl_send_message(LabsCtrl *ctrl, uint16_t type, const uint8_t *payload, size_t payload_size);
LABS_EXPORT LabsErrorCode ctrl_message_toggle_microphone(LabsCtrl *ctrl, bool muted);
LABS_EXPORT LabsErrorCode ctrl_message_connect_microphone(LabsCtrl *ctrl);
LABS_EXPORT void labs_ctrl_set_login_pin(LabsCtrl *ctrl, const uint8_t *pin, size_t pin_size);
LABS_EXPORT LabsErrorCode labs_ctrl_goto_bed(LabsCtrl *ctrl);
LABS_EXPORT LabsErrorCode labs_ctrl_keyboard_set_text(LabsCtrl *ctrl, const char* text);
LABS_EXPORT LabsErrorCode labs_ctrl_keyboard_accept(LabsCtrl *ctrl);
LABS_EXPORT LabsErrorCode labs_ctrl_keyboard_reject(LabsCtrl *ctrl);
LABS_EXPORT LabsErrorCode ctrl_message_go_home(LabsCtrl *ctrl);
LABS_EXPORT LabsErrorCode ctrl_message_set_fallback_session_id(LabsCtrl *ctrl);
LABS_EXPORT void ctrl_enable_features(LabsCtrl *ctrl);

#ifdef __cplusplus
}
#endif

#endif // LABS_CTRL_H
