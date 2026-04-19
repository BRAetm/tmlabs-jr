/*
 * SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
 *
 * UDP Hole Punching Implementation
 * --------------------------------
 *
 * "Remote Play over Internet" uses a custom UDP-based protocol for communication between the
 * console and the client (see `rudp.h` for details on that). The protocol is designed to work
 * even if both the console and the client are behind NATs, by using UDP hole punching via
 * an intermediate server. The end result of the hole punching process is a pair of sockets,
 * one for control messages (using the custom protocol wrapper) and one for data
 * messages (using the same protocol as a local connection).
 *
 * The functions defined in this header should be used in this order:
 * 1. `labs_holepunch_list_devices` to get a list of devices that can be used for remote play
 * 2. `labs_holepunch_session_init` to initialize a session with a valid OAuth2 token
 * 3. `labs_holepunch_session_create` to create a remote play session on the PSN server
 * 4. `labs_holepunch_session_create_offer` to create our offer message to send to the console containing our network information for the control socket
 * 5. `labs_holepunch_session_start` to start the session for a specific device
 * 6. `labs_holepunch_session_punch_hole` called to prepare the control socket
 * 7. `labs_holepunch_session_create_offer` to create our offer message to send to the console containing our network information for the data socket
 * 8. `labs_holepunch_session_punch_hole` called to prepare the data socket
 * 9. `labs_holepunch_session_fini` once the streaming session has terminated.
 */

#ifndef LABS_HOLEPUNCH_H
#define LABS_HOLEPUNCH_H

#include "../common.h"
#include "../log.h"
#include "../random.h"
#include "../sock.h"

#include <stdint.h>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DUID_PREFIX "0000000700410080"

#define LABS_DUID_STR_SIZE 49

/** Handle to holepunching session state */
typedef struct session_t* LabsHolepunchSession;

/** Info for Remote Registration */
typedef struct holepunch_regist_info_t
{
    uint8_t data1[16];
    uint8_t data2[16];
    uint8_t custom_data1[16];
    char regist_local_ip[INET6_ADDRSTRLEN];
} LabsHolepunchRegistInfo;

/** Types of PlayStation consoles supported for Remote Play. */
typedef enum labs_holepunch_console_type_t
{
    LABS_HOLEPUNCH_CONSOLE_TYPE_PS4 = 0,
    LABS_HOLEPUNCH_CONSOLE_TYPE_PS5 = 1
} LabsHolepunchConsoleType;

/** Information about a device that can be used for remote play. */
typedef struct labs_holepunch_device_info_t
{
    LabsHolepunchConsoleType type;
    char device_name[32];
    uint8_t device_uid[32];
    bool remoteplay_enabled;
} LabsHolepunchDeviceInfo;

/** Port types used for remote play. */
typedef enum labs_holepunch_port_type_t
{
    LABS_HOLEPUNCH_PORT_TYPE_CTRL = 0,
    LABS_HOLEPUNCH_PORT_TYPE_DATA = 1
} LabsHolepunchPortType;


/**
 * List devices associated with a PSN account that can be used for remote play.
 *
 * @param[in] psn_oauth2_token Valid PSN OAuth2 token, must have at least the `psn:clientapp` scope
 * @param[in] console_type Type of console (PS4/PS5) to list devices for
 * @param[out] devices Pointer to an array of `LabsHolepunchDeviceInfo` structs, memory will be
 *                     allocated and must be freed with `labs_holepunch_free_device_list`
 * @param[out] device_count Number of devices in the array
 * @param[in] log logging instance to use
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
*/
LABS_EXPORT LabsErrorCode labs_holepunch_list_devices(
    const char* psn_oauth2_token,
    LabsHolepunchConsoleType console_type, LabsHolepunchDeviceInfo** devices,
    size_t* device_count, LabsLog *log);

/**
 * Free the memory allocated for a device list.
 *
 * @param[in] devices Pointer to a pointer to the array of `LabsHolepunchDeviceInfo` structs whose memory
 *                    should be freed
*/
LABS_EXPORT void labs_holepunch_free_device_list(LabsHolepunchDeviceInfo** devices);

/**
 * This function returns the data needed for regist from the LabsHolepunchSession
 *
 * This function should be called after the first labs_holepunch_session_punch_hole
 * punching the control hole used for regist
 *
 * @param[in] session Handle to the holepunching session
 * @return the LabsHolepunchRegistInfo for the session
*/
LABS_EXPORT LabsHolepunchRegistInfo labs_get_regist_info(LabsHolepunchSession session);

/**
 * This function returns the data needed for regist from the LabsHolepunchSession
 *
 * This function should be called after the first labs_holepunch_session_punch_hole
 * punching the control hole used for regist
 *
 * @param[in] session Handle to the holepunching session
 * @param ps_ip The char array to store the selected PlayStation IP
*/
LABS_EXPORT void labs_get_ps_selected_addr(LabsHolepunchSession session, char *ps_ip);

/**
 * This function returns the data needed for regist from the LabsHolepunchSession
 *
 * This function should be called after the first labs_holepunch_session_punch_hole
 * punching the control hole used for regist
 *
 * @param[in] session Handle to the holepunching session
 * @return The selected candidate's ctrl port
*/
LABS_EXPORT uint16_t labs_get_ps_ctrl_port(LabsHolepunchSession session);

/**
 * This function returns the sock created for the holepunch session based on the desired sock type
 *
 * This function should be called after labs_holepunch_session_punch_hole for the given sock.
 *
 * @param[in] session Handle to the holepunching session
 * @return the LabsHolepunchRegistInfo for the session
*/
LABS_EXPORT labs_socket_t *labs_get_holepunch_sock(LabsHolepunchSession session, LabsHolepunchPortType type);

/**
 * Get the STUN port allocation results that were computed while creating the hole punch session.
 *
 * @param[in] session Handle to the holepunching session
 * @param[out] allocation_increment Optional pointer to receive the allocation increment that was detected
 * @param[out] random_allocation Optional pointer to receive whether allocations appeared random
 * @return true if the session handle was valid and values were written, false otherwise
 */
LABS_EXPORT bool labs_holepunch_session_get_stun_allocation(
    LabsHolepunchSession session, int32_t *allocation_increment, bool *random_allocation);

/**
 * Generate a unique device identifier for the client.
 *
 * @param[out] out Buffer to write the identifier to, must be at least `LABS_DUID_STR_SIZE` bytes
 * @param[in,out] out_size Size of the buffer, must be initially set to the size of the
 *                         buffer, will be set to the number of bytes written
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
*/
LABS_EXPORT LabsErrorCode labs_holepunch_generate_client_device_uid(
    char *out, size_t *out_size);

/**
 * Initialize a holepunching session.
 *
 * **IMPORTANT**: The OAuth2 token must fulfill the following requirements:
 * - It must be a valid PSN OAuth2 token, ideally refreshed before calling this function
 *   (see `gui/include/psnaccountid.h`)
 * - It must be authorized for the following scopes:
 *   - `psn:clientapp`
 *   - `referenceDataService:countryConfig.read`
 *   - `pushNotification:webSocket.desktop.connect`
 *   - `sessionManager:remotePlaySession.system.update`
 * - It must have been initially created with a `duid` parameter set
 *   to a unique identifier for the client device (see `labs_holepunch_generate_client_duid`)
 *
 * @param[in] psn_oauth2_token PSN OAuth2 token to use for authentication, must comply with the
 *                             requirements listed above
 * @param[in] log logging instance to use
 * @return handle to the session state on success, otherwise NULL
*/
LABS_EXPORT LabsHolepunchSession labs_holepunch_session_init(
    const char* psn_oauth2_token, LabsLog *log);

/**
 * This forces port guessing to be used when port increment is 0. The session will advertise sequential port guesses as STUN
 * candidates and open multiple sockets to maximize the chance of port overlap
 * with the console's connection attempts. Useful for port-rewriting NATs
 * (e.g. mobile hotspots) where the single-socket approach fails.
 *
 * @param[in] session Handle to the holepunching session
 * @param[in] enabled Whether to enable port guessing
 */
LABS_EXPORT void labs_holepunch_session_force_port_guessing(
    LabsHolepunchSession session, bool enabled);

    /**
 *
 * Sets the number of ports to use for port guessing NAT traversal.
 *
 * @param[in] session Handle to the holepunching session
 * @param[in] count Number of port guesses to advertise (0 to keep default of 75)
 */
LABS_EXPORT void labs_holepunch_session_set_port_guessing_ports(
    LabsHolepunchSession session, int count);

/**
 * Set the number of sockets to open for port guessing NAT traversal.
 *
 * When using port guessing, this controls how many local UDP sockets
 * are opened to increase the chance of a NAT-assigned port matching one
 * of the advertised guesses. Default is 250.
 *
 * @param[in] session Handle to the holepunching session
 * @param[in] count Number of sockets to open (0 to keep default of 250)
 */
LABS_EXPORT void labs_holepunch_session_set_port_guessing_socks(
    LabsHolepunchSession session, int count);

/**
 * Create a remote play session on the PSN server.
 *
 * This function must be called after `labs_holepunch_session_init`.
 *
 * @param[in] session Handle to the holepunching session
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
*/
LABS_EXPORT LabsErrorCode labs_holepunch_session_create(
    LabsHolepunchSession session);

/**
 * Start a remote play session for a specific device.
 *
 * This function must be called after `labs_holepunch_session_create`.
 *
 * @param[in] session Handle to the holepunching session
 * @param[in] console_uid Unique identifier of the console to start the session for
 * @param[in] console_type Type of console to start the session for
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
 */
LABS_EXPORT LabsErrorCode labs_holepunch_session_start(
    LabsHolepunchSession session, const uint8_t* console_uid,
    LabsHolepunchConsoleType console_type);

/** Discovers UPNP if available
 * @param session The Session intance.
*/
LABS_EXPORT LabsErrorCode labs_holepunch_upnp_discover(LabsHolepunchSession session);

/** Creates an OFFER session message to send via PSN.
 *
 * @param session The Session instance.
 * @param type The type of offer message to create
 * @return LABS_ERR_SUCCESS on success, or an error code on failure.
 */
LABS_EXPORT LabsErrorCode holepunch_session_create_offer(LabsHolepunchSession session);

/**
 * Punch a hole in the NAT for the control or data socket.
 *
 * This function must be called twice, once for the control socket and once for the data socket,
 * precisely in that order.
 *
 * @param[in] session Handle to the holepunching session
 * @param[in] port_type Type of port to punch a hole for
 * @return LABS_ERR_SUCCESS on success, otherwise another error code
 */
LABS_EXPORT LabsErrorCode labs_holepunch_session_punch_hole(
    LabsHolepunchSession session, LabsHolepunchPortType port_type);

/**
 * Cancel initial psn connection steps (i.e., session create, session start and session punch hole)
 *
 * @param[in] session Handle to the holepunching session
 * @param[in] bool stop_thread Whether or not to stop the websocket thread
*/
LABS_EXPORT void labs_holepunch_main_thread_cancel(
    LabsHolepunchSession session, bool stop_thread);

/**
 * Finalize a holepunching session.
 *
 * **IMPORTANT**: This function should be called after the **streaming** session has terminated,
 * not after all sockets have been obtained.
 *
 * Will delete the session on the PSN server and free all resources associated with the session.
 *
 * @param[in] session Handle to the holepunching session
*/
LABS_EXPORT void labs_holepunch_session_fini(LabsHolepunchSession session);

#ifdef __cplusplus
}
#endif

#endif // LABS_HOLEPUNCH_H
