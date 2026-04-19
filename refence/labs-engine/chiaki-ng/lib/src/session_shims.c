// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Exported wrappers for the `static inline` setters in session.h so that
// the managed P/Invoke layer (which can only bind real symbols) can hook
// event / video / audio / haptics / display callbacks.
//
// Also exposes the size of LabsSession so the managed side can allocate
// a correctly-sized opaque buffer.

#include <labs/session.h>

LABS_EXPORT size_t labs_session_sizeof(void)
{
    return sizeof(LabsSession);
}

LABS_EXPORT void labs_session_set_event_cb_ex(LabsSession *session, LabsEventCallback cb, void *user)
{
    labs_session_set_event_cb(session, cb, user);
}

LABS_EXPORT void labs_session_set_video_sample_cb_ex(LabsSession *session, LabsVideoSampleCallback cb, void *user)
{
    labs_session_set_video_sample_cb(session, cb, user);
}

LABS_EXPORT void labs_session_set_audio_sink_ex(LabsSession *session, LabsAudioSink *sink)
{
    labs_session_set_audio_sink(session, sink);
}

LABS_EXPORT void labs_session_set_haptics_sink_ex(LabsSession *session, LabsAudioSink *sink)
{
    labs_session_set_haptics_sink(session, sink);
}
