// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/config.h>
#if LABS_LIB_ENABLE_OPUS

#include <labs/opusencoder.h>

#include <opus/opus.h>

#include <string.h>

LABS_EXPORT void labs_opus_encoder_init(LabsOpusEncoder *encoder, LabsLog *log)
{
	encoder->log = log;
	encoder->opus_encoder = NULL;
	memset(&encoder->audio_header, 0, sizeof(encoder->audio_header));
	encoder->audio_sender = NULL;
	encoder->opus_frame_buf = NULL;
	encoder->opus_frame_buf_size = 0;
}

LABS_EXPORT void labs_opus_encoder_fini(LabsOpusEncoder *encoder)
{
	free(encoder->opus_frame_buf);
	labs_audio_sender_free(encoder->audio_sender);
	if(encoder->opus_encoder)
		opus_encoder_destroy(encoder->opus_encoder);
}

LABS_EXPORT void labs_opus_encoder_header(LabsAudioHeader *header, LabsOpusEncoder *encoder, LabsSession *session)
{
	memcpy(&encoder->audio_header, header, sizeof(encoder->audio_header));

	opus_encoder_destroy(encoder->opus_encoder);
	encoder->opus_encoder = NULL;
	if(encoder->audio_sender)
	{
		labs_audio_sender_free(encoder->audio_sender);
		encoder->audio_sender = NULL;
	}
	int error;
	int application = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
	encoder->audio_sender = labs_audio_sender_new(encoder->log, session);

	if(!encoder->audio_sender)
	{
		LABS_LOGE(encoder->log, "Opus encoder failed to initialize Audio Sender");
		return;
	}

	encoder->opus_encoder = opus_encoder_create(header->rate, header->channels, application, &error);

	if(error != OPUS_OK)
	{
		LABS_LOGE(encoder->log, "LabsOpusEncoder failed to initialize opus encoder: %s", opus_strerror(error));
		labs_audio_sender_free(encoder->audio_sender);
		encoder->audio_sender = NULL;
		encoder->opus_encoder = NULL;
		return;
	}

	LABS_LOGI(encoder->log, "LabsOpusEncoder initialized");

	size_t opus_frame_buf_size_required = 40;
	uint8_t *opus_frame_buf_old = encoder->opus_frame_buf;
	if(!encoder->opus_frame_buf || encoder->opus_frame_buf_size != opus_frame_buf_size_required)
		encoder->opus_frame_buf = realloc(encoder->opus_frame_buf, opus_frame_buf_size_required);

	if(!encoder->opus_frame_buf)
	{
		free(opus_frame_buf_old);
		LABS_LOGE(encoder->log, "LabsOpusEncoder failed to alloc opus buffer");
		opus_encoder_destroy(encoder->opus_encoder);
		encoder->opus_encoder = NULL;
		labs_audio_sender_free(encoder->audio_sender);
		encoder->audio_sender = NULL;
		encoder->opus_frame_buf_size = 0;
		return;
	}

	encoder->opus_frame_buf_size = opus_frame_buf_size_required;
}

LABS_EXPORT void labs_opus_encoder_frame(int16_t *pcm_buf, LabsOpusEncoder *encoder)
{
	if(!encoder->opus_encoder)
	{
		LABS_LOGE(encoder->log, "Received audio frame, but opus encoder is not initialized");
		return;
	}

	int r = opus_encode(encoder->opus_encoder, pcm_buf, encoder->audio_header.frame_size, encoder->opus_frame_buf, encoder->opus_frame_buf_size);
	if(r < 1)
		LABS_LOGE(encoder->log, "Encoding audio frame with opus failed: %s", opus_strerror(r));
	else if((size_t)r != encoder->opus_frame_buf_size)
	{
		LABS_LOGV(encoder->log,
			"Encoded audio frame with unexpected size %d, expected %zu; dropping packet as protocol violation",
			r, encoder->opus_frame_buf_size);
	}
	else
	{
		labs_audio_sender_opus_data(encoder->audio_sender, encoder->opus_frame_buf, (size_t)r);
	}
}

#endif
