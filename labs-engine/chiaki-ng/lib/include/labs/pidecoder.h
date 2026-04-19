#ifndef LABS_PI_DECODER_H
#define LABS_PI_DECODER_H

#include <labs/config.h>
#include <labs/log.h>

#include <ilclient.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_pi_decoder_t
{
	LabsLog *log;
	TUNNEL_T tunnel[2];
	COMPONENT_T *components[3];
	ILCLIENT_T *client;
	COMPONENT_T *video_decode;
	COMPONENT_T *video_render;
	bool port_settings_changed;
	bool first_packet;
} LabsPiDecoder;

LABS_EXPORT LabsErrorCode labs_pi_decoder_init(LabsPiDecoder *decoder, LabsLog *log);
LABS_EXPORT void labs_pi_decoder_fini(LabsPiDecoder *decoder);
LABS_EXPORT void labs_pi_decoder_set_params(LabsPiDecoder *decoder, int x, int y, int w, int h, bool visible);
LABS_EXPORT bool labs_pi_decoder_video_sample_cb(uint8_t *buf, size_t buf_size, void *user);

#ifdef __cplusplus
}
#endif

#endif // LABS_PI_DECODER_H
