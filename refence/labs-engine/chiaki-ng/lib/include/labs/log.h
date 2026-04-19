// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_LOG_H
#define LABS_LOG_H

#include <stdint.h>
#include <stdlib.h>

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	LABS_LOG_DEBUG =		(1 << 4),
	LABS_LOG_VERBOSE =	(1 << 3),
	LABS_LOG_INFO =		(1 << 2),
	LABS_LOG_WARNING =	(1 << 1),
	LABS_LOG_ERROR =		(1 << 0)
} LabsLogLevel;

#define LABS_LOG_ALL ((1 << 5) - 1)

LABS_EXPORT char labs_log_level_char(LabsLogLevel level);

typedef void (*LabsLogCb)(LabsLogLevel level, const char *msg, void *user);

typedef struct labs_log_t
{
	uint32_t level_mask;
	LabsLogCb cb;
	void *user;
} LabsLog;

LABS_EXPORT void labs_log_init(LabsLog *log, uint32_t level_mask, LabsLogCb cb, void *user);
LABS_EXPORT void labs_log_set_level(LabsLog *log, uint32_t level_mask);

/**
 * Logging callback (LabsLogCb) that prints to stdout
 */
LABS_EXPORT void labs_log_cb_print(LabsLogLevel level, const char *msg, void *user);

LABS_EXPORT void labs_log(LabsLog *log, LabsLogLevel level, const char *fmt, ...);
LABS_EXPORT void labs_log_hexdump(LabsLog *log, LabsLogLevel level, const uint8_t *buf, size_t buf_size);
LABS_EXPORT void labs_log_hexdump_raw(LabsLog *log, LabsLogLevel level, const uint8_t *buf, size_t buf_size);

#define LABS_LOGD(log, ...) do { labs_log((log), LABS_LOG_DEBUG, __VA_ARGS__); } while(0)
#define LABS_LOGV(log, ...) do { labs_log((log), LABS_LOG_VERBOSE, __VA_ARGS__); } while(0)
#define LABS_LOGI(log, ...) do { labs_log((log), LABS_LOG_INFO, __VA_ARGS__); } while(0)
#define LABS_LOGW(log, ...) do { labs_log((log), LABS_LOG_WARNING, __VA_ARGS__); } while(0)
#define LABS_LOGE(log, ...) do { labs_log((log), LABS_LOG_ERROR, __VA_ARGS__); } while(0)

typedef struct labs_log_sniffer_t
{
	LabsLog *forward_log; // The original log, where everything is forwarded
	LabsLog sniff_log; // The log where others will log into
	uint32_t sniff_level_mask;
	char *buf; // always null-terminated
	size_t buf_len; // strlen(buf)
} LabsLogSniffer;

LABS_EXPORT void labs_log_sniffer_init(LabsLogSniffer *sniffer, uint32_t level_mask, LabsLog *forward_log);
LABS_EXPORT void labs_log_sniffer_fini(LabsLogSniffer *sniffer);
static inline LabsLog *labs_log_sniffer_get_log(LabsLogSniffer *sniffer) { return &sniffer->sniff_log; }
static inline const char *labs_log_sniffer_get_buffer(LabsLogSniffer *sniffer) { return sniffer->buf; }

#ifdef __cplusplus
}
#endif

#endif //LABS_LOG_H
