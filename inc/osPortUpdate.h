/**
 * @file osPortUpdate.h
 *
 * Porting layer for firmware update
 *
 * Copyright (C) Sierra Wireless Inc.
 *
 */

#ifndef __OS_PORT_UPDATE_H__
#define __OS_PORT_UPDATE_H__

#include "lwm2mcore.h"

//--------------------------------------------------------------------------------------------------
/**
 * Define the maximum length for a package URI
 */
//--------------------------------------------------------------------------------------------------
#define LWM2MCORE_PACKAGE_URI_MAX_LEN               255

//--------------------------------------------------------------------------------------------------
/**
 * Define the maximum length for the software objects
 */
//--------------------------------------------------------------------------------------------------
#define LWM2MCORE_SW_OBJECT_INSTANCE_LIST_MAX_LEN   4032

//--------------------------------------------------------------------------------------------------
/**
 * Enumeration to indicates if an update is linked to a firmware update or a software update
 */
//--------------------------------------------------------------------------------------------------
typedef enum
{
    LWM2MCORE_FW_UPDATE_TYPE,    ///< Firmware update
    LWM2MCORE_SW_UPDATE_TYPE,    ///< Software update
    LWM2MCORE_MAX_UPDATE_TYPE    ///< Internal usage
}lwm2mcore_updateType_t;

//--------------------------------------------------------------------------------------------------
/**
 * Enumeration for firmware update state (object 5 (firmware update), resource 3)
 * These values are defined in the LWM2M specification
 */
//--------------------------------------------------------------------------------------------------
typedef enum
{
    LWM2MCORE_FW_UPDATE_STATE_WAITDOWNLOAD      = -1,   ///< A package download for FW update will
                                                        ///< be launched
    LWM2MCORE_FW_UPDATE_STATE_IDLE              = 0,    ///< FW update default state (LWM2M
                                                        ///< specification)
    LWM2MCORE_FW_UPDATE_STATE_DOWNLOADING       = 1,    ///< FW update downloading state (LWM2M
                                                        ///< specification)
    LWM2MCORE_FW_UPDATE_STATE_DOWNLOADED        = 2,    ///< FW update downloaded state (LWM2M
                                                        ///< specification)
    LWM2MCORE_FW_UPDATE_STATE_UPDATING          = 3,    ///< FW update updating state (LWM2M
                                                        ///< specification)
    LWM2MCORE_FW_UPDATE_STATE_WAITINSTALL       = 4,    ///< FW update: wait for install
    LWM2MCORE_FW_UPDATE_STATE_WAITINSTALLRESULT = 5     ///< FW update: install result
}lwm2mcore_fwUpdateState_t;

//--------------------------------------------------------------------------------------------------
/**
 * Enumeration for firmware update result (object 5 (firmware update), resource 5)
 * These values are defined in the LWM2M specification
 */
//--------------------------------------------------------------------------------------------------
typedef enum
{
    LWM2MCORE_FW_UPDATE_RESULT_DEFAULT_NORMAL       = 0,    ///< FW update default result
    LWM2MCORE_FW_UPDATE_RESULT_INSTALLED_SUCCESSFUL = 1,    ///< FW update result: success
    LWM2MCORE_FW_UPDATE_RESULT_NO_STORAGE_SPACE     = 2,    ///< FW update result: failure - not
                                                            ///< enough space
    LWM2MCORE_FW_UPDATE_RESULT_OUT_OF_MEMORY        = 3,    ///< FW update result: out of memory
    LWM2MCORE_FW_UPDATE_RESULT_COMMUNICATION_ERROR  = 4,    ///< FW update result: failure -
                                                            ///< communication error
    LWM2MCORE_FW_UPDATE_RESULT_VERIFY_ERROR         = 5,    ///< FW update result: failure - package
                                                            ///< check error
    LWM2MCORE_FW_UPDATE_RESULT_UNSUPPORTED_PKG_TYPE = 6,    ///< FW update result: failure -
                                                            ///< unsupported package
    LWM2MCORE_FW_UPDATE_RESULT_INVALID_URI          = 7,    ///< FW update result: failure - invalid
                                                            ///< URI
    LWM2MCORE_FW_UPDATE_RESULT_INSTALL_FAILURE      = 8,    ///< FW update result: failure - install
                                                            ///< failure
    LWM2MCORE_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL = 9,    ///< FW update result: failure -
                                                            ///< unsupported protocol
    /* Sierra defined UD_RESULT code */
    LWM2MCORE_FW_UPDATE_RESULT_CLIENT_CANCEL        = 0xF000 ///< internal usage
}lwm2mcore_fwUpdateResult_t;

//--------------------------------------------------------------------------------------------------
/**
 * Enumeration for software update state (object 9 (firmware update), resource 7)
 * These values are defined in the LWM2M specification
 */
//--------------------------------------------------------------------------------------------------
typedef enum
{
    LWM2MCORE_SW_UPDATE_STATE_INITIAL           = 0,    ///< Before downloading (LWM2M
                                                        ///< specification)
    LWM2MCORE_SW_UPDATE_STATE_DOWNLOAD_STARTED  = 1,    ///< The downloading process has started and
                                                        ///< is on-going (LWM2M specification)
    LWM2MCORE_SW_UPDATE_STATE_DOWNLOADED        = 2,    ///< The package has been completely
                                                        ///< downloaded (LWM2M specification)
    LWM2MCORE_SW_UPDATE_STATE_DELIVERED         = 3,    ///< In that state, the package has been
                                                        ///< correctly downloaded and is ready to be
                                                        ///< installed.(LWM2M specification)
    LWM2MCORE_SW_UPDATE_STATE_INSTALLED         = 4,    ///< In that state the software is correctly
                                                        ///< installed and can be activated or
                                                        ///< deactivated (LWM2M specification)
    LWM2MCORE_SW_UPDATE_STATE_WAITINSTALLRESULT = 5     ///< FW update: install result
}lwm2mcore_swUpdateState_t;

//--------------------------------------------------------------------------------------------------
/**
 * Enumeration for software update result (object 9 (firmware update), resource 9)
 * These values are defined in the LWM2M specification
 */
//--------------------------------------------------------------------------------------------------
typedef enum
{
    LWM2MCORE_SW_UPDATE_RESULT_INITIAL          = 0,    ///< Prior to download any new package in
                                                        ///< the Device, Update Result MUST be reset
                                                        ///< to this initial value
    LWM2MCORE_SW_UPDATE_RESULT_DOWNLOADING      = 1,    ///< Downloading. The package downloading
                                                        ///< process is on-going
    LWM2MCORE_SW_UPDATE_RESULT_INSTALLED        = 2,    ///< Software successfully installed
    LWM2MCORE_SW_UPDATE_RESULT_DOWNLOADED       = 3,    ///< Successfully Downloaded and package
                                                        ///< integrity verified
    LWM2MCORE_SW_UPDATE_RESULT_NOT_ENOUGH_MEMORY= 50,   ///< Not enough storage for the new software
                                                        ///< package
    LWM2MCORE_SW_UPDATE_RESULT_OUT_OF_MEMORY    = 51,   ///< Out of memory during downloading
                                                        ///< process
    LWM2MCORE_SW_UPDATE_RESULT_CONNECTION_LOST  = 52,   ///< Connection lost during downloading
                                                        ///< process
    LWM2MCORE_SW_UPDATE_RESULT_CHECK_FAILURE    = 53,   ///< Package integrity check failure
    LWM2MCORE_SW_UPDATE_RESULT_UNSUPPORTED_TYPE = 54,   ///< Unsupported package type
    LWM2MCORE_SW_UPDATE_RESULT_INVALID_URI      = 56,   ///< Invalid URI
    LWM2MCORE_SW_UPDATE_RESULT_DEVICE_ERROR     = 57,   ///< Device defined update error
    LWM2MCORE_SW_UPDATE_RESULT_INSTALL_FAILURE  = 58,   ///< Software installation failure
    LWM2MCORE_SW_UPDATE_RESULT_UNINSTALL_FAILURE= 59    ///< Uninstallation Failure
}lwm2mcore_swUpdateResult_t;

//--------------------------------------------------------------------------------------------------
/**
 * The server pushes a package to the LWM2M client
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdatePushPackage
(
    lwm2mcore_updateType_t type,    ///< [IN] Update type
    uint16_t instanceId,            ///< [IN] Intance Id (0 for FW, any value for SW)
    char* bufferPtr,                ///< [INOUT] data buffer
    size_t len                      ///< [IN] length of input buffer
);

//--------------------------------------------------------------------------------------------------
/**
 * The server sends a package URI to the LWM2M client
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateSetPackageUri
(
    lwm2mcore_updateType_t type,    ///< [IN] Update type
    uint16_t instanceId,            ///< [IN] Intance Id (0 for FW, any value for SW)
    char* bufferPtr,                ///< [INOUT] data buffer
    size_t len                      ///< [IN] length of input buffer
);

//--------------------------------------------------------------------------------------------------
/**
 * The server requires the current package URI stored in the LWM2M client
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateGetPackageUri
(
    lwm2mcore_updateType_t type,    ///< [IN] Update type
    uint16_t instanceId,            ///< [IN] Intance Id (0 for FW, any value for SW)
    char* bufferPtr,                ///< [INOUT] data buffer
    size_t* lenPtr                  ///< [INOUT] length of input buffer and length of the returned
                                    ///< data
);

//--------------------------------------------------------------------------------------------------
/**
 * The server requests to launch an update
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateLaunchUpdate
(
    lwm2mcore_updateType_t type,    ///< [IN] Update type
    uint16_t instanceId,            ///< [IN] Intance Id (0 for FW, any value for SW)
    char* bufferPtr,                ///< [INOUT] data buffer
    size_t len                      ///< [IN] length of input buffer
);

//--------------------------------------------------------------------------------------------------
/**
 * The server requires the update state
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateGetUpdateState
(
    lwm2mcore_updateType_t type,    ///< [IN] Update type
    uint16_t instanceId,            ///< [IN] Intance Id (0 for FW, any value for SW)
    uint8_t* updateStatePtr         ///< [OUT] update state
);

//--------------------------------------------------------------------------------------------------
/**
 * The server requires the update result
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateGetUpdateResult
(
    lwm2mcore_updateType_t type,    ///< [IN] Update type
    uint16_t instanceId,            ///< [IN] Intance Id (0 for FW, any value for SW)
    uint8_t* updateResultPtr        ///< [OUT] update result
);

//--------------------------------------------------------------------------------------------------
/**
 * The server requires the package name
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateGetPackageName
(
    lwm2mcore_updateType_t type,    ///< [IN] Update type
    uint16_t instanceId,            ///< [IN] Intance Id (0 for FW, any value for SW)
    char* bufferPtr,                ///< [INOUT] data buffer
    uint32_t len                    ///< [IN] length of input buffer
);

//--------------------------------------------------------------------------------------------------
/**
 * The server requires the package version
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateGetPackageVersion
(
    lwm2mcore_updateType_t type,    ///< [IN] Update type
    uint16_t instanceId,            ///< [IN] Intance Id (0 for FW, any value for SW)
    char* bufferPtr,                ///< [INOUT] data buffer
    uint32_t len                    ///< [IN] length of input buffer
);

//--------------------------------------------------------------------------------------------------
/**
 * The server sets the "update supported objects" field for software update
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateSetSwSupportedObjects
(
    uint16_t instanceId,            ///< [IN] Intance Id (any value for SW)
    bool value                      ///< [IN] Update supported objects field value
);

//--------------------------------------------------------------------------------------------------
/**
 * The server requires the "update supported objects" field for software update
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateGetSwSupportedObjects
(
    uint16_t instanceId,            ///< [IN] Intance Id (any value for SW)
    bool* valuePtr                  ///< [INOUT] Update supported objects field value
);

//--------------------------------------------------------------------------------------------------
/**
 * The server requires the activation state for one embedded application
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateGetSwActivationState
(
    uint16_t instanceId,            ///< [IN] Intance Id (any value for SW)
    bool* valuePtr                  ///< [INOUT] Activation state
);

//--------------------------------------------------------------------------------------------------
/**
 * The server requires an embedded application to be uninstalled (only for software update)
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateLaunchSwUninstall
(
    uint16_t instanceId,            ///< [IN] Intance Id (any value for SW)
    char* bufferPtr,                ///< [INOUT] data buffer
    size_t len                      ///< [IN] length of input buffer
);

//--------------------------------------------------------------------------------------------------
/**
 * The server requires an embedded application to be activated or deactivated (only for software
 * update)
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateActivateSoftware
(
    bool activation,        ///< [IN] Requested activation (true: activate, false: deactivate)
    uint16_t instanceId,    ///< [IN] Intance Id (any value for SW)
    char* bufferPtr,        ///< [INOUT] data buffer
    size_t len              ///< [IN] length of input buffer
);

//--------------------------------------------------------------------------------------------------
/**
 * The server request to create or delete an object instance of object 9
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treament succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portUpdateSoftwareInstance
(
    bool create,                ///<[IN] Create (true) or delete (false)
    uint16_t instanceId         ///<[IN] Object instance Id to create or delete
);
#endif /* __OS_PORT_UPDATE_H__ */

