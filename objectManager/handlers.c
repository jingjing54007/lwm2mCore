/**
 * @file lwm2mcoreHandlers.c
 *
 * client of the LWM2M stack
 *
 * Copyright (C) Sierra Wireless Inc.
 *
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lwm2mcore/lwm2mcore.h>
#include <lwm2mcore/connectivity.h>
#include <lwm2mcore/device.h>
#include <lwm2mcore/security.h>
#include <lwm2mcore/update.h>
#include "handlers.h"
#include "objects.h"
#include "internals.h"
#include "crypto.h"

//--------------------------------------------------------------------------------------------------
/**
 * Lifetime value to indicate that the lifetime is deactivated
 * This is compliant with the LWM2M specification and a 0-value has no sense
 * 630720000 = 20 years
 * This is used if the customer does not wan any "automatic" connection to the server
 */
//--------------------------------------------------------------------------------------------------
#define LWM2MCORE_LIFETIME_VALUE_DISABLED       630720000

//--------------------------------------------------------------------------------------------------
/**
 * Enum for security mode for LWM2M connection (object 0 (security); resource 2)
 */
//--------------------------------------------------------------------------------------------------
typedef enum
{
    SEC_PSK = 0,          ///< PSK
    SEC_RAW_PK,           ///< Raw PSK
    SEC_CERTIFICATE,      ///< Certificate
    SEC_NONE,             ///< No security
    SEC_MODE_MAX          ///<internal use only
}SecurityMode_t;

//--------------------------------------------------------------------------------------------------
/**
 * Credential temporary RAM storage for BS and DM credentials: storage at the end of the bootstrap
 */
//--------------------------------------------------------------------------------------------------
static uint8_t  BsPskId[DTLS_PSK_MAX_CLIENT_IDENTITY_LEN];
static uint16_t BsPskIdLen = 0;
static uint8_t  BsPsk[DTLS_PSK_MAX_KEY_LEN];
static uint16_t BsPskLen = 0;
static uint8_t  BsAddr[LWM2MCORE_SERVER_URI_MAX_LEN];
static uint8_t  DmPskId[DTLS_PSK_MAX_CLIENT_IDENTITY_LEN];
static uint16_t DmPskIdLen = 0;
static uint8_t  DmPsk[DTLS_PSK_MAX_KEY_LEN];
static uint16_t DmPskLen = 0;
static uint8_t  DmAddr[LWM2MCORE_SERVER_URI_MAX_LEN];

//--------------------------------------------------------------------------------------------------
/**
 * Lifetime value (temporary value)
 */
//--------------------------------------------------------------------------------------------------
static uint32_t Lifetime = LWM2MCORE_LIFETIME_VALUE_DISABLED;

//--------------------------------------------------------------------------------------------------
/**
 * Convert to unsigned 16 bits integer to network bytes stream
 *
 * @return
 *  - converted size
 */
//--------------------------------------------------------------------------------------------------
static size_t FormatUint16ToBytes
(
    uint8_t* bytesPtr,      ///< [INOUT] the buffer contains data converted
    const uint16_t u        ///< [IN] the value to be converted
)
{
    bytesPtr[0] = (u >> 8) & 0xff;
    bytesPtr[1] = u & 0xff;
    return (sizeof(uint16_t));
}

//--------------------------------------------------------------------------------------------------
/**
 * Convert to unsigned 32 bits integer to network bytes stream
 *
 * @return
 *  - converted size
 */
//--------------------------------------------------------------------------------------------------
static size_t FormatUint32ToBytes
(
    uint8_t* bytesPtr,      ///< [INOUT] the buffer contains data converted
    const uint32_t u        ///< [IN] the value to be converted
)
{
    bytesPtr[0] = (u >> 24) & 0xff;
    bytesPtr[1] = (u >> 16) & 0xff;
    bytesPtr[2] = (u >> 8) & 0xff;
    bytesPtr[3] = u & 0xff;
    return (sizeof(uint32_t));
}

//--------------------------------------------------------------------------------------------------
/**
 * Convert to unsigned 64 bits integer to network bytes stream
 *
 * @return
 *  - converted size
 */
//--------------------------------------------------------------------------------------------------
static size_t FormatUint64ToBytes
(
    uint8_t* bytesPtr,      ///< [INOUT] the buffer contains data converted
    const uint64_t u        ///< [IN] the value to be converted
)
{
    FormatUint32ToBytes(bytesPtr, (u >> 32) & 0xffffffff);
    FormatUint32ToBytes(bytesPtr + 4, u & 0xffffffff);
    return (sizeof(uint64_t));
}

//--------------------------------------------------------------------------------------------------
/**
 * Function used by object resource API to write value in buffer
 *
 * @return
 *  - converted size
 */
//--------------------------------------------------------------------------------------------------
static size_t FormatValueToBytes
(
    uint8_t* bytesPtr,      ///< [INOUT] bytes buffer in which u value will be written
    void* u,                ///< [INOUT] Data to be written
    uint32_t size,          ///< [IN] length to be written
    bool bSignedValue       ///< [IN] Indicates if u shall be considered like a signed value
)
{
    size_t lReturn = 0;
    size_t updatedSize = size;

    if ((NULL != bytesPtr) && (NULL != u))
    {
        if (bSignedValue == false)
        {
            uint8_t* u8Value = 0;
            uint16_t* u16Value = 0;
            uint32_t* u32Value = 0;
            uint64_t* u64Value = 0;
            if (size == sizeof(uint8_t))
            {
                u8Value = (uint8_t*)u;
                if (*u8Value > 0x7F)
                {
                    /* Value shall be coded in 2 bytes */
                    u16Value = (uint16_t*)u;
                    updatedSize = sizeof(uint16_t);
                }
            }
            else if (size == sizeof(uint16_t))
            {
                u16Value = (uint16_t*)u;
                if (*u16Value > 0x7FFF)
                {
                    /* Value shall be coded in 4 bytes */
                    u32Value = (uint32_t*)u;
                    updatedSize = sizeof(uint32_t);
                }
                else if (*u16Value <= 0x7F)
                {
                    /* the value could be coded in 1 byte */
                    u8Value = (uint8_t*)u;
                    updatedSize = sizeof(uint8_t);
                }
            }
            else if (size == sizeof(uint32_t))
            {
                u32Value = (uint32_t*)u;
                if (*u32Value > 0x7FFFFFFF)
                {
                    /* Value shall be coded in 8 bytes */
                    u64Value = (uint64_t*)u;
                    updatedSize = sizeof(uint64_t);
                }
                else if (*u32Value <= 0x7F)
                {
                    /* the value could be coded in 1 byte */
                    u8Value = (uint8_t*)u;
                    updatedSize = sizeof(uint8_t);
                }
                else if (*u32Value <= 0x7FFF)
                {
                    /* the value could be coded in 2 bytes */
                    u16Value = (uint16_t*)u;
                    updatedSize = sizeof(uint16_t);
                }
            }
            else if (size == sizeof(uint64_t))
            {
                u64Value = (uint64_t*)u;
                if (*u64Value >> 63)
                {
                    updatedSize = 0;
                }
                else if (*u64Value <= 0x7F)
                {
                    /* the value could be coded in 1 byte */
                    u8Value = (uint8_t*)u;
                    updatedSize = sizeof(uint8_t);
                }
                else if (*u64Value <= 0x7FFF)
                {
                    /* the value could be coded in 2 bytes */
                    u16Value = (uint16_t*)u;
                    updatedSize = sizeof(uint16_t);
                }
                else if (*u64Value <= 0x7FFFFFFF)
                {
                    /* the value could be coded in 4 bytes */
                    u32Value = (uint32_t*)u;
                    updatedSize = sizeof(uint32_t);
                }
            }
            else
            {
                updatedSize = 0;
            }
        }

        switch (updatedSize)
        {
            case 1:
            {
                uint8_t* u8Value = (uint8_t*)u;
                lReturn = sizeof(uint8_t);
                bytesPtr[ 0 ] = *u8Value;
            }
            break;

            case 2:
            {
                uint16_t* u16Value = (uint16_t*)u;
                lReturn = FormatUint16ToBytes(bytesPtr, *u16Value);
            }
            break;

            case 4:
            {
                uint32_t* u32Value = (uint32_t*)u;
                lReturn = FormatUint32ToBytes(bytesPtr, *u32Value);
            }
            break;

            case 8:
            {
                uint64_t* u64Value = (uint64_t*)u;
                lReturn = FormatUint64ToBytes(bytesPtr, *u64Value);
            }
            break;

            default:
            {
                lReturn = -1;
            }
            break;
        }
    }

    return lReturn;
}

//--------------------------------------------------------------------------------------------------
/**
 *                                  OBJECT 0: SECURITY
 */
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/**
 * Function to write a resource of object 0
 * Object: 0 - Security
 * Resource: all
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int WriteSecurityObj
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t len                          ///< [IN] length of input buffer
)
{
    int sID;

    if ((NULL == uriPtr) || (NULL == bufferPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    /* Check that the server which tries to read/write is the bootstrap one
     * The Device Management server can not access to this resource
     */
    //TODO

    if ((uriPtr->op & LWM2MCORE_OP_WRITE) == 0)
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    /* Check that the object instance Id is in the correct range */
    if (uriPtr->oiid >= (LWM2MCORE_DM_SERVER_MAX_COUNT
                       + LWM2MCORE_BOOTSRAP_SERVER_MAX_COUNT))
    {
        return LWM2MCORE_ERR_INCORRECT_RANGE;
    }

    switch (uriPtr->rid)
    {
        /* Resource 0: LWM2M server URI */
        case LWM2MCORE_SECURITY_SERVER_URI_RID:
        {
            if (LWM2MCORE_BUFFER_MAX_LEN < len)
            {
                sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            }
            else
            {
                /* Write operation */
                if (LWM2MCORE_BS_SERVER_OIID == uriPtr->oiid)
                {
                    /* Bootstrap server */
                    memcpy(BsAddr, bufferPtr, len);
                    sID = LWM2MCORE_ERR_COMPLETED_OK;
                }
                else
                {
                    /* Device Management server */
                    memcpy(DmAddr, bufferPtr, len);
                    sID = LWM2MCORE_ERR_COMPLETED_OK;
                }
            }
        }
        break;

        /* Resource 1: Bootstrap server (true or false) */
        case LWM2MCORE_SECURITY_BOOTSTRAP_SERVER_RID:
        {
            if (LWM2MCORE_BS_SERVER_OIID == uriPtr->oiid)
            {
                /* Bootstrap server */
                sID = LWM2MCORE_ERR_COMPLETED_OK;
            }
            else
            {
                /* Device Management server */
                sID = LWM2MCORE_ERR_COMPLETED_OK;
            }
        }
        break;

        /* Resource 2: Security mode */
        case LWM2MCORE_SECURITY_MODE_RID:
        {
            /* Write operation */
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 3: Public key or identity */
        case LWM2MCORE_SECURITY_PKID_RID:
        {
            if (DTLS_PSK_MAX_CLIENT_IDENTITY_LEN < len)
            {
                sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            }
            else
            {
#ifdef CREDENTIALS_DEBUG
                lwm2mcore_DataDump("PSK ID write", bufferPtr, len);
#endif
                if (LWM2MCORE_BS_SERVER_OIID == uriPtr->oiid)
                {
                    /* Bootstrap server */
                    memcpy(BsPskId, bufferPtr, len);
                    BsPskIdLen = len;
                    sID = LWM2MCORE_ERR_COMPLETED_OK;
                }
                else
                {
                    /* Device Management server */
                    memcpy(DmPskId, bufferPtr, len);
                    DmPskIdLen = len;
                    sID = LWM2MCORE_ERR_COMPLETED_OK;
                }
            }
        }
        break;

        /* Resource 4: Server public key */
        case LWM2MCORE_SECURITY_SERVER_KEY_RID:
        {
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 5: Secret key */
        case LWM2MCORE_SECURITY_SECRET_KEY_RID:
        {
            if (DTLS_PSK_MAX_CLIENT_IDENTITY_LEN < len)
            {
                sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            }
            else
            {
#ifdef CREDENTIALS_DEBUG
                lwm2mcore_DataDump("PSK secret write", bufferPtr, len);
#endif
                if (LWM2MCORE_BS_SERVER_OIID == uriPtr->oiid)
                {
                    /* Bootstrap server */
                    memcpy(BsPsk, bufferPtr, len);
                    BsPskLen = len;
                    sID = LWM2MCORE_ERR_COMPLETED_OK;
                }
                else
                {
                    /* Device Management server */
                    memcpy(DmPsk, bufferPtr, len);
                    DmPskLen = len;
                    sID = LWM2MCORE_ERR_COMPLETED_OK;
                }
            }
        }
        break;

        /* Resource 6: SMS security mode */
        case LWM2MCORE_SECURITY_SMS_SECURITY_MODE_RID:
        {
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
        }
        break;

        /* Resource 7: SMS binding key parameters */
        case LWM2MCORE_SECURITY_SMS_BINDING_KEY_PAR_RID:
        {
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
        }
        break;

        /* Resource 8: SMS binding secret key(s) */
        case LWM2MCORE_SECURITY_SMS_BINDING_SEC_KEY_RID:
        {
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
        }
        break;

        /* Resource 9: LWM2M server SMS number */
        case LWM2MCORE_SECURITY_SERVER_SMS_NUMBER_RID:
        {
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
        }
        break;

        /* Resource 10: Short server ID */
        case LWM2MCORE_SECURITY_SERVER_ID_RID:
        {
            if (uriPtr->oiid == LWM2MCORE_BS_SERVER_OIID)
            {
                /* Bootstrap server */
                sID = LWM2MCORE_ERR_COMPLETED_OK;
            }
            else
            {
                /* Device Management server */
                sID = LWM2MCORE_ERR_COMPLETED_OK;
            }
        }
        break;

        /* Resource 11: Client hold of time */
        case LWM2MCORE_SECURITY_CLIENT_HOLD_OFF_TIME_RID:
        {
            if (uriPtr->oiid == LWM2MCORE_BS_SERVER_OIID)
            {
                /* Bootstrap server */
                sID = LWM2MCORE_ERR_COMPLETED_OK;
            }
            else
            {
                /* Device Management server */
                sID = LWM2MCORE_ERR_COMPLETED_OK;
            }
        }
        break;

        default:
        {
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
        }
        break;
    }
    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to read a resource of object 0
 * Object: 0 - Security
 * Resource: All
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int ReadSecurityObj
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t* lenPtr,                     ///< [INOUT] length of input buffer and length of the
                                        ///< returned data
    valueChangedCallback_t changedCb    ///< [IN] callback for notification
)
{
    int sID = LWM2MCORE_ERR_GENERAL_ERROR;

    if ((NULL == uriPtr) || (NULL == bufferPtr) || (NULL == lenPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    /* Check that the server which tries to read/write is the bootstrap one
     * The Device Management server can not access to this resource
     */
    //TODO

    if ((uriPtr->op & LWM2MCORE_OP_READ) == 0)
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    /* Check that the object instance Id is in the correct range */
    if (uriPtr->oiid >= (LWM2MCORE_DM_SERVER_MAX_COUNT + LWM2MCORE_BOOTSRAP_SERVER_MAX_COUNT))
    {
        return LWM2MCORE_ERR_INCORRECT_RANGE;
    }

    switch (uriPtr->rid)
    {
        /* Resource 0: LWM2M server URI */
        case LWM2MCORE_SECURITY_SERVER_URI_RID:
        {
            if (LWM2MCORE_BS_SERVER_OIID == uriPtr->oiid)
            {
                /* Bootstrap server */
                sID = lwm2mcore_GetCredential((uint8_t)LWM2MCORE_CREDENTIAL_BS_ADDRESS,
                                              bufferPtr,
                                              lenPtr);
            }
            else
            {
                /* Device Management server */
                sID = lwm2mcore_GetCredential((uint8_t)LWM2MCORE_CREDENTIAL_DM_ADDRESS,
                                              bufferPtr,
                                              lenPtr);
            }
        }
        break;

        /* Resource 1: Bootstrap server (true or false) */
        case LWM2MCORE_SECURITY_BOOTSTRAP_SERVER_RID:
        {
            if (LWM2MCORE_BS_SERVER_OIID == uriPtr->oiid)
            {
                /* Bootstrap server */
                bufferPtr[0] = 1;
            }
            else
            {
                /* Device Management server */
                bufferPtr[0] = 0;
            }
            *lenPtr = 1;
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 2: Security mode */
        case LWM2MCORE_SECURITY_MODE_RID:
        {
            bufferPtr[0] = SEC_PSK;
            *lenPtr = 1;
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 3: Public key or identity */
        case LWM2MCORE_SECURITY_PKID_RID:
        {
            if (LWM2MCORE_BS_SERVER_OIID == uriPtr->oiid)
            {
                /* Bootstrap server */
                sID = lwm2mcore_GetCredential((uint8_t)LWM2MCORE_CREDENTIAL_BS_PUBLIC_KEY,
                                              bufferPtr,
                                              lenPtr);
            }
            else
            {
                /* Device Management server */
                sID = lwm2mcore_GetCredential((uint8_t)LWM2MCORE_CREDENTIAL_DM_PUBLIC_KEY,
                                              bufferPtr,
                                              lenPtr);
            }
#ifdef CREDENTIALS_DEBUG
            lwm2mcore_DataDump("PSK ID read", bufferPtr, *lenPtr);
#endif
        }
        break;

        /* Resource 4: Server public key */
        case LWM2MCORE_SECURITY_SERVER_KEY_RID:
        {
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 5: Secret key */
        case LWM2MCORE_SECURITY_SECRET_KEY_RID:
        {
            if (LWM2MCORE_BS_SERVER_OIID == uriPtr->oiid)
            {
                /* Bootstrap server */
                sID = lwm2mcore_GetCredential((uint8_t)LWM2MCORE_CREDENTIAL_BS_SECRET_KEY,
                                              bufferPtr,
                                              lenPtr);
            }
            else
            {
                /* Device Management server */
                sID = lwm2mcore_GetCredential((uint8_t)LWM2MCORE_CREDENTIAL_DM_SECRET_KEY,
                                              bufferPtr,
                                              lenPtr);
            }
#ifdef CREDENTIALS_DEBUG
            lwm2mcore_DataDump("PSK secret read", bufferPtr, *lenPtr);
#endif
        }
        break;

        /* Resource 6: SMS security mode */
        case LWM2MCORE_SECURITY_SMS_SECURITY_MODE_RID:
        {
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
        }
        break;

        /* Resource 7: SMS binding key parameters */
        case LWM2MCORE_SECURITY_SMS_BINDING_KEY_PAR_RID:
        {
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
        }
        break;

        /* Resource 8: SMS binding secret key(s) */
        case LWM2MCORE_SECURITY_SMS_BINDING_SEC_KEY_RID:
        {
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
        }
        break;

        /* Resource 9: LWM2M server SMS number */
        case LWM2MCORE_SECURITY_SERVER_SMS_NUMBER_RID:
        {
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
        }
        break;

        /* Resource 10: Short server ID */
        case LWM2MCORE_SECURITY_SERVER_ID_RID:
        {
            if (LWM2MCORE_BS_SERVER_OIID == uriPtr->oiid)
            {
                /* Bootstrap server */
                bufferPtr[0] = 0;
                *lenPtr = 1;
                sID = LWM2MCORE_ERR_COMPLETED_OK;
            }
            else
            {
                /* Device Management server */
                bufferPtr[0] = 1;
                *lenPtr = 1;
                sID = LWM2MCORE_ERR_COMPLETED_OK;
            }
        }
        break;

        /* Resource 11: Client hold of time */
        case LWM2MCORE_SECURITY_CLIENT_HOLD_OFF_TIME_RID:
        {
            if (LWM2MCORE_BS_SERVER_OIID == uriPtr->oiid)
            {
                /* Bootstrap server */
                bufferPtr[0] = 0;
                *lenPtr = 1;
                sID = LWM2MCORE_ERR_COMPLETED_OK;
            }
            else
            {
                /* Device Management server */
                bufferPtr[0] = 0;
                *lenPtr = 1;
                sID = LWM2MCORE_ERR_COMPLETED_OK;
            }
        }
        break;

        default:
        {
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
        }
        break;
    }
    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to store credentials in non volatile memory
 *
 * @return
 *      - true in case of success
 *      - false in case of failure
 */
//--------------------------------------------------------------------------------------------------
bool StoreCredentials
(
    void
)
{
    bool result = false;
    int storageResult = LWM2MCORE_ERR_COMPLETED_OK;

    LOG_ARG("BsPskIdLen %d BsPskLen %d strlen(BsAddr) %d", BsPskIdLen, BsPskLen, strlen(BsAddr));
    LOG_ARG("DmPskIdLen %d DmPskLen %d strlen(DmAddr) %d", DmPskIdLen, DmPskLen, strlen(DmAddr));
    if (BsPskIdLen && BsPskLen && (strlen(BsAddr)))
    {
        storageResult = lwm2mcore_SetCredential((uint8_t)LWM2MCORE_CREDENTIAL_BS_PUBLIC_KEY,
                                                (char*)BsPskId,
                                                BsPskIdLen);
        LOG_ARG("Store BsPskId result %d", storageResult);

        storageResult = lwm2mcore_SetCredential((uint8_t)LWM2MCORE_CREDENTIAL_BS_SECRET_KEY,
                                                (char*)BsPsk,
                                                BsPskLen);
        LOG_ARG("Store BsPsk result %d", storageResult);

        storageResult = lwm2mcore_SetCredential((uint8_t)LWM2MCORE_CREDENTIAL_BS_ADDRESS,
                                                (char*)BsAddr,
                                                strlen(BsAddr));
        LOG_ARG("Store BsAddr result %d", storageResult);
    }

    if (DmPskIdLen && DmPskLen && (strlen(DmAddr)) && (LWM2MCORE_ERR_COMPLETED_OK == storageResult))
    {
        storageResult = lwm2mcore_SetCredential((uint8_t)LWM2MCORE_CREDENTIAL_DM_PUBLIC_KEY,
                                                (char*)DmPskId,
                                                DmPskIdLen);
        LOG_ARG("Store DmPskId result %d", storageResult);

        storageResult = lwm2mcore_SetCredential((uint8_t)LWM2MCORE_CREDENTIAL_DM_SECRET_KEY,
                                                (char*)DmPsk,
                                                DmPskLen);
        LOG_ARG("Store DmPsk result %d", storageResult);

        storageResult = lwm2mcore_SetCredential((uint8_t)LWM2MCORE_CREDENTIAL_DM_ADDRESS,
                                                (char*)DmAddr,
                                                strlen(DmAddr));
        LOG_ARG("Store DmAddr result %d", storageResult);
    }

    if (LWM2MCORE_ERR_COMPLETED_OK == storageResult)
    {
        result = true;

        /* Reset local variables */
        BsPskIdLen = 0;
        BsPskLen = 0;
        DmPskIdLen = 0;
        DmPskLen = 0;

        memset(BsPskId, 0, DTLS_PSK_MAX_CLIENT_IDENTITY_LEN);
        memset(BsPsk, 0, DTLS_PSK_MAX_KEY_LEN);
        memset(BsAddr, 0, LWM2MCORE_SERVER_URI_MAX_LEN);
        memset(DmPskId, 0, DTLS_PSK_MAX_CLIENT_IDENTITY_LEN);
        memset(DmPsk, 0, DTLS_PSK_MAX_KEY_LEN);
        memset(DmAddr, 0, LWM2MCORE_SERVER_URI_MAX_LEN);
    }
    LOG_ARG("credentials storage: %d", result);
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function for the server SMS parameters
 * Object: 0 - Security
 * Resources: 6, 7, 8, 9
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int SmsDummy
(
    lwm2mcore_Uri_t* uriPtr,                ///< [IN] uriPtr represents the requested operation and
                                            ///< object/resource
    char* bufferPtr,                        ///< [INOUT] data buffer for information
    size_t len,                             ///< [IN] length of input buffer
    valueChangedCallback_t changedCb        ///< [IN] not used for READ operation but for WRITE one
)
{
    int sID;

    if ((NULL ==uriPtr) || (NULL == bufferPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    /* Check that the server which tries to read/write is the bootstrap one
     * The Device Management server can not access to this resource
     */
    //TODO

    if ((uriPtr->op & (LWM2MCORE_OP_READ | LWM2MCORE_OP_WRITE)) == 0)
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    /* Check that the object instance Id is in the correct range */
    if (uriPtr->oiid >= (LWM2MCORE_DM_SERVER_MAX_COUNT
                       + LWM2MCORE_BOOTSRAP_SERVER_MAX_COUNT))
    {
        sID = LWM2MCORE_ERR_INCORRECT_RANGE;
    }
    else
    {
        sID = LWM2MCORE_ERR_COMPLETED_OK;
    }
    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 *                                  OBJECT 1: SERVER
 */
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/**
 * Function to write a resource of object 1
 * Object: 1 - Server
 * Resource: all
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int WriteServerObj
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t len                          ///< [IN] length of input buffer
)
{
    int sID;

    if ((NULL == uriPtr) || (NULL == bufferPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    if ((uriPtr->op & LWM2MCORE_OP_WRITE) == 0)
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    /* Check that the object instance Id is in the correct range */
    if (LWM2MCORE_DM_SERVER_MAX_COUNT <= uriPtr->oiid)
    {
        return LWM2MCORE_ERR_INCORRECT_RANGE;
    }

    switch (uriPtr->rid)
    {
        /* Resource 0: Server short ID */
        case LWM2MCORE_SERVER_SHORT_ID_RID:
        {
            /* Write operation */
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 1: Server lifetime */
        case LWM2MCORE_SERVER_LIFETIME_RID:
        {

            Lifetime = (uint64_t)BytesToInt((uint8_t*)bufferPtr, len);
            LOG_ARG("lifetime write %s, %d", bufferPtr, Lifetime);
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 2: Server default minimum period */
        case LWM2MCORE_SERVER_DEFAULT_MIN_PERIOD_RID:
        {
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 3: Server default minimum period */
        case LWM2MCORE_SERVER_DEFAULT_MAX_PERIOD_RID:
        {
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 5: Disable timeout */
        case LWM2MCORE_SERVER_DISABLE_TIMEOUT_RID:
        {
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
        }
        break;

        /* Resource 6: Notification storing when disabled or offline */
        case LWM2MCORE_SERVER_STORE_NOTIF_WHEN_OFFLINE_RID:
        {
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 7: Binding */
        case LWM2MCORE_SERVER_BINDING_MODE_RID:
        {
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        default:
        {
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
        }
        break;
    }
    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to read a resource of object 1
 * Object: 1 - Server
 * Resource: All
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int ReadServerObj
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t* lenPtr,                     ///< [INOUT] length of input buffer and length of the
                                        ///< returned data
    valueChangedCallback_t changedCb    ///< [IN] callback for notification
)
{
    int sID;

    if ((NULL == uriPtr) || (NULL == bufferPtr) || (NULL == lenPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    if ((uriPtr->op & LWM2MCORE_OP_READ) == 0)
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    /* Check that the object instance Id is in the correct range */
    if (LWM2MCORE_DM_SERVER_MAX_COUNT <= uriPtr->oiid)
    {
        return LWM2MCORE_ERR_INCORRECT_RANGE;
    }

    switch (uriPtr->rid)
    {
        /* Resource 0: Server short ID */
        case LWM2MCORE_SERVER_SHORT_ID_RID:
        {
            bufferPtr[0] = 1;
            *lenPtr = 1;
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 1: Server lifetime */
        case LWM2MCORE_SERVER_LIFETIME_RID:
        {
            *lenPtr = FormatValueToBytes((uint8_t*) bufferPtr, &Lifetime, sizeof(Lifetime), false);
            LOG_ARG("lifetime read len %d", *lenPtr);
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 2: Server default minimum period */
        case LWM2MCORE_SERVER_DEFAULT_MIN_PERIOD_RID:
        {
            bufferPtr[0] = LWM2MCORE_PMIN_DEFAULT_VALUE;
            *lenPtr = 1;
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 3: Server default minimum period */
        case LWM2MCORE_SERVER_DEFAULT_MAX_PERIOD_RID:
        {
            bufferPtr[0] = LWM2MCORE_PMAX_DEFAULT_VALUE;
            *lenPtr = 1;
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 5: Disable timeout */
        case LWM2MCORE_SERVER_DISABLE_TIMEOUT_RID:
        {
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
        }
        break;

        /* Resource 6: Notification storing when disabled or offline */
        case LWM2MCORE_SERVER_STORE_NOTIF_WHEN_OFFLINE_RID:
        {
            bufferPtr[0] = 0;
            *lenPtr = 1;
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        /* Resource 7: Binding */
        case LWM2MCORE_SERVER_BINDING_MODE_RID:
        {
            *lenPtr = snprintf(bufferPtr, *lenPtr, LWM2MCORE_BINDING_UDP_QUEUE);
            sID = LWM2MCORE_ERR_COMPLETED_OK;
        }
        break;

        default:
        {
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
        }
        break;
    }
    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 *                                  OBJECT 3: DEVICE
 */
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/**
 * Function to write a resource of object 3
 * Object: 3 - Device
 * Resource: All with write operation
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - LWM2MCORE_ERR_OVERFLOW in case of buffer overflow
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int WriteDeviceObj
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t len                          ///< [IN] length of input buffer
)
{
    int sID;

    if ((!uriPtr) || (!bufferPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    /* Check that the object instance Id is in the correct range (only one object instance) */
    if (0 < uriPtr->oiid)
    {
        return LWM2MCORE_ERR_INCORRECT_RANGE;
    }

    /* Check that the operation is coherent */
    if (0 ==(uriPtr->op & LWM2MCORE_OP_WRITE))
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    switch (uriPtr->rid)
    {
        /* Resource 13: Current time */
        case LWM2MCORE_DEVICE_CURRENT_TIME_RID:
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
            break;


        /* Resource 16: Supported binding mode */
        case LWM2MCORE_DEVICE_SUPPORTED_BINDING_MODE_RID:
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
            break;

        default:
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            break;
    }

    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to read a resource of object 3
 * Object: 3 - Device
 * Resource: All with read operation
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - LWM2MCORE_ERR_OVERFLOW in case of buffer overflow
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int ReadDeviceObj
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t* lenPtr,                     ///< [INOUT] length of input buffer and length of the
                                        ///< returned data
    valueChangedCallback_t changedCb    ///< [IN] callback for notification
)
{
    int sID;

    if ((!uriPtr) || (!bufferPtr) || (!lenPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    /* Check that the object instance Id is in the correct range (only one object instance) */
    if (0 < uriPtr->oiid)
    {
        return LWM2MCORE_ERR_INCORRECT_RANGE;
    }

    /* Check that the operation is coherent */
    if (0 == (uriPtr->op & LWM2MCORE_OP_READ))
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    switch (uriPtr->rid)
    {
        /* Resource 0: Manufacturer */
        case LWM2MCORE_DEVICE_MANUFACTURER_RID:
            sID = lwm2mcore_DeviceManufacturer(bufferPtr, (uint32_t*)lenPtr);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = strlen(bufferPtr);
            }
            break;

        /* Resource 1: Device number */
        case LWM2MCORE_DEVICE_MODEL_NUMBER_RID:
            sID = lwm2mcore_DeviceModelNumber(bufferPtr, (uint32_t*)lenPtr);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = strlen(bufferPtr);
            }
            break;

        /* Resource 2: Serial number */
        case LWM2MCORE_DEVICE_SERIAL_NUMBER_RID:
            sID = lwm2mcore_DeviceSerialNumber(bufferPtr, (uint32_t*)lenPtr);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = strlen(bufferPtr);
            }
            break;

        /* Resource 3: Firmware */
        case LWM2MCORE_DEVICE_FIRMWARE_VERSION_RID:
            sID = lwm2mcore_DeviceFirmwareVersion(bufferPtr, (uint32_t*)lenPtr);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = strlen(bufferPtr);
            }
            break;

        /* Resource 9: Battery level */
        case LWM2MCORE_DEVICE_BATTERY_LEVEL_RID:
        {
            uint8_t batteryLevel = 0;
            sID = lwm2mcore_DeviceBatteryLevel(&batteryLevel);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = FormatValueToBytes((uint8_t*)bufferPtr,
                                             &batteryLevel,
                                             sizeof(batteryLevel),
                                             false);
            }
        }
        break;

        /* Resource 13: Current time */
        case LWM2MCORE_DEVICE_CURRENT_TIME_RID:
        {
            uint64_t currentTime = 0;
            sID = lwm2mcore_DeviceCurrentTime(&currentTime);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = FormatValueToBytes((uint8_t*)bufferPtr,
                                             &currentTime,
                                             sizeof(currentTime),
                                             false);
            }
        }
        break;

        /* Resource 16: Supported binding mode */
        case LWM2MCORE_DEVICE_SUPPORTED_BINDING_MODE_RID:
            *lenPtr = snprintf(bufferPtr, *lenPtr, LWM2MCORE_BINDING_UDP_QUEUE);
            sID = LWM2MCORE_ERR_COMPLETED_OK;
            break;

        default:
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            break;
    }

    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 *                              OBJECT 4: CONNECTIVITY MONITORING
 */
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/**
 * Function to read a resource of object 4
 * Object: 4 - Connectivity monitoring
 * Resource: All
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - LWM2MCORE_ERR_OVERFLOW in case of buffer overflow
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int ReadConnectivityMonitoringObj
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t* lenPtr,                     ///< [INOUT] length of input buffer and length of the
                                        ///< returned data
    valueChangedCallback_t changedCb    ///< [IN] callback for notification
)
{
    int sID;

    if ((!uriPtr) || (!bufferPtr) || (!lenPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    /* Check that the object instance Id is in the correct range (only one object instance) */
    if (0 < uriPtr->oiid)
    {
        return LWM2MCORE_ERR_INCORRECT_RANGE;
    }

    /* Check that the operation is coherent */
    if (0 == (uriPtr->op & LWM2MCORE_OP_READ))
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    switch (uriPtr->rid)
    {
        /* Resource 0: Network bearer */
        case LWM2MCORE_CONN_MONITOR_NETWORK_BEARER_RID:
        {
            lwm2mcore_networkBearer_enum_t networkBearer;
            sID = lwm2mcore_ConnectivityNetworkBearer(&networkBearer);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = FormatValueToBytes((uint8_t*)bufferPtr,
                                             &networkBearer,
                                             sizeof(networkBearer),
                                             false);
            }
        }
        break;

        /* Resource 1: Available network bearer */
        case LWM2MCORE_CONN_MONITOR_AVAIL_NETWORK_BEARER_RID:
            /* Check that the resource instance Id is in the correct range */
            if (uriPtr->riid < CONN_MONITOR_AVAIL_NETWORK_BEARER_MAX_NB)
            {
                static lwm2mcore_networkBearer_enum_t
                       bearersList[CONN_MONITOR_AVAIL_NETWORK_BEARER_MAX_NB];
                static uint16_t bearersNb = 0;

                if (0 == uriPtr->riid)
                {
                    /* Reset the available network bearers list */
                    memset(bearersList, 0, sizeof(bearersList));
                    bearersNb = 0;

                    /* Retrieve the list */
                    sID = lwm2mcore_ConnectivityAvailableNetworkBearers(&bearersList, &bearersNb);
                }
                else
                {
                    sID = LWM2MCORE_ERR_COMPLETED_OK;
                }

                if (LWM2MCORE_ERR_COMPLETED_OK == sID)
                {
                    if (uriPtr->riid < bearersNb)
                    {
                        *lenPtr = FormatValueToBytes((uint8_t*)bufferPtr,
                                                     &bearersList[uriPtr->riid],
                                                     sizeof(bearersList[uriPtr->riid]),
                                                     false);
                    }
                    else
                    {
                        *lenPtr = 0;
                    }
                }
            }
            else
            {
                sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            }
            break;

        /* Resource 2: Radio signal strength */
        case LWM2MCORE_CONN_MONITOR_RADIO_SIGNAL_STRENGTH_RID:
        {
            int32_t signalStrength;
            sID = lwm2mcore_ConnectivitySignalStrength(&signalStrength);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = FormatValueToBytes((uint8_t*)bufferPtr,
                                             &signalStrength,
                                             sizeof(signalStrength),
                                             true);
            }
        }
        break;

        /* Resource 3: Link quality */
        case LWM2MCORE_CONN_MONITOR_LINK_QUALITY_RID:
        {
            uint16_t linkQuality;
            sID = lwm2mcore_ConnectivityLinkQuality(&linkQuality);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = FormatValueToBytes((uint8_t*)bufferPtr,
                                             &linkQuality,
                                             sizeof(linkQuality),
                                             false);
            }
        }
        break;

        /* Resource 4: IP addresses */
        case LWM2MCORE_CONN_MONITOR_IP_ADDRESSES_RID:
            /* Check that the resource instance Id is in the correct range */
            if (uriPtr->riid < CONN_MONITOR_IP_ADDRESSES_MAX_NB)
            {
                static char ipAddrList[CONN_MONITOR_IP_ADDRESSES_MAX_NB]
                                      [CONN_MONITOR_IP_ADDR_MAX_BYTES];
                static uint16_t ipAddrNb = 0;

                if (0 == uriPtr->riid)
                {
                    /* Reset the IP addresses list */
                    memset(ipAddrList, 0, sizeof(ipAddrList));
                    ipAddrNb = 0;

                    /* Retrieve the list */
                    sID = lwm2mcore_ConnectivityIpAddresses(ipAddrList, &ipAddrNb);
                }
                else
                {
                    sID = LWM2MCORE_ERR_COMPLETED_OK;
                }

                if (LWM2MCORE_ERR_COMPLETED_OK == sID)
                {
                    if (uriPtr->riid < ipAddrNb)
                    {
                        *lenPtr = snprintf(bufferPtr,
                                           CONN_MONITOR_IP_ADDR_MAX_BYTES,
                                           "%s",
                                           ipAddrList[uriPtr->riid]);
                    }
                    else
                    {
                        *lenPtr = 0;
                    }
                }
            }
            else
            {
                sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            }
            break;

        /* Resource 5: Router IP addresses */
        case LWM2MCORE_CONN_MONITOR_ROUTER_IP_ADDRESSES_RID:
            /* Check that the resource instance Id is in the correct range */
            if (uriPtr->riid < CONN_MONITOR_ROUTER_IP_ADDRESSES_MAX_NB)
            {
                static char ipAddrList[CONN_MONITOR_ROUTER_IP_ADDRESSES_MAX_NB]
                                      [CONN_MONITOR_IP_ADDR_MAX_BYTES];
                static uint16_t ipAddrNb = 0;

                if (0 == uriPtr->riid)
                {
                    /* Reset the router IP addresses list */
                    memset(ipAddrList, 0, sizeof(ipAddrList));
                    ipAddrNb = 0;

                    /* Retrieve the list */
                    sID = lwm2mcore_ConnectivityRouterIpAddresses(ipAddrList, &ipAddrNb);
                }
                else
                {
                    sID = LWM2MCORE_ERR_COMPLETED_OK;
                }

                if (LWM2MCORE_ERR_COMPLETED_OK == sID)
                {
                    if (uriPtr->riid < ipAddrNb)
                    {
                        *lenPtr = snprintf(bufferPtr,
                                           CONN_MONITOR_IP_ADDR_MAX_BYTES,
                                           "%s",
                                           ipAddrList[uriPtr->riid]);
                    }
                    else
                    {
                        *lenPtr = 0;
                    }
                }
            }
            else
            {
                sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            }
            break;

        /* Resource 6: Link utilization */
        case LWM2MCORE_CONN_MONITOR_LINK_UTILIZATION_RID:
        {
            uint8_t linkUtilization;
            sID = lwm2mcore_ConnectivityLinkUtilization(&linkUtilization);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = FormatValueToBytes((uint8_t*)bufferPtr,
                                             &linkUtilization,
                                             sizeof(linkUtilization),
                                             false);
            }
        }
        break;

        /* Resource 7: Access Point Name */
        case LWM2MCORE_CONN_MONITOR_APN_RID:
            /* Check that the resource instance Id is in the correct range */
            if (uriPtr->riid < CONN_MONITOR_APN_MAX_NB)
            {
                static char apnList[CONN_MONITOR_APN_MAX_NB][CONN_MONITOR_APN_MAX_BYTES];
                static uint16_t apnNb = 0;

                if (0 == uriPtr->riid)
                {
                    /* Reset the APN list */
                    memset(apnList, 0, sizeof(apnList));
                    apnNb = 0;

                    /* Retrieve the list */
                    sID = lwm2mcore_ConnectivityApn(apnList, &apnNb);
                }
                else
                {
                    sID = LWM2MCORE_ERR_COMPLETED_OK;
                }

                if (LWM2MCORE_ERR_COMPLETED_OK == sID)
                {
                    if (uriPtr->riid < apnNb)
                    {
                        *lenPtr = snprintf(bufferPtr,
                                           CONN_MONITOR_APN_MAX_BYTES,
                                           "%s",
                                           apnList[uriPtr->riid]);
                    }
                    else
                    {
                        *lenPtr = 0;
                    }
                }
            }
            else
            {
                sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            }
            break;

        /* Resource 8: Cell ID */
        case LWM2MCORE_CONN_MONITOR_CELL_ID_RID:
        {
            uint32_t cellId;
            sID = lwm2mcore_ConnectivityCellId(&cellId);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = FormatValueToBytes((uint8_t*)bufferPtr,
                                             &cellId,
                                             sizeof(cellId),
                                             false);
            }
        }
        break;

        /* Resource 9: Serving Mobile Network Code */
        case LWM2MCORE_CONN_MONITOR_SMNC_RID:
        {
            uint16_t mnc;
            sID = lwm2mcore_ConnectivityMncMcc(&mnc, NULL);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = FormatValueToBytes((uint8_t*)bufferPtr,
                                             &mnc,
                                             sizeof(mnc),
                                             false);
            }
        }
        break;

        /* Resource 10: Serving Mobile Country Code */
        case LWM2MCORE_CONN_MONITOR_SMCC_RID:
        {
            uint16_t mcc;
            sID = lwm2mcore_ConnectivityMncMcc(NULL, &mcc);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = FormatValueToBytes((uint8_t*)bufferPtr,
                                             &mcc,
                                             sizeof(mcc),
                                             false);
            }
        }
        break;

        default:
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            break;
    }

    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 *                                  OBJECT 5: FIRMWARE UPDATE
 */
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/**
 * Function to write a resource of object 5
 * Object: 5 - Firmware update
 * Resource: all with write operation
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int WriteFwUpdateObj
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t len                          ///< [IN] length of input buffer
)
{
    int sID;
    if ((NULL == uriPtr) || (NULL == bufferPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    /* Only one object instance */
    if (0 < uriPtr->oiid)
    {
        return LWM2MCORE_ERR_INCORRECT_RANGE;
    }

    if (0 == (uriPtr->op & LWM2MCORE_OP_WRITE))
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    switch (uriPtr->rid)
    {
        /* Resource 1: Package URI */
        case LWM2MCORE_FW_UPDATE_PACKAGE_URI_RID:
        {
            if (LWM2MCORE_BUFFER_MAX_LEN < len)
            {
                sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            }
            else
            {
                sID = lwm2mcore_UpdateSetPackageUri(LWM2MCORE_FW_UPDATE_TYPE,
                                                    uriPtr->oid,
                                                    bufferPtr,
                                                    len);
            }
        }
        break;

        default:
        {
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
        }
        break;
    }
    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to read a resource of object 5
 * Object: 5 - Firmware update
 * Resource: all with read operation
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int ReadFwUpdateObj
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t* lenPtr,                     ///< [INOUT] length of input buffer and length of the
                                        ///< returned data
    valueChangedCallback_t changedCb    ///< [IN] not used for READ operation but for WRITE one
)
{
    int sID;
    if ((NULL == uriPtr) || (NULL == bufferPtr) || (NULL == lenPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    /* Only one object instance */
    if (0 < uriPtr->oiid)
    {
        return LWM2MCORE_ERR_INCORRECT_RANGE;
    }

    if (0 == (uriPtr->op & LWM2MCORE_OP_READ))
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    switch (uriPtr->rid)
    {
        /* Resource 1: Package URI */
        case LWM2MCORE_FW_UPDATE_PACKAGE_URI_RID:
        {
            sID = lwm2mcore_UpdateGetPackageUri(LWM2MCORE_FW_UPDATE_TYPE,
                                                uriPtr->oid,
                                                bufferPtr,
                                                lenPtr);
        }
        break;

        /* Resource 3: Update state */
        case LWM2MCORE_FW_UPDATE_UPDATE_STATE_RID:
        {
            uint8_t updateState;
            sID = lwm2mcore_UpdateGetUpdateState(LWM2MCORE_FW_UPDATE_TYPE,
                                                 uriPtr->oiid,
                                                 &updateState);
            if (sID == LWM2MCORE_ERR_COMPLETED_OK)
            {
                *lenPtr = FormatValueToBytes((uint8_t*) bufferPtr,
                                             &updateState,
                                             sizeof(updateState),
                                             false);
            }
        }
        break;

        /* Resource 5: Update result */
        case LWM2MCORE_FW_UPDATE_UPDATE_RESULT_RID:
        {
            uint8_t updateResult;
            sID = lwm2mcore_UpdateGetUpdateResult(LWM2MCORE_FW_UPDATE_TYPE,
                                                  uriPtr->oiid,
                                                  &updateResult);
            if (sID == LWM2MCORE_ERR_COMPLETED_OK)
            {
                *lenPtr = FormatValueToBytes((uint8_t*) bufferPtr,
                                            &updateResult,
                                            sizeof(updateResult),
                                            false);
            }
        }
        break;

        /* Resource 6: Package name */
        case LWM2MCORE_FW_UPDATE_PACKAGE_NAME_RID:
        {
            sID = LWM2MCORE_ERR_OP_NOT_SUPPORTED;
        }
        break;

        /* Resource 7: Package version */
        case LWM2MCORE_FW_UPDATE_PACKAGE_VERSION_RID:
        {
            sID = LWM2MCORE_ERR_OP_NOT_SUPPORTED;
        }
        break;

        default:
        {
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
        }
        break;
    }
    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to execute a resource of object 5
 * Object: 5 - Firmware update
 * Resource: all with execute operation
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int ExecFwUpdate
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t len                          ///< [IN] length of input buffer
)
{
    int sID;

    // bufferPtr can be null as per spec (OMA-TS-LightweightM2M-V1_0-20151214-C.pdf, appendix E.6)
    if (NULL == uriPtr)
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    if (0 < uriPtr->oiid)
    {
        /* Only one object instance */
        return LWM2MCORE_ERR_INCORRECT_RANGE;
    }

    /* Check if the related command is EXECUTE */
    if (0 == (uriPtr->op & LWM2MCORE_OP_EXECUTE))
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    switch (uriPtr->rid)
    {
        /* Resource 2: Update */
        case LWM2MCORE_FW_UPDATE_UPDATE_RID:
        {
            sID = lwm2mcore_UpdateLaunchUpdate(LWM2MCORE_FW_UPDATE_TYPE,
                                               uriPtr->oiid,
                                               bufferPtr,
                                               len);
        }
        break;

        default:
        {
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
        }
        break;
    }

    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 *                                  OBJECT 9: SOFTWARE UPDATE
 */
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/**
 * Function to write a resource of object 9
 * Object: 9 - software update
 * Resource: all with write operation
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int WriteSwUpdateObj
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t len                          ///< [IN] length of input buffer
)
{
    int sID;
    if ((NULL == uriPtr) || (NULL == bufferPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    /* Check if the related command is WRITE */
    if (0 == (uriPtr->op & LWM2MCORE_OP_WRITE))
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    LOG_ARG("WriteSwUpdateObj rid %d", uriPtr->rid);
    switch (uriPtr->rid)
    {
        /* Resource 3: Package URI */
        case LWM2MCORE_SW_UPDATE_PACKAGE_URI_RID:
        {
            LOG_ARG("WriteSwUpdateObj len %d", len);
            if (LWM2MCORE_BUFFER_MAX_LEN < len)
            {
                sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            }
            else
            {
                sID = lwm2mcore_UpdateSetPackageUri(LWM2MCORE_SW_UPDATE_TYPE,
                                                    uriPtr->oid,
                                                    bufferPtr,
                                                    len);
            }
        }
        break;

        /* Resource 8: Update Supported Objects */
        case LWM2MCORE_SW_UPDATE_UPDATE_SUPPORTED_OBJ_RID:
        {
            if (LWM2MCORE_BUFFER_MAX_LEN < len)
            {
                sID = LWM2MCORE_ERR_INCORRECT_RANGE;
            }
            else
            {
                sID = lwm2mcore_UpdateSetSwSupportedObjects(uriPtr->oiid,
                                                            (bool)BytesToInt(bufferPtr, len));
            }
        }
        break;

        default:
        {
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
        }
        break;
    }
    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to read a resource of object 9
 * Object: 9 - Software update
 * Resource: all with read operation
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int ReadSwUpdateObj
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t* lenPtr,                     ///< [INOUT] length of input buffer and length of the
                                        ///< returned data
    valueChangedCallback_t changedCb    ///< [IN] not used for READ operation but for WRITE one
)
{
    int sID;
    if ((NULL == uriPtr) || (NULL == bufferPtr) || (NULL == lenPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    /* Check if the related command is READ */
    if (0 == (uriPtr->op & LWM2MCORE_OP_READ))
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    switch (uriPtr->rid)
    {
        /* Resource 0: package name */
        case LWM2MCORE_SW_UPDATE_PACKAGE_NAME_RID:
        {
            sID = lwm2mcore_UpdateGetPackageName(LWM2MCORE_SW_UPDATE_TYPE,
                                                 uriPtr->oiid,
                                                 bufferPtr,
                                                 (uint32_t)*lenPtr);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = strlen(bufferPtr);
            }
        }
        break;

        /* Resource 1: package version */
        case LWM2MCORE_SW_UPDATE_PACKAGE_VERSION_RID:
        {
            sID = lwm2mcore_UpdateGetPackageVersion(LWM2MCORE_SW_UPDATE_TYPE,
                                                    uriPtr->oiid,
                                                    bufferPtr,
                                                    (uint32_t)*lenPtr);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = strlen(bufferPtr);
            }
        }
        break;

        /* Resource 7: Update State */
        case LWM2MCORE_SW_UPDATE_UPDATE_STATE_RID:
        {
            uint8_t updateResult;
            sID = lwm2mcore_UpdateGetUpdateState(LWM2MCORE_SW_UPDATE_TYPE,
                                                 uriPtr->oiid,
                                                 &updateResult);
            if (sID == LWM2MCORE_ERR_COMPLETED_OK)
            {
                *lenPtr = FormatValueToBytes((uint8_t*) bufferPtr,
                                             &updateResult,
                                             sizeof(updateResult),
                                             false);
            }
        }
        break;

        /* Resource 8: Update Supported Objects */
        case LWM2MCORE_SW_UPDATE_UPDATE_SUPPORTED_OBJ_RID:
        {
            bool value;
            sID = lwm2mcore_UpdateGetSwSupportedObjects(uriPtr->oiid, &value);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = FormatValueToBytes((uint8_t*) bufferPtr,
                                             &value,
                                             sizeof(value),
                                             false);
            }
        }
        break;

        /* Resource 9: Update result */
        case LWM2MCORE_SW_UPDATE_UPDATE_RESULT_RID:
        {
            uint8_t updateResult;
            sID = lwm2mcore_UpdateGetUpdateResult(LWM2MCORE_SW_UPDATE_TYPE,
                                                  uriPtr->oiid,
                                                  &updateResult);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = FormatValueToBytes((uint8_t*) bufferPtr,
                                             &updateResult,
                                             sizeof(updateResult),
                                             false);
            }
        }
        break;

        /* Resource 12: Activation state */
        case LWM2MCORE_SW_UPDATE_ACTIVATION_STATE_RID:
        {
            bool value;
            sID = lwm2mcore_UpdateGetSwActivationState(uriPtr->oiid, &value);
            if (LWM2MCORE_ERR_COMPLETED_OK == sID)
            {
                *lenPtr = FormatValueToBytes((uint8_t*) bufferPtr,
                                             &value,
                                             sizeof(value),
                                             false);
            }
        }
        break;

        default:
        {
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
        }
        break;
    }
    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to execute a resource of object 9
 * Object: 9 - Software update
 * Resource: all with execute operation
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int ExecSwUpdate
(
    lwm2mcore_Uri_t* uriPtr,            ///< [IN] uri represents the requested operation and
                                        ///< object/resource
    char* bufferPtr,                    ///< [INOUT] data buffer for information
    size_t len                          ///< [IN] length of input buffer
)
{
    int sID;

    if ((NULL == uriPtr) || ((NULL == bufferPtr) && len))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    /* Check if the related command is EXECUTE */
    if (0 == (uriPtr->op & LWM2MCORE_OP_EXECUTE))
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    switch (uriPtr->rid)
    {
        /* Resource 4: Install */
        case LWM2MCORE_SW_UPDATE_INSTALL_RID:
        {
            sID = lwm2mcore_UpdateLaunchUpdate(LWM2MCORE_SW_UPDATE_TYPE,
                                               uriPtr->oiid,
                                               bufferPtr,
                                               len);
        }
        break;

        /* Resource 6: Uninstall */
        case LWM2MCORE_SW_UPDATE_UNINSTALL_RID:
        {
            sID = lwm2mcore_UpdateLaunchSwUninstall(uriPtr->oiid,
                                                    bufferPtr,
                                                    len);
        }
        break;

        /* Resource 10: Activate */
        case LWM2MCORE_SW_UPDATE_ACTIVATE_RID:
        {
            sID = lwm2mcore_UpdateActivateSoftware(true,
                                                   uriPtr->oiid,
                                                   bufferPtr,
                                                   len);
        }
        break;

        /* Resource 11: Deactivate */
        case LWM2MCORE_SW_UPDATE_DEACTIVATE_RID:
        {
            sID = lwm2mcore_UpdateActivateSoftware(false,
                                                   uriPtr->oiid,
                                                   bufferPtr,
                                                   len);
        }
        break;

        default:
        {
            sID = LWM2MCORE_ERR_INCORRECT_RANGE;
        }
        break;
    }

    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 *                              OBJECT 10243: SSL certificates
 */
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/**
 * Function to read/write the SSL certificates
 * Object: 10243 - SSL certificates
 * Resource: 0
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int OnSslCertif
(
    lwm2mcore_Uri_t* uriPtr,                ///< [IN] uriPtr represents the requested operation and
                                            ///< object/resource
    char* bufferPtr,                        ///< [INOUT] data buffer for information
    size_t* lenPtr,                         ///< [INOUT] length of input buffer and length of the
                                            ///< returned data
    valueChangedCallback_t changedCb        ///< [IN] not used for READ operation but for WRITE one
)
{
    int sID;
    if ((NULL == uriPtr) || (NULL == bufferPtr) || (NULL == lenPtr))
    {
        return LWM2MCORE_ERR_INVALID_ARG;
    }

    if (0 == (uriPtr->op & (LWM2MCORE_OP_READ | LWM2MCORE_OP_WRITE)))
    {
        return LWM2MCORE_ERR_OP_NOT_SUPPORTED;
    }

    /* Only one instance */
    if (0 < uriPtr->oiid)
    {
        return LWM2MCORE_ERR_INCORRECT_RANGE;
    }

    /* This resource needs the BLOCK1 option support */
    if (uriPtr->op & LWM2MCORE_OP_READ)
    {
        /* Read operation */
        if (0 == *lenPtr)
        {
            /* Delete the certificates */
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
        }
        else
        {
            /* Read the stored certificates */
            sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
        }
    }
    else
    {
        /* Write a certificate */
        sID = LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
    }
    return sID;
}

//--------------------------------------------------------------------------------------------------
/**
 * Function for not registered objects
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - positive value for asynchronous response
 */
//--------------------------------------------------------------------------------------------------
int OnUnlistedObject
(
    lwm2mcore_Uri_t* uriPtr,                ///< [IN] uriPtr represents the requested operation and
                                            ///< object/resource
    char* bufferPtr,                        ///< [INOUT] data buffer for information
    size_t* lenPtr,                         ///< [INOUT] length of input buffer and length of the
                                            ///< returned data
    valueChangedCallback_t changedCb        ///< [IN] callback function pointer for OBSERVE
                                            ///< operation
)
{
    return LWM2MCORE_ERR_NOT_YET_IMPLEMENTED;
}

