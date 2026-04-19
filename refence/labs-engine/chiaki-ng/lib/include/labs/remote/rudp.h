/*
 * SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
 *
 * RUDP Implementation
 * --------------------------------
 *
 * "Remote Play over Internet" uses a custom UDP-based protocol named "SCE RUDP" for communication between the
 * console and the client for the portions that use TCP for a local connection.
 *
 * .
 */

#ifndef LABS_RUDP_H
#define LABS_RUDP_H

#include <stdint.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <labs/log.h>
#include <labs/common.h>
#include <labs/sock.h>
#include <labs/stoppipe.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Handle to rudp session state */
typedef struct rudp_t* LabsRudp;
typedef struct rudp_message_t RudpMessage;

typedef enum rudp_packet_type_t
{
    INIT_REQUEST = 0x8030,
    INIT_RESPONSE = 0xD000,
    COOKIE_REQUEST = 0x9030,
    COOKIE_RESPONSE = 0xA030,
    SESSION_MESSAGE = 0x2030,
    STREAM_CONNECTION_SWITCH_ACK = 0x242E,
    ACK = 0x2430,
    CTRL_MESSAGE = 0x0230,
    UNKNOWN = 0x022F,
    OFFSET8 = 0x1230,
    OFFSET10 = 0x2630,
    FINISH = 0xC000,
} RudpPacketType;

/** Rudp Message */
struct rudp_message_t
{
    uint8_t subtype;
    RudpPacketType type;
    uint16_t size;
    uint8_t *data;
    size_t data_size;
    uint16_t remote_counter;
    RudpMessage *subMessage;
    uint16_t subMessage_size;
};

/**
 * Create rudp instance
 *
 * @param[in] sock Pointer to a sock to use for Rudp messages
 * @param[in] log The LabsLog to use for log messages
 * @return The initialized rudp instance
*/
LABS_EXPORT LabsRudp labs_rudp_init(labs_socket_t *sock,
    LabsLog *log);

/**
 * Creates and sends an init rudp message for use when starting the session
 *
 * @param rudp Pointer to the Rudp instance to use for the message
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
*/

/**
 * Resets the counter and header of the rudp instance (used before init message is sent if rudp is already initialized)
 *
 * @param rudp Pointer to the Rudp instance of which to reset the counter and header
*/
LABS_EXPORT void labs_rudp_reset_counter_header(LabsRudp rudp);

LABS_EXPORT LabsErrorCode labs_rudp_send_init_message(
    LabsRudp rudp);

/**
 * Creates and sends a cookie rudp message for use after starting message
 *
 * @param rudp Pointer to the Rudp instance to use for the message
 * @param[in] response_buf The response from the init message
 * @param[in] response_size The size of the response from the init message
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
 * 
*/
LABS_EXPORT LabsErrorCode labs_rudp_send_cookie_message(LabsRudp rudp, uint8_t *response_buf, 
    size_t response_size);

/**
 * Creates and sends a session rudp message for use with registration message
 *
 * @param rudp Pointer to the Rudp instance to use for the message
 * @param[in] session_msg The data from the session msg (i.e., regist message)
 * @param[in] session_msg_size The size of the session message
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
 * 
*/
LABS_EXPORT LabsErrorCode labs_rudp_send_session_message(LabsRudp rudp, uint16_t remote_counter,
    uint8_t *session_msg, size_t session_msg_size);

/**
 * Creates and sends an ack rudp message for use in acking received rudp messages
 *
 * @param rudp Pointer to the Rudp instance to use for the message
 * @param[in] remote_counter The remote counter of the message to ack
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
*/
LABS_EXPORT LabsErrorCode labs_rudp_send_ack_message(LabsRudp rudp, uint16_t remote_counter);

/**
 * Creates and sends a ctrl rudp message for use with ctrl
 *
 * @param rudp Pointer to the Rudp indstance to use for the message
 * @param[in] ctrl_message The byte array of the ctrl message to send
 * @param[in] ctrl_message_size The size of the ctrl message to send
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
 * 
*/
LABS_EXPORT LabsErrorCode labs_rudp_send_ctrl_message(LabsRudp rudp, uint8_t *ctrl_message, 
    size_t ctrl_message_size);

/**
 * Creates and sends a switch to stream connection rudp message for use when switching from Senkusha to Stream Connection
 *
 * @param rudp Pointer to the Rudp instance to use for the message
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
*/
LABS_EXPORT LabsErrorCode labs_rudp_send_switch_to_stream_connection_message(LabsRudp rudp);

/**
 * Sends a raw byte array over the Rudp socket
 *
 * @param rudp Pointer to the Rudp instance containing the socket to send over
 * @param[in] buf The serialized message to send
 * @param[in] buf_size The size of the serialized message
 * 
*/
LABS_EXPORT LabsErrorCode labs_rudp_send_raw(LabsRudp rudp,
    uint8_t *buf, size_t buf_size);

/**
 * Sends a rudp message of a given type and checks for a given type to be returned.
 * Tries a given number of times before failing.
 * Used for initial rudp sequences.
 *
 * @param rudp Pointer to the rudp instance to use
 * @param message Pointer to the rudp message that will be filled in during the receive
 * @param[in] buf The buf to send as part of the data of the rudp message or NULL if not used
 * @param[in] buf_size The size of the sbuf or 0 if buf not used for the message type
 * @param[in] remote_counter The remote counter of the message this message is being sent in response to or 0 if not used for the message type
 * @param[in] send_type The RudpPacketType of the message to send
 * @param[in] recv_type The RudpPacketType of that we want to receive (success when received, otherwise failure)
 * @param[in] min_data_size The minimum size of the Rudp Message data for the response message to be considered valid
 * @param[in] tries The amount of times to try to receive a message of receive type in response to our sent message before giving up
 *
*/
LABS_EXPORT LabsErrorCode labs_rudp_send_recv(LabsRudp rudp,
    RudpMessage *message, uint8_t *buf, size_t buf_size,
    uint16_t remote_counter, RudpPacketType send_type,
    RudpPacketType recv_type, size_t min_data_size, size_t tries);

/**
 * Selects an incoming message from the queue and receives the rudp message
 *
 * @param rudp Pointer to the Rudp instance to use
 * @param[in] buf_size The size of the received message
 * @param[out] message The Rudp message returned
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
 *
*/
LABS_EXPORT LabsErrorCode labs_rudp_select_recv(LabsRudp rudp, size_t buf_size, RudpMessage *message);

/**
 * Receives the rudp message. Must use select separately from this function
 *
 * @param rudp Pointer to the Rudp instance to use
 * @param[in] buf_size The size of the received message
 * @param[out] message The Rudp message returned
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
 * 
*/
LABS_EXPORT LabsErrorCode labs_rudp_recv_only(LabsRudp rudp, size_t buf_size,  RudpMessage *message);

/**
 * Selects a rudp message using the given stop pipe and timeout
 *
 * @param rudp Pointer to the Rudp instance to use
 * @param[in] stop_pipe Pointer to the stop pipe to use
 * @param[in] timeout The timeout in ms
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
 *
*/
LABS_EXPORT LabsErrorCode labs_rudp_stop_pipe_select_single(LabsRudp rudp, LabsStopPipe *stop_pipe, uint64_t timeout);

/**
 * Frees rudp message pointers if present (data and subMessage)
 *
 * @param message Pointer to the Rudp message with the data to free
*/

/**
 * Acknowledge received ack for rudp packet
 *
 * @param rudp Pointer to the Rudp instance to use
 * @param[in] counter_to_ack The size of the received message
 * @param[out] message The Rudp message returned
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
 * 
*/

LABS_EXPORT LabsErrorCode labs_rudp_ack_packet(LabsRudp rudp, uint16_t counter_to_ack);

/**
 * Print Rudp Message
 *
 * @param rudp Pointer to the Rudp instance to use
 * @param[in] message Pointer to the message to print
 * 
*/
LABS_EXPORT void labs_rudp_print_message(LabsRudp rudp, RudpMessage *message);

LABS_EXPORT void labs_rudp_message_pointers_free(RudpMessage *message);

/**
 * Terminate rudp instance
 *
 * @param[in] rudp Pointer to the Rudp instance to be destroyed
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
*/
LABS_EXPORT LabsErrorCode labs_rudp_fini(LabsRudp rudp);

#ifdef __cplusplus
}
#endif

#endif // LABS_RUDP_H
