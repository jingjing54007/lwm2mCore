/**
 * @file lwm2mcoreSession.c
 *
 * LWM2M core file for session management
 *
 *
 * Copyright (C) Sierra Wireless Inc.
 *
 */

/* include files */
#include <lwm2mcore/lwm2mcore.h>
#include <lwm2mcore/security.h>
#include <lwm2mcore/coapHandlers.h>
#include <lwm2mcore/timer.h>
#include <lwm2mcore/udp.h>
#include "liblwm2m.h"
#include "internals.h"
#include "objects.h"
#include "dtlsConnection.h"
#include "sessionManager.h"

//--------------------------------------------------------------------------------------------------
/**
 *  Context
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_context_t* Lwm2mcoreCtxPtr = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Socket configuration variable
 */
//--------------------------------------------------------------------------------------------------
static  lwm2mcore_SocketConfig_t SocketConfig;

//--------------------------------------------------------------------------------------------------
/**
 *  Context
 */
//--------------------------------------------------------------------------------------------------
static ClientData_t* DataCtxPtr;

//--------------------------------------------------------------------------------------------------
/**
 *  Callback for events
 */
//--------------------------------------------------------------------------------------------------
static lwm2mcore_StatusCb_t StatusCb = NULL;

//--------------------------------------------------------------------------------------------------
/**
 *  Static boolean for bootstrap notification
 */
//--------------------------------------------------------------------------------------------------
static bool BootstrapSession = false;

//--------------------------------------------------------------------------------------------------
/**
 *  Client state bootstrapping / registered etc.,
 */
//--------------------------------------------------------------------------------------------------
#ifdef LWM2M_BOOTSTRAP
static lwm2m_client_state_t PreviousState;
#endif

//--------------------------------------------------------------------------------------------------
/**
 *                      PRIVATE FUNCTIONS
 */
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/** initialize the lwm2m context object
 *
 * @return
 *  - pointer to lwm2m context object
 */
//--------------------------------------------------------------------------------------------------
static lwm2mcore_context_t* InitContext
(
    ClientData_t* dataPtr,         ///< [IN] Context
    lwm2m_endpoint_type_t epType   ///< [IN] Lwm2m endpoint type, e.g. server/client.
)
{
    dataPtr->lwm2mcoreCtxPtr = (lwm2mcore_context_t*)lwm2m_malloc(sizeof(lwm2mcore_context_t));
    LWM2MCORE_ASSERT(dataPtr->lwm2mcoreCtxPtr);
    memset(dataPtr->lwm2mcoreCtxPtr, 0, sizeof(lwm2mcore_context_t));
    return dataPtr->lwm2mcoreCtxPtr;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function called by the LWM2M core to initiate a connection to a server
 *
 * @return
 *  - dtls_connection_t structure adress on success
 *  - NULL on failure
 */
//--------------------------------------------------------------------------------------------------
void* lwm2m_connect_server
(
    uint16_t secObjInstID,      ///< [IN] Security object instance ID
    void * userDataPtr          ///< [IN] User data
)
{
    ClientData_t* dataPtr;
    lwm2m_list_t* instancePtr;
    dtls_connection_t* newConnPtr = NULL;
    dataPtr = (ClientData_t*)userDataPtr;

    if (NULL != dataPtr)
    {
        lwm2m_object_t  * securityObj = dataPtr->securityObjPtr;

        instancePtr = LWM2M_LIST_FIND(dataPtr->securityObjPtr->instanceList, secObjInstID);
        if (NULL == instancePtr)
        {
            return NULL;
        }

        newConnPtr = connection_create(dataPtr->connListPtr,
                                       dataPtr->sock,
                                       securityObj,
                                       instancePtr->id,
                                       dataPtr->lwm2mHPtr,
                                       dataPtr->addressFamily);
        if (NULL == newConnPtr)
        {
            LOG("Connection creation failed");
            return NULL;
        }
        dataPtr->connListPtr = newConnPtr;
    }

    return (void *)newConnPtr;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function called by the LWM2M core to close a connection from a server
 */
//--------------------------------------------------------------------------------------------------
void lwm2m_close_connection
(
    void* sessionHPtr,     ///< [IN] Connection context
    void* userDataPtr      ///< [IN] Context
)
{
    ClientData_t* appDataPtr;
    dtls_connection_t* targetPtr;

    appDataPtr = (ClientData_t*)userDataPtr;
    targetPtr = (dtls_connection_t*)sessionHPtr;

    if ((NULL != appDataPtr) && (NULL != targetPtr))
    {
        if (targetPtr == appDataPtr->connListPtr)
        {
            appDataPtr->connListPtr = targetPtr->nextPtr;
            lwm2m_free(targetPtr);
        }
        else
        {
            dtls_connection_t* parentPtr;
            parentPtr = appDataPtr->connListPtr;
            while ((parentPtr != NULL) && (parentPtr->nextPtr != targetPtr))
            {
                parentPtr = parentPtr->nextPtr;
            }
            if (NULL != parentPtr)
            {
                parentPtr->nextPtr = targetPtr->nextPtr;
                lwm2m_free(targetPtr);
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Update bootstrap state and backup security object if bootstrap succeeded.
 */
//--------------------------------------------------------------------------------------------------
static void UpdateBootstrapInfo
(
    lwm2m_client_state_t* previousBsStatePtr,   ///< [IN] Bootstrap state
    lwm2m_context_t* contextPtr                 ///< [IN] Context
)
{
    static bool bootstrapDone = false;

    if (*previousBsStatePtr != contextPtr->state)
    {
        *previousBsStatePtr = contextPtr->state;
        switch(contextPtr->state)
        {
            case STATE_BOOTSTRAPPING:
            {
                bootstrapDone = true;
            }
            break;

            // if we go through bootstrap and registration succeeds, backup security object.
            case STATE_READY:
            {
                if (bootstrapDone)
                {
                    LOG("Backup security object.");
                    //TODO objSecurity_Backup(&context->objectList[0]);
                }
            }
            break;

            default:
            {
            }
            break;
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 *  lwm2m client step that handles data transmit.
 */
//--------------------------------------------------------------------------------------------------
static void Lwm2mClientStepHandler
(
    void* timerRef    ///< Timer that expired
)
{
    int result = 0;

    static struct timeval tv;
    tv.tv_sec = 60;
    tv.tv_usec = 0;

    LOG("Entering");

    /* This function does two things:
     * - first it does the work needed by liblwm2m (eg. (re)sending some packets).
     * - Secondly it adjusts the timeout value (default 60s) depending on the state of the
     *   transaction
     *   (eg. retransmission) and the time between the next operation
     */

    result = lwm2m_step(DataCtxPtr->lwm2mHPtr, &(tv.tv_sec));
    if (result != 0)
    {
       LOG_ARG("lwm2m_step() failed: 0x%X.", result);
#ifdef LWM2M_BOOTSTRAP
       if (STATE_BOOTSTRAPPING == PreviousState)
       {
#ifdef WITH_LOGS
           LOG("[BOOTSTRAP] restore security and server objects.");
#endif
           //prv_restore_objects(ClientCtxt.lwm2mH);
           DataCtxPtr->lwm2mHPtr->state = STATE_INITIAL;
       }
#endif
    }

    /* Maunch timer step */
    if (false == lwm2mcore_TimerSet(LWM2MCORE_TIMER_STEP, tv.tv_sec, NULL))
    {
        LOG("ERROR to launch the step timer");
    }

    UpdateBootstrapInfo(&PreviousState, DataCtxPtr->lwm2mHPtr);

    LOG("lwm2m step completed.");
}

//--------------------------------------------------------------------------------------------------
/**
 *  Convert coap response code to lwm2m standard error codes
 */
//--------------------------------------------------------------------------------------------------
static uint32_t ConvertToCoapCode
(
    CoapResponseCode_t response
)
{
    uint32_t coapCode;

    switch(response)
    {
        case COAP_RESOURCE_CHANGED:
        {
           coapCode = CHANGED_2_04;
           break;
        }
        case COAP_CONTENT_AVAILABLE:
        {
            coapCode =  CONTENT_2_05;
            break;
        }
        case COAP_BAD_REQUEST:
        {
            coapCode = BAD_REQUEST_4_00;
            break;
        }
        case COAP_METHOD_UNAUTHORIZED:
        {
            coapCode = UNAUTHORIZED_4_01;
            break;
        }
        case COAP_NOT_FOUND:
        {
            coapCode = NOT_FOUND_4_04;
            break;
        }
        case COAP_METHOD_NOT_ALLOWED:
        {
            coapCode = METHOD_NOT_ALLOWED_4_05;
            break;
        }
        case COAP_INTERNAL_ERROR:
        {
            coapCode = INTERNAL_SERVER_ERROR_5_00;
            break;
        }
        default:
            coapCode = INTERNAL_SERVER_ERROR_5_00;
    }

    return coapCode;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to send status event to the application, using the callback stored in the LWM2M core
 * session manager
 */
//--------------------------------------------------------------------------------------------------
void SendStatusEvent
(
    lwm2mcore_Status_t status
)
{
    // Check if a status callback is available
    if (!StatusCb)
    {
        LOG("No StatusCb to send status events");
        return;
    }

    // Send the status event notification
    StatusCb(status);
}

//--------------------------------------------------------------------------------------------------
/**
 * Function for session events
 */
//--------------------------------------------------------------------------------------------------
void SendSessionEvent
(
    SessionEventType_t eventId,         ///< [IN] Event Id
    SessionEventStatus_t eventstatus    ///< [IN] Event status
)
{
    lwm2mcore_Status_t status;

    switch (eventId)
    {
        case EVENT_TYPE_BOOTSTRAP:
        {
            switch (eventstatus)
            {
                case EVENT_STATUS_STARTED:
                {
                    LOG("BOOTSTRAP START");
                    BootstrapSession = true;
                }
                break;

                case EVENT_STATUS_DONE_SUCCESS:
                {
                    LOG("BOOTSTRAP DONE");
                    StoreCredentials();
                }
                break;

                case EVENT_STATUS_DONE_FAIL:
                {
                    LOG("BOOTSTRAP FAILURE");
                    status.event = LWM2MCORE_EVENT_SESSION_FAILED;
                    SendStatusEvent(status);
                }
                break;

                default:
                break;
            }
        }
        break;

        case EVENT_TYPE_REGISTRATION:
        {
            switch (eventstatus)
            {
                case EVENT_STATUS_STARTED:
                {
                    LOG("REGISTER START");
                }
                break;

                case EVENT_STATUS_DONE_SUCCESS:
                {
                    LOG("REGISTER DONE");

                    status.event = LWM2MCORE_EVENT_SESSION_STARTED;
                    SendStatusEvent(status);

                    status.event = LWM2MCORE_EVENT_LWM2M_SESSION_TYPE_START;
                    status.u.session.type = LWM2MCORE_SESSION_DEVICE_MANAGEMENT;
                    SendStatusEvent(status);

                    // Check if a download should be resumed
                    if (LWM2MCORE_ERR_COMPLETED_OK != lwm2mcore_ResumePackageDownload())
                    {
                        LOG("Error while checking download resume");
                    }
                }
                break;

                case EVENT_STATUS_DONE_FAIL:
                {
                    LOG("REGISTER FAILURE");
                    status.event = LWM2MCORE_EVENT_SESSION_FAILED;
                    SendStatusEvent(status);

                    /* Delete DM credentials in order to force a connection to the BS server */
                    LOG("DELETE DM CREDENTIALS");
                    lwm2mcore_DeleteCredential(LWM2MCORE_CREDENTIAL_DM_PUBLIC_KEY);
                    lwm2mcore_DeleteCredential(LWM2MCORE_CREDENTIAL_DM_SERVER_PUBLIC_KEY);
                    lwm2mcore_DeleteCredential(LWM2MCORE_CREDENTIAL_DM_SECRET_KEY);
                    lwm2mcore_DeleteCredential(LWM2MCORE_CREDENTIAL_DM_ADDRESS);
                }
                break;

                default:
                break;
            }
        }
        break;

        case EVENT_TYPE_REG_UPDATE:
        {
            switch (eventstatus)
            {
                case EVENT_STATUS_STARTED:
                {
                    LOG("REG UPDATE START");
                }
                break;

                case EVENT_STATUS_DONE_SUCCESS:
                {
                    LOG("REG UPDATE DONE");
                }
                break;

                case EVENT_STATUS_DONE_FAIL:
                {
                    LOG("REG UPDATE FAILURE");
                }
                break;

                default:
                break;
            }
        }
        break;

        case EVENT_TYPE_DEREG:
        {
            switch (eventstatus)
            {
                case EVENT_STATUS_STARTED:
                {
                    LOG("DEREGISTER START");
                }
                break;

                case EVENT_STATUS_DONE_SUCCESS:
                {
                    LOG("DEREGISTER DONE");
                }
                break;

                case EVENT_STATUS_DONE_FAIL:
                {
                    LOG("DEREGISTER FAILURE");
                }
                break;

                default:
                break;
            }
        }
        break;

        case EVENT_TYPE_AUTHENTICATION:
        {
            switch (eventstatus)
            {
                case EVENT_STATUS_STARTED:
                {
                    LOG ("AUTHENTICATION START");
                    status.event = LWM2MCORE_EVENT_AUTHENTICATION_STARTED;
                    SendStatusEvent(status);
                }
                break;

                case EVENT_STATUS_DONE_SUCCESS:
                {
                    LOG("AUTHENTICATION DONE");

                    if (BootstrapSession)
                    {
                        status.event = LWM2MCORE_EVENT_SESSION_STARTED;
                        SendStatusEvent(status);

                        status.event = LWM2MCORE_EVENT_LWM2M_SESSION_TYPE_START;
                        status.u.session.type = LWM2MCORE_SESSION_BOOTSTRAP;
                        SendStatusEvent(status);
                        BootstrapSession = false;
                    }
                }
                break;

                case EVENT_STATUS_DONE_FAIL:
                {
                    LOG("AUTHENTICATION FAILURE");
                    status.event = LWM2MCORE_EVENT_AUTHENTICATION_FAILED;
                    SendStatusEvent(status);
                }
                break;

                default:
                break;
            }
        }
        break;

        case EVENT_TYPE_RESUMING:
        {
            switch (eventstatus)
            {
                case EVENT_STATUS_STARTED:
                {
                    LOG("DTLS RESUME START");
                }
                break;

                case EVENT_STATUS_DONE_SUCCESS:
                {
                    LOG("DTLS RESUME DONE");
                }
                break;

                case EVENT_STATUS_DONE_FAIL:
                {
                    LOG("DTLS RESUME FAILURE");
                }
                break;

                default:
                break;
            }
        }
        break;

        case EVENT_SESSION:
        {
            switch (eventstatus)
            {
                case EVENT_STATUS_STARTED:
                {
                    LOG("SESSION START");
                }
                break;

                case EVENT_STATUS_DONE_SUCCESS:
                {
                    LOG("SESSION DONE");
                    BootstrapSession = false;
                    status.event = LWM2MCORE_EVENT_SESSION_FINISHED;
                    SendStatusEvent(status);
                }
                break;

                case EVENT_STATUS_DONE_FAIL:
                {
                    LOG("SESSION FAILURE");
                    BootstrapSession = false;
                    status.event = LWM2MCORE_EVENT_SESSION_FAILED;
                    SendStatusEvent(status);
                }
                break;

                default:
                break;
            }
        }
        break;

        default:
        {
            LOG_ARG("Bad event %d", eventId);
        }
        break;
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Callback called when the socked is opened
 */
//--------------------------------------------------------------------------------------------------
void lwm2mcore_UdpReceiveCb
(
    uint8_t* bufferPtr,                 ///< [IN] Received data
    uint32_t len,                       ///< [IN] Received data length
    struct sockaddr_storage *addrPtr,   ///< [INOUT] source address
    socklen_t addrLen,                  ///< @TODO
    lwm2mcore_SocketConfig_t config     ///< [IN] Socket config
)
{
    ClientData_t* dataPtr = (ClientData_t*)config.instanceRef;
    dtls_connection_t* connPtr;
    LOG("avc_udpCb");

    dataPtr->sock = config.sock;
    dataPtr->addressFamily = config.af;

    connPtr = connection_find(dataPtr->connListPtr, addrPtr, addrLen);
    if (NULL != connPtr)
    {
        // Let liblwm2m respond to the query depending on the context
        int result;
        LOG("Handle packet");
        result = connection_handlePacket(connPtr, bufferPtr, (size_t)len);
        if (0 != result)
        {
             LOG_ARG("Error handling message %d.",result);
        }
    }
    else
    {
        LOG("Received bytes ignored.");
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Private function to send an update message to the Device Management server
 *
 * @return
 *      - true if the treatment is launched
 *      - else false
 */
//--------------------------------------------------------------------------------------------------
bool UpdateRequest
(
    lwm2mcore_Ref_t instanceRef,    ///< [IN] instance reference
    bool withObjects                ///< [IN] indicates if supported object instance list needs to
                                    ///< be sent
)
{
    bool result = false;
    bool registered = false;
    ClientData_t* dataPtr = (ClientData_t*) instanceRef;

    if (NULL == dataPtr)
    {
        return false;
    }

    /* Check that the device is registered to DM server */
    if ((true == lwm2mcore_ConnectionGetType(instanceRef, &registered) && registered))
    {
        int iresult;
        /* Retrieve the serverID from list */
        lwm2m_server_t * targetPtr = dataPtr->lwm2mHPtr->serverList;
        if (NULL == targetPtr)
        {
            LOG("serverList is NULL");
            return false;
        }

        LOG_ARG("shortServerId %d", targetPtr->shortID);
        iresult = lwm2m_update_registration(dataPtr->lwm2mHPtr, targetPtr->shortID, withObjects);
        LOG_ARG("lwm2m_update_registration return %d", iresult);
        if (!iresult)
        {
            /* Stop the timer and launch it */
            if (false == lwm2mcore_TimerStop(LWM2MCORE_TIMER_STEP) )
            {
                LOG("Error to stop the step timer");
            }

            /* Launch the LWM2MCORE_TIMER_STEP timer with 1 second
               to treat the update request */
            if (false == lwm2mcore_TimerSet(LWM2MCORE_TIMER_STEP,
                                            1,
                                            Lwm2mClientStepHandler))
            {
                LOG("ERROR to launch the step timer for registration update");
            }
            else
            {
                result = true;
            }
        }
    }
    else
    {
        LOG("REG update is requested but the device is not registered");
    }
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 *                      PUBLIC FUNCTIONS
 */
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/**
 * Initialize the LWM2M core
 *
 * @return
 *  - instance reference
 *  - NULL in case of error
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_Ref_t lwm2mcore_Init
(
    lwm2mcore_StatusCb_t eventCb    ///< [IN] event callback
)
{
    ClientData_t* dataPtr = NULL;

    if (NULL == eventCb)
    {
        return NULL;
    }

    StatusCb = eventCb;
    dataPtr = (ClientData_t*)lwm2m_malloc(sizeof(ClientData_t));
    LWM2MCORE_ASSERT(dataPtr);
    memset(dataPtr, 0, sizeof(ClientData_t));

     /* Initialize LWM2M agent */
    dataPtr->lwm2mHPtr = lwm2m_init(dataPtr);
    LWM2MCORE_ASSERT(dataPtr->lwm2mHPtr);

    dataPtr->lwm2mcoreCtxPtr = InitContext(dataPtr, ENDPOINT_CLIENT);
    Lwm2mcoreCtxPtr = dataPtr->lwm2mcoreCtxPtr;
    LWM2MCORE_ASSERT(dataPtr->lwm2mcoreCtxPtr);

    DataCtxPtr = dataPtr;

    // Check if the update state/result should be changed after a FW install
    if (LWM2MCORE_ERR_COMPLETED_OK != lwm2mcore_GetFirmwareUpdateInstallResult())
    {
        LOG("Error while checking update state");
    }

    LOG_ARG("Init done -> context %p", dataPtr);
    return dataPtr;
}

//--------------------------------------------------------------------------------------------------
/**
 * Free the LWM2M core
 *
 */
//--------------------------------------------------------------------------------------------------
void lwm2mcore_Free
(
    lwm2mcore_Ref_t instanceRef     ///< [IN] instance reference
)
{
    ClientData_t* dataPtr = (ClientData_t*)instanceRef;

    if (NULL != dataPtr)
    {
        /* Free objects */
        ObjectsFree();

        if (NULL != dataPtr->lwm2mcoreCtxPtr)
        {
            lwm2m_free(dataPtr->lwm2mcoreCtxPtr);
        }

        if (NULL != dataPtr)
        {
            lwm2m_free(dataPtr);
            LOG("free dataPtr");
        }
        else
        {
            LOG("dataPtr NOT free");
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * LWM2M client entry point to initiate a connection
 *
 * @return
 *      - true if the treatment is launched
 *      - else false
 */
//--------------------------------------------------------------------------------------------------
bool lwm2mcore_Connect
(
    lwm2mcore_Ref_t instanceRef     ///< [IN] instance reference
)
{
    bool result = false;
    ClientData_t* dataPtr = (ClientData_t*)instanceRef;

    if (NULL == instanceRef)
    {
        return result;
    }

    /* Create the socket */
    memset(&SocketConfig, 0, sizeof (lwm2mcore_SocketConfig_t));
    result = lwm2mcore_UdpOpen(instanceRef, lwm2mcore_UdpReceiveCb, &SocketConfig);

    if (true == result)
    {
        LOG_ARG ("lwm2mcore_connect -> socket %d opened ", SocketConfig.sock);
        dataPtr->sock = SocketConfig.sock;
        dataPtr->addressFamily = SocketConfig.af;

        /* Initialize the lwm2m client step timer */
        DataCtxPtr = dataPtr;
        if (false == lwm2mcore_TimerSet(LWM2MCORE_TIMER_STEP, 1, Lwm2mClientStepHandler))
        {
            LOG("ERROR to launch the 1st step timer");
        }
        else
        {
            LOG("LWM2M Client started");
            result = true;
        }
    }
    else
    {
        LOG("ERROR on socket create");
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to send an update message to the Device Management server
 * This API can be used when the application wants to send a notification or during a firmware/app
 * update in order to be able to fully treat the scheduled update job
 *
 * @return
 *      - true if the treatment is launched
 *      - else false
 */
//--------------------------------------------------------------------------------------------------
bool lwm2mcore_Update
(
    lwm2mcore_Ref_t instanceRef     ///< [IN] instance reference
)
{
    return UpdateRequest(instanceRef, false);
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to close a connection
 *
 * @return
 *      - true if the treatment is launched
 *      - else false
 */
//--------------------------------------------------------------------------------------------------
bool lwm2mcore_Disconnect
(
    lwm2mcore_Ref_t instanceRef     ///< [IN] instance reference
)
{
    bool result = false;
    ClientData_t* dataPtr = (ClientData_t*) instanceRef;

    if (NULL == instanceRef)
    {
        return result;
    }

    /* Stop the current timers */
    if (false == lwm2mcore_TimerStop (LWM2MCORE_TIMER_STEP))
    {
        LOG("Error to stop the step timer");
    }

    /* Stop the agent */
    lwm2m_close(dataPtr->lwm2mHPtr);
    connection_free(dataPtr->connListPtr);
    dataPtr->lwm2mHPtr = NULL;
    dataPtr->connListPtr = NULL;

    /* Close the socket */
    result = lwm2mcore_UdpClose(SocketConfig);
    if (false == result)
    {
        LOG("ERROR in socket closure");
    }
    else
    {
        memset(&SocketConfig, 0, sizeof (lwm2mcore_SocketConfig_t));
        /* Notify that the connection is stopped */
        SendSessionEvent(EVENT_SESSION, EVENT_STATUS_DONE_SUCCESS);
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to know what is the current connection
 *
 * @return
 *      - true if the device is connected to any DM server
 *      - else false
 */
//--------------------------------------------------------------------------------------------------
bool lwm2mcore_ConnectionGetType
(
    lwm2mcore_Ref_t instanceRef,    ///< [IN] instance reference
    bool* isDeviceManagement        ///< [INOUT] Session type (false: bootstrap,
                                    ///< true: device management)
)
{
    bool result = false;
    ClientData_t* dataPtr = (ClientData_t*) instanceRef;

    if ((NULL == instanceRef) || (NULL == isDeviceManagement))
    {
        return result;
    }


    if ((dataPtr->lwm2mHPtr->state) >= STATE_REGISTER_REQUIRED )
    {
        *isDeviceManagement = true;
    }
    else
    {
        *isDeviceManagement = false;
    }
    result = true;
    LOG_ARG("state %d --> isDeviceManagement %d", dataPtr->lwm2mHPtr->state, *isDeviceManagement);

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to set the push callback handler
 */
//--------------------------------------------------------------------------------------------------
void lwm2mcore_SetPushCallback
(
    lwm2mcore_PushAckCallback_t callbackP  ///< [IN] push callback pointer
)
{
    lwm2m_set_push_callback(callbackP);
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to push data to lwm2mCore
 *
 * @return
 *      - LWM2MCORE_PUSH_INITIATED if data push transaction is initiated
 *      - LWM2MCORE_PUSH_FAILED if data push transaction failed
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_PushResult_t lwm2mcore_Push
(
    lwm2mcore_Ref_t instanceRef,            ///< [IN] instance reference
    uint8_t* payloadPtr,                    ///< [IN] payload
    size_t payloadLength,                   ///< [IN] payload length
    lwm2mcore_PushContent_t content,        ///< [IN] content type
    uint16_t* midPtr                        ///< [OUT] message id
)
{
    int rc;
    lwm2mcore_PushResult_t result = LWM2MCORE_PUSH_FAILED;
    lwm2m_media_type_t contentType;
    bool registered = false;
    ClientData_t* dataPtr = (ClientData_t*) instanceRef;

    if (NULL == instanceRef)
    {
        return result;
    }

    switch (content)
    {
        case LWM2MCORE_PUSH_CONTENT_CBOR:
            contentType = LWM2M_CONTENT_CBOR;
            break;
        case LWM2MCORE_PUSH_CONTENT_ZCBOR:
            contentType = LWM2M_CONTENT_ZCBOR;
            break;
        default:
            LOG_ARG("Invalid content type %d", content);
            return LWM2MCORE_PUSH_FAILED;
    }

    /* Check that the device is registered to DM server */
    if ((true == lwm2mcore_ConnectionGetType(instanceRef, &registered) && registered))
    {
        /* Retrieve the serverID from list */
        lwm2m_server_t * targetPtr = dataPtr->lwm2mHPtr->serverList;
        if (NULL == targetPtr)
        {
            LOG("serverList is NULL");
        }
        else
        {
            LOG_ARG("shortServerId %d", targetPtr->shortID);
            rc = lwm2m_data_push(dataPtr->lwm2mHPtr,
                                 targetPtr->shortID,
                                 payloadPtr,
                                 payloadLength,
                                 contentType,
                                 midPtr);

            if (rc == COAP_NO_ERROR)
            {
                result = LWM2MCORE_PUSH_INITIATED;
            }
            else if (rc == COAP_412_PRECONDITION_FAILED)
            {
                result = LWM2MCORE_PUSH_BUSY;
            }
            else
            {
                result = LWM2MCORE_PUSH_FAILED;
            }
        }
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to send an async response to server.
 *
 * @return
 *      - true if an async response is initiated
 *      - else false
 */
//--------------------------------------------------------------------------------------------------
bool lwm2mcore_SendAsyncResponse
(
    lwm2mcore_Ref_t instanceRef,                ///< [IN] instance reference
    lwm2mcore_CoapRequest_t* requestPtr,        ///< [IN] coap request refernce
    lwm2mcore_CoapResponse_t* responsePtr       ///< [IN] coap response
)
{
    bool result = false;
    bool registered = false;
    ClientData_t* dataPtr = (ClientData_t*) instanceRef;

    if (NULL == instanceRef)
    {
        return result;
    }

    /* Check that the device is registered to DM server */
    if ((true == lwm2mcore_ConnectionGetType(instanceRef, &registered) && registered))
    {
        /* Retrieve the serverID from list */
        lwm2m_server_t* targetPtr = dataPtr->lwm2mHPtr->serverList;
        if (NULL == targetPtr)
        {
            LOG("serverList is NULL");
            return false;
        }
        else
        {
            return lwm2m_async_response(dataPtr->lwm2mHPtr,
                                        targetPtr->shortID,
                                        requestPtr->messageId,
                                        ConvertToCoapCode(responsePtr->code),
                                        responsePtr->token,
                                        responsePtr->tokenLength,
                                        responsePtr->contentType,
                                        responsePtr->payload,
                                        responsePtr->payloadLength);
        }
    }
}
