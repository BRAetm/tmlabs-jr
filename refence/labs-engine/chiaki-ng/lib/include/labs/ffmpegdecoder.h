#ifndef LABS_FFMPEG_DECODER_H
#define LABS_FFMPEG_DECODER_H

#include <labs/config.h>
#include <labs/log.h>
#include <labs/thread.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>

typedef struct labs_ffmpeg_decoder_t LabsFfmpegDecoder;

typedef struct labs_ffmpeg_frame_t LabsFfmpegFrame;

typedef void (*LabsFfmpegFrameAvailable)(LabsFfmpegDecoder *decover, void *user);

struct labs_ffmpeg_frame_t
{
	AVFrame *frame;
	double pts;
	double duration;
	bool recovered;
};

struct labs_ffmpeg_decoder_t
{
	LabsLog *log;
	LabsMutex mutex;
	const AVCodec *av_codec;
	AVCodecContext *codec_context;
	enum AVPixelFormat hw_pix_fmt;
	AVBufferRef *hw_device_ctx;
	bool hdr_enabled;
	LabsMutex cb_mutex;
	LabsFfmpegFrameAvailable frame_available_cb;
	void *frame_available_cb_user;
	int32_t frames_lost;
	bool frame_recovered;
	int32_t session_bitrate_kbps;
	int64_t synthetic_packet_pts;
	AVRational synthetic_time_base;
	AVRational synthetic_framerate;
	double synthetic_frame_duration_us;
	double synthetic_candidate_duration_us;
	uint64_t synthetic_last_sample_time_us;
	uint8_t synthetic_candidate_count;
};

LABS_EXPORT LabsErrorCode labs_ffmpeg_decoder_init(LabsFfmpegDecoder *decoder, LabsLog *log,
		LabsCodec codec, unsigned int max_fps, const char *hw_decoder_name, AVBufferRef *hw_device_ctx,
		LabsFfmpegFrameAvailable frame_available_cb, void *frame_available_cb_user);
LABS_EXPORT void labs_ffmpeg_decoder_fini(LabsFfmpegDecoder *decoder);
LABS_EXPORT bool labs_ffmpeg_decoder_video_sample_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user);
LABS_EXPORT LabsFfmpegFrame labs_ffmpeg_decoder_pull_frame(LabsFfmpegDecoder *decoder, int32_t *frames_lost);
LABS_EXPORT enum AVPixelFormat labs_ffmpeg_decoder_get_pixel_format(LabsFfmpegDecoder *decoder);

/**
 * Compute the wall-clock pts (seconds) and frame duration (seconds) from raw
 * AVFrame timestamp fields and codec context timing metadata.  Exposed for
 * unit testing; normally called internally by labs_ffmpeg_decoder_pull_frame.
 *
 * Fallback chain for time_base: pkt_timebase → ctx_timebase → {1,1000000}.
 * Fallback chain for pts:       best_effort_timestamp → pts → 0.
 * Fallback for fps:             framerate or 60.0.
 */
LABS_EXPORT void labs_ffmpeg_frame_get_timing(
	AVFrame *frame,
	AVRational pkt_timebase,
	AVRational ctx_timebase,
	AVRational framerate,
	double *out_pts,
	double *out_duration);

#ifdef __cplusplus
}
#endif

#endif // LABS_FFMPEG_DECODER_H
