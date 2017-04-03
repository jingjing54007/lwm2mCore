/**
 * @file udp.c
 *
 * Adaptation layer for os socket
 *
 * Copyright (C) Sierra Wireless Inc.
 *
 */

#include <stdbool.h>
#include <lwm2mcore/lwm2mcore.h>
#include <lwm2mcore/udp.h>
#include <platform/inet.h>

//--------------------------------------------------------------------------------------------------
/**
 * Open a socket to the server
 * This function is called by the LWM2MCore and must be adapted to the platform
 * The aim of this function is to create a socket and fill the configPtr structure
 *
 * @return
 *      - true on success
 *      - false on error
 *
 */
//--------------------------------------------------------------------------------------------------
bool lwm2mcore_UdpOpen
(
    int context,                            ///< [IN] LWM2M context
    lwm2mcore_UdpCb_t callback,             ///< [IN] callback for data receipt
    lwm2mcore_SocketConfig_t* configPtr     ///< [INOUT] socket configuration
)
{
    bool result = false;

    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Close the socket
 * This function is called by the LWM2MCore and must be adapted to the platform
 * The aim of this function is to close a socket
 *
 * @return
 *      - true on success
 *      - false on error
 *
 */
//--------------------------------------------------------------------------------------------------
bool lwm2mcore_UdpClose
(
    lwm2mcore_SocketConfig_t config        ///< [INOUT] socket configuration
)
{
    bool result = false;
    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Send data on a socket
 * This function is called by the LWM2MCore and must be adapted to the platform
 * The aim of this function is to send data on a socket
 *
 * @return
 *      -
 *      - false on error
 *
 */
//--------------------------------------------------------------------------------------------------
ssize_t lwm2mcore_UdpSend
(
    int sockfd,
    const void *bufferPtr,
    size_t length,
    int flags,
    const struct sockaddr *dest_addrPtr,
    socklen_t addrlen
)
{
    return 0;
}

