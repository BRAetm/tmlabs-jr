// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_REGIST_H
#define LABS_REGIST_H

#include "common.h"
#include "log.h"
#include "thread.h"
#include "stoppipe.h"
#include "rpcrypt.h"
#include "remote/holepunch.h"
#include "remote/rudp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LABS_PSN_ACCOUNT_ID_SIZE 8
#define LABS_SESSION_AUTH_SIZE 0x10

typedef struct labs_regist_info_t
{
	LabsTarget target;
	const char *host;
	bool broadcast;

	/**
	 * may be null, in which case psn_account_id will be used
	 */
	const char *psn_online_id;

	/**
	 * will be used if psn_online_id is null, for PS4 >= 7.0
	 */
	uint8_t psn_account_id[LABS_PSN_ACCOUNT_ID_SIZE];

	uint32_t pin;
	uint32_t console_pin;
	/**
	 * may be null, in which regular regist (instead of PSN Regist will be used)
	 */
	LabsHolepunchRegistInfo* holepunch_info;
	/**
	 * NULL unless using PSN Regist
	 */
	LabsRudp rudp;
} LabsRegistInfo;

typedef struct labs_registered_host_t
{
	LabsTarget target;
	char ap_ssid[0x30];
	char ap_bssid[0x20];
	char ap_key[0x50];
	char ap_name[0x20];
	uint8_t server_mac[6];
	char server_nickname[0x20];
	char rp_regist_key[LABS_SESSION_AUTH_SIZE]; // must be completely filled (pad with \0)
	uint32_t rp_key_type;
	uint8_t rp_key[0x10];
	uint32_t console_pin;
} LabsRegisteredHost;

typedef enum labs_regist_event_type_t {
	LABS_REGIST_EVENT_TYPE_FINISHED_CANCELED,
	LABS_REGIST_EVENT_TYPE_FINISHED_FAILED,
	LABS_REGIST_EVENT_TYPE_FINISHED_SUCCESS
} LabsRegistEventType;

typedef struct labs_regist_event_t
{
	LabsRegistEventType type;
	LabsRegisteredHost *registered_host;
} LabsRegistEvent;

typedef void (*LabsRegistCb)(LabsRegistEvent *event, void *user);

typedef struct labs_regist_t
{
	LabsLog *log;
	LabsRegistInfo info;
	LabsRegistCb cb;
	void *cb_user;

	LabsThread thread;
	LabsStopPipe stop_pipe;
} LabsRegist;

LABS_EXPORT LabsErrorCode labs_regist_start(LabsRegist *regist, LabsLog *log, const LabsRegistInfo *info, LabsRegistCb cb, void *cb_user);
LABS_EXPORT void labs_regist_fini(LabsRegist *regist);
LABS_EXPORT void labs_regist_stop(LabsRegist *regist);

/**
 * @param psn_account_id must be exactly of size LABS_PSN_ACCOUNT_ID_SIZE
 */
LABS_EXPORT LabsErrorCode labs_regist_request_payload_format(LabsTarget target, const uint8_t *ambassador, uint8_t *buf, size_t *buf_size, LabsRPCrypt *crypt, const char *psn_online_id, const uint8_t *psn_account_id, uint32_t pin, LabsHolepunchRegistInfo *holepunch_info);

#ifdef __cplusplus
}
#endif

#endif // LABS_REGIST_H
