// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_TAKION_H
#define LABS_TAKION_H

#include "common.h"
#include "thread.h"
#include "log.h"
#include "gkcrypt.h"
#include "seqnum.h"
#include "stoppipe.h"
#include "reorderqueue.h"
#include "feedback.h"
#include "takionsendbuffer.h"

#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif

typedef enum labs_takion_message_data_type_t {
	LABS_TAKION_MESSAGE_DATA_TYPE_PROTOBUF = 0,
	LABS_TAKION_MESSAGE_DATA_TYPE_RUMBLE = 7,
	LABS_TAKION_MESSAGE_DATA_TYPE_PAD_INFO = 9,
	LABS_TAKION_MESSAGE_DATA_TYPE_TRIGGER_EFFECTS = 11,
} LabsTakionMessageDataType;

typedef struct labs_takion_av_packet_t
{
	LabsSeqNum16 packet_index;
	LabsSeqNum16 frame_index;
	bool uses_nalu_info_structs;
	bool is_video;
	bool is_haptics;
	LabsSeqNum16 unit_index;
	uint16_t units_in_frame_total; // source + units_in_frame_fec
	uint16_t units_in_frame_fec;
	uint8_t codec;
	uint16_t word_at_0x18;
	uint8_t adaptive_stream_index;
	uint8_t byte_at_0x2c;

	uint64_t key_pos;

	uint8_t *data; // not owned
	size_t data_size;
} LabsTakionAVPacket;

static inline uint8_t labs_takion_av_packet_audio_unit_size(LabsTakionAVPacket *packet)				{ return packet->units_in_frame_fec >> 8; }
static inline uint8_t labs_takion_av_packet_audio_source_units_count(LabsTakionAVPacket *packet)	{ return packet->units_in_frame_fec & 0xf; }
static inline uint8_t labs_takion_av_packet_audio_fec_units_count(LabsTakionAVPacket *packet)		{ return (packet->units_in_frame_fec >> 4) & 0xf; }

typedef LabsErrorCode (*LabsTakionAVPacketParse)(LabsTakionAVPacket *packet, LabsKeyState *key_state, uint8_t *buf, size_t buf_size);

typedef struct labs_takion_congestion_packet_t
{
	uint16_t word_0;
	uint16_t received;
	uint16_t lost;
} LabsTakionCongestionPacket;


typedef enum {
	LABS_TAKION_EVENT_TYPE_CONNECTED,
	LABS_TAKION_EVENT_TYPE_DISCONNECT,
	LABS_TAKION_EVENT_TYPE_DATA,
	LABS_TAKION_EVENT_TYPE_DATA_ACK,
	LABS_TAKION_EVENT_TYPE_AV
} LabsTakionEventType;

typedef enum {
	LABS_NONE_DISABLED = 0,  //(bits: 00)
	LABS_AUDIO_DISABLED = 1, //(bits: 01)
	LABS_VIDEO_DISABLED = 2, //(bits: 10)
	LABS_AUDIO_VIDEO_DISABLED = 3 //(bits: 11)
} LabsDisableAudioVideo;

typedef struct labs_takion_event_t
{
	LabsTakionEventType type;
	union
	{
		struct
		{
			LabsTakionMessageDataType data_type;
			uint8_t *buf;
			size_t buf_size;
		} data;

		struct
		{
			LabsSeqNum32 seq_num;
		} data_ack;

		LabsTakionAVPacket *av;
	};
} LabsTakionEvent;

typedef void (*LabsTakionCallback)(LabsTakionEvent *event, void *user);

typedef struct labs_takion_connect_info_t
{
	LabsLog *log;
	struct sockaddr *sa;
	size_t sa_len;
	bool ip_dontfrag;
	LabsTakionCallback cb;
	void *cb_user;
	LabsDisableAudioVideo disable_audio_video;
	bool enable_crypt;
	bool enable_dualsense;
	uint8_t protocol_version;
	bool close_socket; // close socket when finishing takion
} LabsTakionConnectInfo;


typedef struct labs_takion_t
{
	LabsLog *log;
	uint8_t version;

	// Whether or not audio or video is disabled from further processing beyond basic ack
	LabsDisableAudioVideo disable_audio_video;
	/**
	 * Whether encryption should be used.
	 *
	 * If false, encryption and MACs are disabled completely.
	 *
	 * If true, encryption and MACs will be used depending on whether gkcrypt_local and gkcrypt_remote are non-null, respectively.
	 * However, if gkcrypt_remote is null, only control data packets are passed to the callback and all other packets are postponed until
	 * gkcrypt_remote is set, so it has been set, so eventually all MACs will be checked.
	 */
	bool enable_crypt;

	/**
	 * Array to be temporarily allocated when non-data packets come, enable_crypt is true, but gkcrypt_remote is NULL
	 * to not ignore any MACs in this period.
	 */
	struct labs_takion_postponed_packet_t *postponed_packets;
	size_t postponed_packets_size;
	size_t postponed_packets_count;

	LabsGKCrypt *gkcrypt_local; // if NULL (default), no gmac is calculated and nothing is encrypted
	uint64_t key_pos_local;
	LabsMutex gkcrypt_local_mutex;

	LabsGKCrypt *gkcrypt_remote; // if NULL (default), remote gmacs are IGNORED (!) and everything is expected to be unencrypted

	LabsReorderQueue data_queue;
	LabsReorderQueue video_queue;
	bool video_queue_initialized;
	int64_t video_queue_head_wait_start_us;
	uint64_t video_queue_head_wait_seq_num;
	LabsTakionSendBuffer send_buffer;

	LabsTakionCallback cb;
	void *cb_user;
	labs_socket_t sock;
	LabsThread thread;
	LabsStopPipe stop_pipe;
	uint32_t tag_local;
	uint32_t tag_remote;
	bool close_socket;

	LabsSeqNum32 seq_num_local;
	LabsMutex seq_num_local_mutex;

	/**
	 * Advertised Receiver Window Credit
	 */
	uint32_t a_rwnd;

	LabsTakionAVPacketParse av_packet_parse;

	LabsKeyState key_state;

	bool enable_dualsense;
} LabsTakion;


LABS_EXPORT LabsErrorCode labs_takion_connect(LabsTakion *takion, LabsTakionConnectInfo *info, labs_socket_t *sock);
LABS_EXPORT void labs_takion_close(LabsTakion *takion);

/**
 * Must be called from within the Takion thread, i.e. inside the callback!
 */
static inline void labs_takion_set_crypt(LabsTakion *takion, LabsGKCrypt *gkcrypt_local, LabsGKCrypt *gkcrypt_remote)
{
	takion->gkcrypt_local = gkcrypt_local;
	takion->gkcrypt_remote = gkcrypt_remote;
}

LABS_EXPORT LabsErrorCode labs_takion_packet_mac(LabsGKCrypt *crypt, uint8_t *buf, size_t buf_size, uint64_t key_pos, uint8_t *mac_out, uint8_t *mac_old_out);

/**
 * Get a new key pos and advance by data_size.
 *
 * Thread-safe while Takion is running.
 * @param key_pos pointer to write the new key pos to. will be 0 if encryption is disabled. Contents undefined on failure.
 */
LABS_EXPORT LabsErrorCode labs_takion_crypt_advance_key_pos(LabsTakion *takion, size_t data_size, uint64_t *key_pos);

/**
 * Send a datagram directly on the socket.
 *
 * Thread-safe while Takion is running.
 */
LABS_EXPORT LabsErrorCode labs_takion_send_raw(LabsTakion *takion, const uint8_t *buf, size_t buf_size);

/**
 * Calculate the MAC for the packet depending on the type derived from the first byte in buf,
 * assign MAC inside buf at the respective position and send the packet.
 *
 * If encryption is disabled, the MAC will be set to 0.
 */
LABS_EXPORT LabsErrorCode labs_takion_send(LabsTakion *takion, uint8_t *buf, size_t buf_size, uint64_t key_pos);

/**
 * Thread-safe while Takion is running.
 *
 * @param optional pointer to write the sequence number of the sent packet to
 */
LABS_EXPORT LabsErrorCode labs_takion_send_message_data(LabsTakion *takion, uint8_t chunk_flags, uint16_t channel, uint8_t *buf, size_t buf_size, LabsSeqNum32 *seq_num);

/**
 * Thread-safe while Takion is running.
 *
 * @param optional pointer to write the sequence number of the sent packet to
 */
LABS_EXPORT LabsErrorCode labs_takion_send_message_data_cont(LabsTakion *takion, uint8_t chunk_flags, uint16_t channel, uint8_t *buf, size_t buf_size, LabsSeqNum32 *seq_num);

/**
 * Thread-safe while Takion is running.
 */
LABS_EXPORT LabsErrorCode labs_takion_send_congestion(LabsTakion *takion, LabsTakionCongestionPacket *packet);

/**
 * Thread-safe while Takion is running.
 */
LABS_EXPORT LabsErrorCode labs_takion_send_feedback_state(LabsTakion *takion, LabsSeqNum16 seq_num, LabsFeedbackState *feedback_state);

LABS_EXPORT LabsErrorCode labs_takion_send_mic_packet(LabsTakion *takion, uint8_t *audio_packet, size_t packet_size, bool ps5);
/**
 * Thread-safe while Takion is running.
 */
LABS_EXPORT LabsErrorCode labs_takion_send_feedback_history(LabsTakion *takion, LabsSeqNum16 seq_num, uint8_t *payload, size_t payload_size);

#define LABS_TAKION_V9_AV_HEADER_SIZE_VIDEO 0x17
#define LABS_TAKION_V9_AV_HEADER_SIZE_AUDIO 0x12

LABS_EXPORT LabsErrorCode labs_takion_v9_av_packet_parse(LabsTakionAVPacket *packet, LabsKeyState *key_state, uint8_t *buf, size_t buf_size);

#define LABS_TAKION_V12_AV_HEADER_SIZE_VIDEO 0x17
#define LABS_TAKION_V12_AV_HEADER_SIZE_AUDIO 0x13

LABS_EXPORT LabsErrorCode labs_takion_v12_av_packet_parse(LabsTakionAVPacket *packet, LabsKeyState *key_state, uint8_t *buf, size_t buf_size);

#define LABS_TAKION_V7_AV_HEADER_SIZE_BASE					0x12
#define LABS_TAKION_V7_AV_HEADER_SIZE_VIDEO_ADD				0x3
#define LABS_TAKION_V7_AV_HEADER_SIZE_NALU_INFO_STRUCTS_ADD	0x3

LABS_EXPORT LabsErrorCode labs_takion_v7_av_packet_format_header(uint8_t *buf, size_t buf_size, size_t *header_size_out, LabsTakionAVPacket *packet);

LABS_EXPORT LabsErrorCode labs_takion_v7_av_packet_parse(LabsTakionAVPacket *packet, LabsKeyState *key_state, uint8_t *buf, size_t buf_size);

#define LABS_TAKION_CONGESTION_PACKET_SIZE 0xf

LABS_EXPORT void labs_takion_format_congestion(uint8_t *buf, LabsTakionCongestionPacket *packet, uint64_t key_pos);

#ifdef __cplusplus
}
#endif

#endif // LABS_TAKION_H
