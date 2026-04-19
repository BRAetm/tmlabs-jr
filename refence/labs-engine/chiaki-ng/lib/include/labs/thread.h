// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_THREAD_H
#define LABS_THREAD_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef void *(*LabsThreadFunc)(void *);

typedef enum {
	LABS_THREAD_NAME_CTRL,
	LABS_THREAD_NAME_CONGESTION,
	LABS_THREAD_NAME_DISCOVERY,
	LABS_THREAD_NAME_DISCOVERY_SVC,
	LABS_THREAD_NAME_TAKION,
	LABS_THREAD_NAME_TAKION_SEND,
	LABS_THREAD_NAME_RUDP_SEND,
	LABS_THREAD_NAME_HOLEPUNCH,
	LABS_THREAD_NAME_FEEDBACK,
	LABS_THREAD_NAME_SESSION,
	LABS_THREAD_NAME_REGIST,
	LABS_THREAD_NAME_GKCRYPT
} LabsThreadName;

typedef void (*LabsThreadAffinityFunc)(LabsThreadName name, void *user);

typedef struct labs_thread_t
{
#ifdef _WIN32
	HANDLE thread;
	LabsThreadFunc func;
	void *arg;
	void *ret;
#else
	pthread_t thread;
#endif
} LabsThread;

LABS_EXPORT LabsErrorCode labs_thread_create(LabsThread *thread, LabsThreadFunc func, void *arg);
LABS_EXPORT LabsErrorCode labs_thread_join(LabsThread *thread, void **retval);
LABS_EXPORT LabsErrorCode labs_thread_timedjoin(LabsThread *thread, void **retval, uint64_t timeout_ms);
LABS_EXPORT LabsErrorCode labs_thread_set_name(LabsThread *thread, const char *name);
LABS_EXPORT void labs_thread_set_affinity(LabsThreadName name);
LABS_EXPORT void labs_thread_set_affinity_cb(LabsThreadAffinityFunc func, void *user);


typedef struct labs_mutex_t
{
#ifdef _WIN32
	CRITICAL_SECTION cs;
#else
	pthread_mutex_t mutex;
#endif
} LabsMutex;

LABS_EXPORT LabsErrorCode labs_mutex_init(LabsMutex *mutex, bool rec);
LABS_EXPORT LabsErrorCode labs_mutex_fini(LabsMutex *mutex);
LABS_EXPORT LabsErrorCode labs_mutex_lock(LabsMutex *mutex);
LABS_EXPORT LabsErrorCode labs_mutex_trylock(LabsMutex *mutex);
LABS_EXPORT LabsErrorCode labs_mutex_unlock(LabsMutex *mutex);


typedef struct labs_cond_t
{
#ifdef _WIN32
	CONDITION_VARIABLE cond;
#else
	pthread_cond_t cond;
#endif
} LabsCond;

typedef bool (*LabsCheckPred)(void *);

LABS_EXPORT LabsErrorCode labs_cond_init(LabsCond *cond);
LABS_EXPORT LabsErrorCode labs_cond_fini(LabsCond *cond);
LABS_EXPORT LabsErrorCode labs_cond_wait(LabsCond *cond, LabsMutex *mutex);
LABS_EXPORT LabsErrorCode labs_cond_timedwait(LabsCond *cond, LabsMutex *mutex, uint64_t timeout_ms);
LABS_EXPORT LabsErrorCode labs_cond_wait_pred(LabsCond *cond, LabsMutex *mutex, LabsCheckPred check_pred, void *check_pred_user);
LABS_EXPORT LabsErrorCode labs_cond_timedwait_pred(LabsCond *cond, LabsMutex *mutex, uint64_t timeout_ms, LabsCheckPred check_pred, void *check_pred_user);
LABS_EXPORT LabsErrorCode labs_cond_signal(LabsCond *cond);
LABS_EXPORT LabsErrorCode labs_cond_broadcast(LabsCond *cond);


typedef struct labs_bool_pred_cond_t
{
	LabsCond cond;
	LabsMutex mutex;
	bool pred;
} LabsBoolPredCond;

LABS_EXPORT LabsErrorCode labs_bool_pred_cond_init(LabsBoolPredCond *cond);
LABS_EXPORT LabsErrorCode labs_bool_pred_cond_fini(LabsBoolPredCond *cond);
LABS_EXPORT LabsErrorCode labs_bool_pred_cond_lock(LabsBoolPredCond *cond);
LABS_EXPORT LabsErrorCode labs_bool_pred_cond_unlock(LabsBoolPredCond *cond);
LABS_EXPORT LabsErrorCode labs_bool_pred_cond_wait(LabsBoolPredCond *cond);
LABS_EXPORT LabsErrorCode labs_bool_pred_cond_timedwait(LabsBoolPredCond *cond, uint64_t timeout_ms);
LABS_EXPORT LabsErrorCode labs_bool_pred_cond_signal(LabsBoolPredCond *cond);
LABS_EXPORT LabsErrorCode labs_bool_pred_cond_broadcast(LabsBoolPredCond *cond);

#ifdef __cplusplus
}

class LabsMutexLock
{
	private:
		LabsMutex * const mutex;

	public:
		LabsMutexLock(LabsMutex *mutex) : mutex(mutex) { labs_mutex_lock(mutex); }
		~LabsMutexLock() { labs_mutex_unlock(mutex); }
};
#endif

#endif // LABS_THREAD_H
