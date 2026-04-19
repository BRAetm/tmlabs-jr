// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#define _GNU_SOURCE

#include <labs/thread.h>
#include <labs/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#if defined(__ANDROID__)
struct labs_timedjoin_ctx
{
	pthread_t target;
	void *retval;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	bool done;
	bool timed_out;
};

static void *labs_timedjoin_helper(void *arg)
{
	struct labs_timedjoin_ctx *ctx = (struct labs_timedjoin_ctx *)arg;
	void *retval = NULL;
	pthread_join(ctx->target, &retval);

	pthread_mutex_lock(&ctx->mutex);
	ctx->retval = retval;
	ctx->done = true;
	pthread_cond_signal(&ctx->cond);
	bool timed_out = ctx->timed_out;
	pthread_mutex_unlock(&ctx->mutex);

	if(timed_out)
	{
		pthread_mutex_destroy(&ctx->mutex);
		pthread_cond_destroy(&ctx->cond);
		free(ctx);
	}
	return NULL;
}
#endif

#if _WIN32
static DWORD WINAPI win32_thread_func(LPVOID param)
{
	LabsThread *thread = (LabsThread *)param;
	thread->ret = thread->func(thread->arg);
	return 0;
}
#endif

#ifdef __SWITCH__
int64_t get_thread_limit()
{
	uint64_t resource_limit_handle_value = INVALID_HANDLE;
	svcGetInfo(&resource_limit_handle_value, InfoType_ResourceLimit, INVALID_HANDLE, 0);
	int64_t thread_cur_value = 0, thread_lim_value = 0;
	svcGetResourceLimitCurrentValue(&thread_cur_value, resource_limit_handle_value, LimitableResource_Threads);
	svcGetResourceLimitLimitValue(&thread_lim_value, resource_limit_handle_value, LimitableResource_Threads);
	//printf("thread_cur_value: %lu, thread_lim_value: %lu\n", thread_cur_value, thread_lim_value);
	return thread_lim_value - thread_cur_value;
}
#endif

LABS_EXPORT LabsErrorCode labs_thread_create(LabsThread *thread, LabsThreadFunc func, void *arg)
{
#if _WIN32
	thread->func = func;
	thread->arg = arg;
	thread->ret = NULL;
	thread->thread = CreateThread(NULL, 0, win32_thread_func, thread, 0, 0);
	if(!thread->thread)
		return LABS_ERR_THREAD;
#else
#ifdef __SWITCH__
	if(get_thread_limit() <= 1)
		return LABS_ERR_THREAD;
#endif
	int r = pthread_create(&thread->thread, NULL, func, arg);
	if(r != 0)
		return LABS_ERR_THREAD;
#endif
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_thread_join(LabsThread *thread, void **retval)
{
#if _WIN32
	int r = WaitForSingleObject(thread->thread, INFINITE);
	if(r != WAIT_OBJECT_0)
		return LABS_ERR_THREAD;
	if(retval)
		*retval = thread->ret;
#else
	int r = pthread_join(thread->thread, retval);
	if(r != 0)
		return LABS_ERR_THREAD;
#endif
	return LABS_ERR_SUCCESS;
}

//#define LABS_WINDOWS_THREAD_NAME

LABS_EXPORT LabsErrorCode labs_thread_set_name(LabsThread *thread, const char *name)
{
#if defined(_WIN32) && defined(LABS_WINDOWS_THREAD_NAME)
	int len = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
	wchar_t *wstr = calloc(sizeof(wchar_t), len+1);
	if(!wstr)
		return LABS_ERR_MEMORY;
	MultiByteToWideChar(CP_UTF8, 0, name, -1, wstr, len);
	SetThreadDescription(thread->thread, wstr);
	free(wstr);
#else
#ifdef __GLIBC__
	int r = pthread_setname_np(thread->thread, name);
	if(r != 0)
		return LABS_ERR_THREAD;
#else
	(void)thread;
	(void)name;
#endif
#endif
	return LABS_ERR_SUCCESS;
}

static LabsThreadAffinityFunc g_affinity_cb = NULL;
static void *g_affinity_cb_user = NULL;

LABS_EXPORT void labs_thread_set_affinity(LabsThreadName name)
{
	if(g_affinity_cb)
		g_affinity_cb(name, g_affinity_cb_user);
}

LABS_EXPORT void labs_thread_set_affinity_cb(LabsThreadAffinityFunc func, void *user)
{
	g_affinity_cb = func;
	g_affinity_cb_user = user;
}

LABS_EXPORT LabsErrorCode labs_mutex_init(LabsMutex *mutex, bool rec)
{
#if _WIN32
	InitializeCriticalSection(&mutex->cs);
	(void)rec; // always recursive
#else
	pthread_mutexattr_t attr;
	int r = pthread_mutexattr_init(&attr);
	if(r != 0)
		return LABS_ERR_UNKNOWN;

	pthread_mutexattr_settype(&attr, rec ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_DEFAULT);

	r = pthread_mutex_init(&mutex->mutex, &attr);

	pthread_mutexattr_destroy(&attr);

	if(r != 0)
		return LABS_ERR_UNKNOWN;
#endif
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_mutex_fini(LabsMutex *mutex)
{
#if _WIN32
	DeleteCriticalSection(&mutex->cs);
#else
	int r = pthread_mutex_destroy(&mutex->mutex);
	if(r != 0)
		return LABS_ERR_UNKNOWN;
#endif
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_mutex_lock(LabsMutex *mutex)
{
#if _WIN32
	EnterCriticalSection(&mutex->cs);
#else
	int r = pthread_mutex_lock(&mutex->mutex);
	if(r != 0)
		return LABS_ERR_UNKNOWN;
#endif
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_mutex_trylock(LabsMutex *mutex)
{
#if _WIN32
	int r = TryEnterCriticalSection(&mutex->cs);
	if(!r)
		return LABS_ERR_MUTEX_LOCKED;
#else
	int r = pthread_mutex_trylock(&mutex->mutex);
	if(r == EBUSY)
		return LABS_ERR_MUTEX_LOCKED;
	else if(r != 0)
		return LABS_ERR_UNKNOWN;
#endif
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_mutex_unlock(LabsMutex *mutex)
{
#if _WIN32
	LeaveCriticalSection(&mutex->cs);
#else
	int r = pthread_mutex_unlock(&mutex->mutex);
	if(r != 0)
		return LABS_ERR_UNKNOWN;
#endif
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_cond_init(LabsCond *cond)
{
#if _WIN32
	InitializeConditionVariable(&cond->cond);
#else
	pthread_condattr_t attr;
	int r = pthread_condattr_init(&attr);
	if(r != 0)
		return LABS_ERR_UNKNOWN;
#if !__APPLE__
	r = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
	if(r != 0)
	{
		pthread_condattr_destroy(&attr);
		return LABS_ERR_UNKNOWN;
	}
#endif
	r = pthread_cond_init(&cond->cond, &attr);
	if(r != 0)
	{
		pthread_condattr_destroy(&attr);
		return LABS_ERR_UNKNOWN;
	}
	pthread_condattr_destroy(&attr);
#endif
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_cond_fini(LabsCond *cond)
{
#if _WIN32
#else
	int r = pthread_cond_destroy(&cond->cond);
	if(r != 0)
		return LABS_ERR_UNKNOWN;
#endif
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_cond_wait(LabsCond *cond, LabsMutex *mutex)
{
#if _WIN32
	int r = SleepConditionVariableCS(&cond->cond, &mutex->cs, INFINITE);
	if(!r)
		return LABS_ERR_THREAD;
#else
	int r = pthread_cond_wait(&cond->cond, &mutex->mutex);
	if(r != 0)
		return LABS_ERR_UNKNOWN;
#endif
	return LABS_ERR_SUCCESS;
}

#if !__APPLE__ && !defined(_WIN32)
static LabsErrorCode labs_cond_timedwait_abs(LabsCond *cond, LabsMutex *mutex, struct timespec *timeout)
{
	int r = pthread_cond_timedwait(&cond->cond, &mutex->mutex, timeout);
	if(r != 0)
	{
		if(r == ETIMEDOUT)
			return LABS_ERR_TIMEOUT;
		return LABS_ERR_UNKNOWN;
	}
	return LABS_ERR_SUCCESS;
}

static void set_timeout(struct timespec *timeout, uint64_t ms_from_now)
{
	clock_gettime(CLOCK_MONOTONIC, timeout);
	timeout->tv_sec += ms_from_now / 1000;
	timeout->tv_nsec += (ms_from_now % 1000) * 1000000;
	if(timeout->tv_nsec > 1000000000)
	{
		timeout->tv_sec += timeout->tv_nsec / 1000000000;
		timeout->tv_nsec %= 1000000000;
	}
}
#endif

#if !__APPLE__ && !__SWITCH__
LABS_EXPORT LabsErrorCode labs_thread_timedjoin(LabsThread *thread, void **retval, uint64_t timeout_ms)
{
#if _WIN32
	int r = WaitForSingleObject(thread->thread, timeout_ms);
	if(r != WAIT_OBJECT_0)
		return LABS_ERR_THREAD;
	if(retval)
		*retval = thread->ret;
#elif defined(__ANDROID__)
	// Android Bionic lacks pthread_clockjoin_np/pthread_timedjoin_np.
	// Use a helper thread + condvar to implement timed join portably.
	struct labs_timedjoin_ctx *ctx = calloc(1, sizeof(*ctx));
	if(!ctx)
		return LABS_ERR_MEMORY;

	ctx->target = thread->thread;
	ctx->done = false;
	ctx->timed_out = false;
	pthread_mutex_init(&ctx->mutex, NULL);

	pthread_condattr_t cattr;
	pthread_condattr_init(&cattr);
	pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
	pthread_cond_init(&ctx->cond, &cattr);
	pthread_condattr_destroy(&cattr);

	pthread_t helper;
	if(pthread_create(&helper, NULL, labs_timedjoin_helper, ctx) != 0)
	{
		pthread_mutex_destroy(&ctx->mutex);
		pthread_cond_destroy(&ctx->cond);
		free(ctx);
		return LABS_ERR_THREAD;
	}
	pthread_detach(helper);

	struct timespec timeout;
	set_timeout(&timeout, timeout_ms);

	pthread_mutex_lock(&ctx->mutex);
	int r = 0;
	while(!ctx->done && r != ETIMEDOUT)
		r = pthread_cond_timedwait(&ctx->cond, &ctx->mutex, &timeout);

	if(ctx->done)
	{
		if(retval)
			*retval = ctx->retval;
		pthread_mutex_unlock(&ctx->mutex);
		pthread_mutex_destroy(&ctx->mutex);
		pthread_cond_destroy(&ctx->cond);
		free(ctx);
	}
	else
	{
		ctx->timed_out = true;
		pthread_mutex_unlock(&ctx->mutex);
		return LABS_ERR_TIMEOUT;
	}
#else
	struct timespec timeout;
	set_timeout(&timeout, timeout_ms);
	int r = pthread_clockjoin_np(thread->thread, retval, CLOCK_MONOTONIC, &timeout);
	if(r != 0)
	{
		if(r == ETIMEDOUT)
			return LABS_ERR_TIMEOUT;
		return LABS_ERR_THREAD;
	}
#endif
	return LABS_ERR_SUCCESS;
}
#endif

LABS_EXPORT LabsErrorCode labs_cond_timedwait(LabsCond *cond, LabsMutex *mutex, uint64_t timeout_ms)
{
#if _WIN32
	int r = SleepConditionVariableCS(&cond->cond, &mutex->cs, (DWORD)timeout_ms);
	if(!r)
	{
		if(GetLastError() == ERROR_TIMEOUT)
			return LABS_ERR_TIMEOUT;
		return LABS_ERR_THREAD;
	}
	return LABS_ERR_SUCCESS;
#else
	struct timespec timeout;
#if __APPLE__
	timeout.tv_sec = (__darwin_time_t)(timeout_ms / 1000);
	timeout.tv_nsec = (long)((timeout_ms % 1000) * 1000000);
	int r = pthread_cond_timedwait_relative_np(&cond->cond, &mutex->mutex, &timeout);
	if(r != 0)
	{
		if(r == ETIMEDOUT)
			return LABS_ERR_TIMEOUT;
		return LABS_ERR_UNKNOWN;
	}
	return LABS_ERR_SUCCESS;
#else
	set_timeout(&timeout, timeout_ms);
	return labs_cond_timedwait_abs(cond, mutex, &timeout);
#endif
#endif
}

LABS_EXPORT LabsErrorCode labs_cond_wait_pred(LabsCond *cond, LabsMutex *mutex, LabsCheckPred check_pred, void *check_pred_user)
{
	while(!check_pred(check_pred_user))
	{
		LabsErrorCode err = labs_cond_wait(cond, mutex);
		if(err != LABS_ERR_SUCCESS)
			return err;
	}
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_cond_timedwait_pred(LabsCond *cond, LabsMutex *mutex, uint64_t timeout_ms, LabsCheckPred check_pred, void *check_pred_user)
{
#if __APPLE__ || defined(_WIN32)
	uint64_t start_time = labs_time_now_monotonic_ms();
	uint64_t elapsed = 0;
#else
	struct timespec timeout;
	set_timeout(&timeout, timeout_ms);
#endif
	while(!check_pred(check_pred_user))
	{
#if __APPLE__ || defined(_WIN32)
		LabsErrorCode err = labs_cond_timedwait(cond, mutex, timeout_ms - elapsed);
#else
		LabsErrorCode err = labs_cond_timedwait_abs(cond, mutex, &timeout);
#endif
		if(err != LABS_ERR_SUCCESS)
			return err;
#if __APPLE__ || defined(_WIN32)
		elapsed = labs_time_now_monotonic_ms() - start_time;
		if(elapsed >= timeout_ms)
			return LABS_ERR_TIMEOUT;
#endif
	}
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_cond_signal(LabsCond *cond)
{
#if _WIN32
	WakeConditionVariable(&cond->cond);
#else
	int r = pthread_cond_signal(&cond->cond);
	if(r != 0)
		return LABS_ERR_UNKNOWN;
#endif
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_cond_broadcast(LabsCond *cond)
{
#if _WIN32
	WakeAllConditionVariable(&cond->cond);
#else
	int r = pthread_cond_broadcast(&cond->cond);
	if(r != 0)
		return LABS_ERR_UNKNOWN;
#endif
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_bool_pred_cond_init(LabsBoolPredCond *cond)
{
	cond->pred = false;

	LabsErrorCode err = labs_mutex_init(&cond->mutex, false);
	if(err != LABS_ERR_SUCCESS)
		return err;

	err = labs_cond_init(&cond->cond);
	if(err != LABS_ERR_SUCCESS)
	{
		labs_mutex_fini(&cond->mutex);
		return err;
	}

	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_bool_pred_cond_fini(LabsBoolPredCond *cond)
{
	LabsErrorCode err = labs_cond_fini(&cond->cond);
	if(err != LABS_ERR_SUCCESS)
		return err;

	err = labs_mutex_fini(&cond->mutex);
	if(err != LABS_ERR_SUCCESS)
		return err;

	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_bool_pred_cond_lock(LabsBoolPredCond *cond)
{
	return labs_mutex_lock(&cond->mutex);
}

LABS_EXPORT LabsErrorCode labs_bool_pred_cond_unlock(LabsBoolPredCond *cond)
{
	return labs_mutex_unlock(&cond->mutex);
}

bool bool_pred_cond_check_pred(void *user)
{
	LabsBoolPredCond *bool_pred_cond = user;
	return bool_pred_cond->pred;
}

LABS_EXPORT LabsErrorCode labs_bool_pred_cond_wait(LabsBoolPredCond *cond)
{
	return labs_cond_wait_pred(&cond->cond, &cond->mutex, bool_pred_cond_check_pred, cond);
}

LABS_EXPORT LabsErrorCode labs_bool_pred_cond_timedwait(LabsBoolPredCond *cond, uint64_t timeout_ms)
{
	return labs_cond_timedwait_pred(&cond->cond, &cond->mutex, timeout_ms, bool_pred_cond_check_pred, cond);
}

LABS_EXPORT LabsErrorCode labs_bool_pred_cond_signal(LabsBoolPredCond *cond)
{
	LabsErrorCode err = labs_bool_pred_cond_lock(cond);
	if(err != LABS_ERR_SUCCESS)
		return err;

	cond->pred = true;

	err = labs_bool_pred_cond_unlock(cond);
	if(err != LABS_ERR_SUCCESS)
		return err;

	return labs_cond_signal(&cond->cond);
}

LABS_EXPORT LabsErrorCode labs_bool_pred_cond_broadcast(LabsBoolPredCond *cond)
{
	LabsErrorCode err = labs_bool_pred_cond_lock(cond);
	if(err != LABS_ERR_SUCCESS)
		return err;

	cond->pred = true;

	err = labs_bool_pred_cond_unlock(cond);
	if(err != LABS_ERR_SUCCESS)
		return err;

	return labs_cond_broadcast(&cond->cond);
}
