#include <labs/remote/rudp.h>
#include <labs/random.h>
#include <labs/thread.h>
#include <labs/remote/rudpsendbuffer.h>

#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(__SWITCH__)
#include <sys/socket.h>
#endif
#ifdef _MSC_VER
#include <malloc.h>
#endif

#define RUDP_CONSTANT 0x244F244F
#define RUDP_SEND_BUFFER_SIZE 16
#define RUDP_EXPECT_TIMEOUT_MS 1000
typedef struct rudp_t
{
    uint16_t counter;
    uint32_t header;
    LabsMutex counter_mutex;
    LabsStopPipe stop_pipe;
    labs_socket_t sock;
    LabsLog *log;
    LabsRudpSendBuffer send_buffer;
} RudpInstance;

static uint16_t get_then_increase_counter(RudpInstance *rudp);
static LabsErrorCode labs_rudp_message_parse(uint8_t *serialized_msg, size_t msg_size, RudpMessage *message);
static void rudp_message_serialize(RudpMessage *message, uint8_t *serialized_msg, size_t *msg_size);
static void print_rudp_message_type(RudpInstance *rudp, RudpPacketType type);
static bool assign_submessage_to_message(RudpMessage *message);


LABS_EXPORT RudpInstance *labs_rudp_init(labs_socket_t *sock, LabsLog *log)
{
    RudpInstance *rudp = (RudpInstance *)calloc(1, sizeof(RudpInstance));
    if(!rudp)
        return NULL;
    rudp->log = log;
    LabsErrorCode err;
    err = labs_mutex_init(&rudp->counter_mutex, false);
    if(err != LABS_ERR_SUCCESS)
    {
        LABS_LOGE(rudp->log, "Rudp failed initializing, failed creating counter mutex");
        goto cleanup_rudp;
    }
    err = labs_stop_pipe_init(&rudp->stop_pipe);
    if(err != LABS_ERR_SUCCESS)
    {
        LABS_LOGE(rudp->log, "Rudp failed initializing, failed creating stop pipe");
        goto cleanup_mutex;
    }
    labs_rudp_reset_counter_header(rudp);
    rudp->sock = *sock;
	// The send buffer size MUST be consistent with the acked seqnums array size in rudp_handle_message_ack()
    err = labs_rudp_send_buffer_init(&rudp->send_buffer, rudp, log, RUDP_SEND_BUFFER_SIZE);
    if(err != LABS_ERR_SUCCESS)
    {
        LABS_LOGE(rudp->log, "Rudp failed initializing, failed creating send buffer");
        goto cleanup_stop_pipe;
    }
    return rudp;

cleanup_stop_pipe:
    labs_stop_pipe_fini(&rudp->stop_pipe);
cleanup_mutex:
    err = labs_mutex_fini(&rudp->counter_mutex);
    if(err != LABS_ERR_SUCCESS)
        LABS_LOGE(rudp->log, "Rudp couldn't cleanup counter mutex!");
cleanup_rudp:
    free(rudp);
    return NULL;
}

LABS_EXPORT void labs_rudp_reset_counter_header(RudpInstance *rudp)
{
    labs_mutex_lock(&rudp->counter_mutex);
    rudp->counter = labs_random_32()%0x5E00 + 0x1FF;
    labs_mutex_unlock(&rudp->counter_mutex);
    rudp->header = labs_random_32() + 0x8000;
}

LABS_EXPORT LabsErrorCode labs_rudp_send_init_message(RudpInstance *rudp)
{
    RudpMessage message;
    uint16_t local_counter = get_then_increase_counter(rudp);
    message.type = INIT_REQUEST;
    message.subMessage = NULL;
    message.data_size = 14;
    uint8_t *data = (uint8_t *)_alloca(sizeof(uint8_t) * (message.data_size));
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        LABS_LOGE(rudp->log, "Error allocating memory for rudp message");
        return LABS_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    const uint8_t after_header[0x2] = { 0x05, 0x82 };
    const uint8_t after_counter[0x6] = { 0x0B, 0x01, 0x01, 0x00, 0x01, 0x00 };
    *(labs_unaligned_uint16_t *)(data) = htons(local_counter);
    memcpy(data + 2, after_counter, sizeof(after_counter));
    *(labs_unaligned_uint32_t *)(data + 8) = htonl(rudp->header);
    memcpy(data + 12, after_header, sizeof(after_header));
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    LabsErrorCode err = labs_rudp_send_raw(rudp, serialized_msg, msg_size);
    free(serialized_msg);
    return err;
}

LABS_EXPORT LabsErrorCode labs_rudp_send_cookie_message(RudpInstance *rudp, uint8_t *response_buf, size_t response_size)
{
    RudpMessage message;
    uint16_t local_counter = get_then_increase_counter(rudp);
    message.type = COOKIE_REQUEST;
    message.subMessage = NULL;
    message.data_size = 14 + response_size;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        LABS_LOGE(rudp->log, "Error allocating memory for rudp message");
        return LABS_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    uint8_t *data = (uint8_t *)_alloca(sizeof(uint8_t) * (message.data_size));
    const uint8_t after_header[0x2] = { 0x05, 0x82 };
    const uint8_t after_counter[0x6] = { 0x0B, 0x01, 0x01, 0x00, 0x01, 0x00 };
    *(labs_unaligned_uint16_t *)(data) = htons(local_counter);
    memcpy(data + 2, after_counter, sizeof(after_counter));
    *(labs_unaligned_uint32_t *)(data + 8) = htonl(rudp->header);
    memcpy(data + 12, after_header, sizeof(after_header));
    memcpy(data + 14, response_buf, response_size);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    LabsErrorCode err = labs_rudp_send_raw(rudp, serialized_msg, msg_size);
    free(serialized_msg);
    return err;
}

LABS_EXPORT LabsErrorCode labs_rudp_send_session_message(RudpInstance *rudp, uint16_t remote_counter, uint8_t *session_msg, size_t session_msg_size)
{
    RudpMessage subMessage;
    uint16_t local_counter = get_then_increase_counter(rudp);
    subMessage.type = CTRL_MESSAGE;
    subMessage.subMessage = NULL;
    subMessage.data_size = 2 + session_msg_size;
    subMessage.size = (0xC << 12) | (8 + subMessage.data_size);
    uint8_t *subdata = (uint8_t *)_alloca(sizeof(uint8_t) * (subMessage.data_size));
    *(labs_unaligned_uint16_t *)(subdata) = htons(local_counter);
    memcpy(subdata + 2, session_msg, session_msg_size);
    subMessage.data = subdata;

    RudpMessage message;
    message.type = SESSION_MESSAGE;
    message.subMessage = &subMessage;
    message.data_size = 4;
    size_t alloc_size = 8 + message.data_size + 8 + subMessage.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        LABS_LOGE(rudp->log, "Error allocating memory for rudp message");
        return LABS_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | (8 + message.data_size);
    uint8_t *data = (uint8_t *)_alloca(sizeof(uint8_t) * (message.data_size));
    *(labs_unaligned_uint16_t *)(data) = htons(local_counter);
    *(labs_unaligned_uint16_t *)(data + 2) = htons(remote_counter);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    LabsErrorCode err = labs_rudp_send_raw(rudp, serialized_msg, msg_size);
    free(serialized_msg);
    return err;
}

LABS_EXPORT LabsErrorCode labs_rudp_send_ack_message(RudpInstance *rudp, uint16_t remote_counter)
{
    RudpMessage message;
    uint16_t counter = rudp->counter;
    message.type = ACK;
    message.subMessage = NULL;
    message.data_size = 6;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        LABS_LOGE(rudp->log, "Error allocating memory for rudp message");
        return LABS_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    uint8_t *data = (uint8_t *)_alloca(sizeof(uint8_t) * (message.data_size));
    const uint8_t after_counters[0x2] = { 0x00, 0x92 };
    *(labs_unaligned_uint16_t *)(data) = htons(counter);
    *(labs_unaligned_uint16_t *)(data + 2) = htons(remote_counter);
    memcpy(data + 4, after_counters, sizeof(after_counters));
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    LabsErrorCode err = labs_rudp_send_raw(rudp, serialized_msg, msg_size);
    free(serialized_msg);
    return err;
}

LABS_EXPORT LabsErrorCode labs_rudp_send_ctrl_message(RudpInstance *rudp, uint8_t *ctrl_message, size_t ctrl_message_size)
{
    RudpMessage message;
    uint16_t counter = get_then_increase_counter(rudp);
    uint16_t counter_ack = rudp->counter;
    message.type = CTRL_MESSAGE;
    message.subMessage = NULL;
    message.data_size = 2 + ctrl_message_size;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        LABS_LOGE(rudp->log, "Error allocating memory for rudp message");
        return LABS_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    uint8_t *data = (uint8_t *)_alloca(sizeof(uint8_t) * (message.data_size));
    *(labs_unaligned_uint16_t *)(data) = htons(counter);
    memcpy(data + 2, ctrl_message, ctrl_message_size);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    LabsErrorCode err = labs_rudp_send_raw(rudp, serialized_msg, msg_size);
    if(err != LABS_ERR_SUCCESS)
    {
        free(serialized_msg);
        return err;
    }
    err = labs_rudp_send_buffer_push(&rudp->send_buffer, counter_ack, serialized_msg, msg_size);
    return err;
}

LABS_EXPORT LabsErrorCode labs_rudp_send_switch_to_stream_connection_message(RudpInstance *rudp)
{
    RudpMessage message;
    uint16_t counter = get_then_increase_counter(rudp);
    uint16_t counter_ack = rudp->counter;
    message.type = CTRL_MESSAGE;
    message.subMessage = NULL;
    message.data_size = 26;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        LABS_LOGE(rudp->log, "Error allocating memory for rudp message");
        return LABS_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    uint8_t *data = (uint8_t *)_alloca(sizeof(uint8_t) * (message.data_size));
    const size_t buf_size = 16;
    uint8_t *buf = (uint8_t *)_alloca(sizeof(uint8_t) * (buf_size));
    const uint8_t before_buf[8] = { 0x00, 0x00, 0x00, 0x10, 0x00, 0x0D, 0x00, 0x00 };
    labs_random_bytes_crypt(buf, buf_size);
    *(labs_unaligned_uint16_t *)(data) = htons(counter);
    memcpy(data + 2, before_buf, sizeof(before_buf));
    memcpy(data + 10, buf, buf_size);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    LabsErrorCode err = labs_rudp_send_raw(rudp, serialized_msg, msg_size);
    if(err != LABS_ERR_SUCCESS)
    {
        free(serialized_msg);
        return err;
    }
    err = labs_rudp_send_buffer_push(&rudp->send_buffer, counter_ack, serialized_msg, msg_size);
    return err;
}

/**
 * Serializes rudp message into byte array
 *
 * @param[in] message The rudp message to serialize
 * @param[out] serialized_msg The serialized message
 * @param[out] msg_size The size of the serialized message
 * 
*/
static void rudp_message_serialize(
    RudpMessage *message, uint8_t *serialized_msg, size_t *msg_size)
{
    *(labs_unaligned_uint16_t *)(serialized_msg) = htons(message->size);
    *(labs_unaligned_uint32_t *)(serialized_msg + 2) = htonl(RUDP_CONSTANT);
    *(labs_unaligned_uint16_t *)(serialized_msg + 6) = htons(message->type);
    memcpy(serialized_msg + 8, message->data, message->data_size);
    *msg_size += 8 + message->data_size;
    if(message->subMessage)
    {
        rudp_message_serialize(message->subMessage, serialized_msg + 8 + message->data_size, msg_size);
    }
}

/**
 * Parse serialized message into rudp message
 *
 * @param[in] serialized_msg The serialized message to transform to a rudp message
 * @param[in] msg_size The size of the serialized message
 * @param[out] RudpMessage The parsed rudp message
 * @return LABS_ERR_SUCCESS on sucess or error code on failure
 * 
*/
static LabsErrorCode labs_rudp_message_parse(
    uint8_t *serialized_msg, size_t msg_size, RudpMessage *message)
{
    LabsErrorCode err = LABS_ERR_SUCCESS;
    message->data = NULL;
    message->subMessage = NULL;
    message->subMessage_size = 0;
    message->data_size = 0;
    message->size = ntohs(*(labs_unaligned_uint16_t *)(serialized_msg));
    message->type = ntohs(*(labs_unaligned_uint16_t *)(serialized_msg + 6));
    message->subtype = serialized_msg[6] & 0xFF;
    // Eliminate 0xC before length (size of header + data but not submessage)
    serialized_msg[0] = serialized_msg[0] & 0x0F;
    message->remote_counter = 0;
    uint16_t length = ntohs(*(labs_unaligned_uint16_t *)(serialized_msg));
    int remaining = msg_size - 8;
    int data_size = 0;
    if(length > 8)
    {
        data_size = length - 8;
        if(remaining < data_size)
            data_size = remaining;
        message->data_size = data_size;
        message->data = malloc(message->data_size * sizeof(uint8_t));
        if(!message->data)
            return LABS_ERR_MEMORY;
        memcpy(message->data, serialized_msg + 8, data_size);
        if(data_size >= 2)
            message->remote_counter = ntohs(*(labs_unaligned_uint16_t *)(message->data)) + 1;
    }

    remaining = remaining - data_size;
    if (remaining >= 8)
    {
        message->subMessage = malloc(1 * sizeof(RudpMessage));
        if(!message->subMessage)
            return LABS_ERR_MEMORY;
        message->subMessage_size = remaining;
        err = labs_rudp_message_parse(serialized_msg + 8 + data_size, remaining, message->subMessage);
    }
    return err;
}

/**
 * Get current rudp local counter and then increase rudp local counter
 *
 * @param[in] rudp The rudp instance to use
 * @return The rudp counter before increasing
 * 
*/
static uint16_t get_then_increase_counter(RudpInstance *rudp)
{
    labs_mutex_lock(&rudp->counter_mutex);
    uint16_t tmp = rudp->counter;
    if(rudp->counter >= UINT16_MAX)
        rudp->counter = 0;
    else
        rudp->counter++;
    labs_mutex_unlock(&rudp->counter_mutex);
    return tmp;
}

LABS_EXPORT uint16_t labs_rudp_get_local_counter(RudpInstance *rudp)
{
    return rudp->counter;
}

LABS_EXPORT LabsErrorCode labs_rudp_send_raw(RudpInstance *rudp, uint8_t *buf, size_t buf_size)
{
    if(LABS_SOCKET_IS_INVALID(rudp->sock))
    {
        return LABS_ERR_DISCONNECTED;
    }
    LABS_LOGV(rudp->log, "Sending Message:");
    labs_log_hexdump(rudp->log, LABS_LOG_VERBOSE, buf, buf_size);
	int sent = send(rudp->sock, (LABS_SOCKET_BUF_TYPE) buf, buf_size, 0);
	if(sent < 0)
	{
		LABS_LOGE(rudp->log, "Rudp raw failed to send packet: " LABS_SOCKET_ERROR_FMT, LABS_SOCKET_ERROR_VALUE);
		return LABS_ERR_NETWORK;
	}
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_rudp_select_recv(RudpInstance *rudp, size_t buf_size,  RudpMessage *message)
{
    uint8_t *buf = (uint8_t *)_alloca(sizeof(uint8_t) * (buf_size));
	LabsErrorCode err = labs_stop_pipe_select_single(&rudp->stop_pipe, rudp->sock, false, RUDP_EXPECT_TIMEOUT_MS);
	if(err == LABS_ERR_TIMEOUT || err == LABS_ERR_CANCELED)
		return err;
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(rudp->log, "Rudp select failed: " LABS_SOCKET_ERROR_FMT, LABS_SOCKET_ERROR_VALUE);
		return err;
	}

	LABS_SSIZET_TYPE received_sz = recv(rudp->sock, (LABS_SOCKET_BUF_TYPE) buf, buf_size, 0);
	if(received_sz <= 8)
	{
		if(received_sz < 0)
			LABS_LOGE(rudp->log, "Rudp recv failed: " LABS_SOCKET_ERROR_FMT, LABS_SOCKET_ERROR_VALUE);
		else
			LABS_LOGE(rudp->log, "Rudp recv returned less than the required 8 byte RUDP header");
		return LABS_ERR_NETWORK;
	}
    LABS_LOGV(rudp->log, "Receiving message:");
    labs_log_hexdump(rudp->log, LABS_LOG_VERBOSE, buf, received_sz);

    err = labs_rudp_message_parse(buf, received_sz, message);
    
	return err;
}

LABS_EXPORT LabsErrorCode labs_rudp_recv_only(RudpInstance *rudp, size_t buf_size,  RudpMessage *message)
{
    uint8_t *buf = (uint8_t *)_alloca(sizeof(uint8_t) * (buf_size));
	LABS_SSIZET_TYPE received_sz = recv(rudp->sock, (LABS_SOCKET_BUF_TYPE) buf, buf_size, 0);
	if(received_sz <= 8)
	{
		if(received_sz < 0)
			LABS_LOGE(rudp->log, "Rudp recv failed: " LABS_SOCKET_ERROR_FMT, LABS_SOCKET_ERROR_VALUE);
		else
			LABS_LOGE(rudp->log, "Rudp recv returned less than the required 8 byte RUDP header");
		return LABS_ERR_NETWORK;
	}
    LABS_LOGV(rudp->log, "Receiving message:");
    labs_log_hexdump(rudp->log, LABS_LOG_VERBOSE, buf, received_sz);

    LabsErrorCode err = labs_rudp_message_parse(buf, received_sz, message);

	return err;
}

LABS_EXPORT LabsErrorCode labs_rudp_stop_pipe_select_single(RudpInstance *rudp, LabsStopPipe *stop_pipe, uint64_t timeout)
{
	LabsErrorCode err = labs_stop_pipe_select_single(stop_pipe, rudp->sock, false, timeout);
	if(err == LABS_ERR_TIMEOUT || err == LABS_ERR_CANCELED)
		return err;
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(rudp->log, "Rudp select failed: " LABS_SOCKET_ERROR_FMT, LABS_SOCKET_ERROR_VALUE);
		return err;
	}
    return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_rudp_send_recv(RudpInstance *rudp, RudpMessage *message, uint8_t *buf, size_t buf_size, uint16_t remote_counter, RudpPacketType send_type, RudpPacketType recv_type, size_t min_data_size, size_t tries)
{
    bool success = false;
    for(int i = 0; i < tries; i++)
    {
        switch(send_type)
        {
            case INIT_REQUEST:
                labs_rudp_send_init_message(rudp);
                break;
            case COOKIE_REQUEST:
                labs_rudp_send_cookie_message(rudp, buf, buf_size);
                break;
            case ACK:
                labs_rudp_send_ack_message(rudp, remote_counter);
                break;
            case SESSION_MESSAGE:
                labs_rudp_send_session_message(rudp, remote_counter, buf, buf_size);
                break;
            default:
                LABS_LOGE(rudp->log, "Selected RudpPacketType 0x%04x to send that is not supported by rudp send receive.", send_type);
                return LABS_ERR_INVALID_DATA;
        }
        LabsErrorCode err = labs_rudp_select_recv(rudp, 1500, message);
        if(err == LABS_ERR_TIMEOUT)
            continue;
        if(err != LABS_ERR_SUCCESS)
            return err;
        bool found = true;
        while(true)
        {
            switch(recv_type)
            {
                case INIT_RESPONSE:
                    if(message->subtype != 0xD0)
                    {
                        if(assign_submessage_to_message(message))
                            continue;
                        LABS_LOGE(rudp->log, "Expected INIT RESPONSE with subtype 0xD0.\nReceived unexpected RUDP message ... retrying");
                        labs_rudp_print_message(rudp, message);
                        labs_rudp_message_pointers_free(message);
                        found = false;
                        break;
                    }
                    break;
                case COOKIE_RESPONSE:
                    if(message->subtype != 0xA0)
                    {
                        if(assign_submessage_to_message(message))
                            continue;
                        LABS_LOGE(rudp->log, "Expected COOKIE RESPONSE with subtype 0xA0.\nReceived unexpected RUDP message ... retrying");
                        labs_rudp_print_message(rudp, message);
                        labs_rudp_message_pointers_free(message);
                        found = false;
                        break;
                    }
                    break;
                case CTRL_MESSAGE:
                    if((message->subtype & 0x0F) != 0x2 && (message->subtype & 0x0F) != 0x6)
                    {
                        if(assign_submessage_to_message(message))
                            continue;
                        LABS_LOGE(rudp->log, "Expected CTRL MESSAGE with subtype 0x2 or 0x36.\nReceived unexpected RUDP message ... retrying");
                        labs_rudp_print_message(rudp, message);
                        labs_rudp_message_pointers_free(message);
                        found = false;
                        break;
                    }
                    break;
                case FINISH:
                    if(message->subtype != 0xC0)
                    {
                        if(assign_submessage_to_message(message))
                            continue;
                        LABS_LOGE(rudp->log, "Expected FINISH MESSAGE with subtype 0xC0 .\nReceived unexpected RUDP message ... retrying");
                        labs_rudp_print_message(rudp, message);
                        labs_rudp_message_pointers_free(message);
                        found = false;
                        break;
                    }
                    break;
                default:
                    LABS_LOGE(rudp->log, "Selected RudpPacketType 0x%04x to receive that is not supported by rudp send receive.", send_type);
                    labs_rudp_message_pointers_free(message);
                    return LABS_ERR_INVALID_DATA;
            }
            break;
        }
        if(!found)
            continue;
        if(message->data_size < min_data_size)
        {
            labs_rudp_message_pointers_free(message);
            LABS_LOGE(rudp->log, "Received message with too small of data size");
            continue;
        }
        success = true;
        break;
    }
    if(success)
        return LABS_ERR_SUCCESS;
    else
    {
        LABS_LOGE(rudp->log, "Could not receive correct RUDP message after %llu tries", tries);
        print_rudp_message_type(rudp, recv_type);
        return LABS_ERR_INVALID_RESPONSE;
    }
}

static bool assign_submessage_to_message(RudpMessage *message)
{
    if(message->subMessage)
    {
        if(message->data)
        {
            free(message->data);
            message->data = NULL;
        }
        RudpMessage *tmp = message->subMessage;
        memcpy(message, message->subMessage, sizeof(RudpMessage));
        free(tmp);
        return true;
    }
    return false;
}
LABS_EXPORT LabsErrorCode labs_rudp_ack_packet(RudpInstance *rudp, uint16_t counter_to_ack)
{
	LabsSeqNum16 acked_seq_nums[RUDP_SEND_BUFFER_SIZE];
	size_t acked_seq_nums_count = 0;
	LabsErrorCode err = labs_rudp_send_buffer_ack(&rudp->send_buffer, counter_to_ack, acked_seq_nums, &acked_seq_nums_count);
    return err;
}

LABS_EXPORT void labs_rudp_print_message(RudpInstance *rudp, RudpMessage *message)
{
    LABS_LOGI(rudp->log, "-------------RUDP MESSAGE------------");
    print_rudp_message_type(rudp, message->type);
    LABS_LOGI(rudp->log, "Rudp Message Subtype: 0x%02x", message->subtype);
    LABS_LOGI(rudp->log, "Rudp Message Size: %02x", message->size);
    LABS_LOGI(rudp->log, "Rudp Message Data Size: %zu", message->data_size);
    LABS_LOGI(rudp->log, "-----Rudp Message Data ---");
    if(message->data)
        labs_log_hexdump(rudp->log, LABS_LOG_INFO, message->data, message->data_size);
    LABS_LOGI(rudp->log, "Rudp Message Remote Counter: %u", message->remote_counter);
    if(message->subMessage)
        labs_rudp_print_message(rudp, message->subMessage);
}

LABS_EXPORT void labs_rudp_message_pointers_free(RudpMessage *message)
{
    if(message->data)
    {
        free(message->data);
        message->data = NULL;
    }
    if(message->subMessage)
    {
        labs_rudp_message_pointers_free(message->subMessage);
        free(message->subMessage);
        message->subMessage = NULL;
    }
}

LABS_EXPORT LabsErrorCode labs_rudp_fini(RudpInstance *rudp)
{
    LabsErrorCode err = LABS_ERR_SUCCESS;
    if(rudp)
    {
        labs_rudp_send_buffer_fini(&rudp->send_buffer);
        if (!LABS_SOCKET_IS_INVALID(rudp->sock))
        {
            LABS_SOCKET_CLOSE(rudp->sock);
            rudp->sock = LABS_INVALID_SOCKET;
        }
        err = labs_mutex_fini(&rudp->counter_mutex);
        labs_stop_pipe_fini(&rudp->stop_pipe);
        free(rudp);
    }
    return err;
}

/**
 * Prints a given rudp message type
 *
 * @param[in] rudp The rudp instance to use
 * @return type The type of packet to print
 *
*/
static void print_rudp_message_type(RudpInstance *rudp, RudpPacketType type)
{
    switch(type)
    {
        case INIT_REQUEST:
            LABS_LOGI(rudp->log, "Message Type: Init Request");
            break;
        case INIT_RESPONSE:
            LABS_LOGI(rudp->log, "Message Type: Init Response");
            break;
        case COOKIE_REQUEST:
            LABS_LOGI(rudp->log, "Message Type: Cookie Request");
            break;
        case COOKIE_RESPONSE:
            LABS_LOGI(rudp->log, "Message Type: Cookie Response");
            break;
        case SESSION_MESSAGE:
            LABS_LOGI(rudp->log, "Message Type: Session Message");
            break;
        case STREAM_CONNECTION_SWITCH_ACK:
            LABS_LOGI(rudp->log, "Message Type: Takion Switch Ack");
            break;
        case ACK:
            LABS_LOGI(rudp->log, "Message Type: Ack");
            break;
        case CTRL_MESSAGE:
            LABS_LOGI(rudp->log, "Message Type: Ctrl Message");
            break;
        case UNKNOWN:
            LABS_LOGI(rudp->log, "Message Type: Unknown");
            break;
        case FINISH:
            LABS_LOGI(rudp->log, "Message Type: Finish");
            break;
        default:
            LABS_LOGI(rudp->log, "Unknown Message Type: %04x", type);
            break;      
    }
}

