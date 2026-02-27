/*
 * FreeRTOS+TCP <DEVELOPMENT BRANCH>
 * Copyright (C) 2022 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */



/* Include standard libraries */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "list.h"

#include "FreeRTOS_IP.h"
#include "FreeRTOS_IP_Private.h"

NetworkInterface_t xInterfaces[ 1 ];

volatile BaseType_t xInsideInterrupt = pdFALSE;

BaseType_t xNetworkUp;

/** @brief The expected IP version and header length coded into the IP header itself. */
#define ipIP_VERSION_AND_HEADER_LENGTH_BYTE    ( ( uint8_t ) 0x45 )

uint32_t ulApplicationGetNextSequenceNumber( uint32_t ulSourceAddress,
                                             uint16_t usSourcePort,
                                             uint32_t ulDestinationAddress,
                                             uint16_t usDestinationPort )
{
}

/* Use by the pseudo random number generator. */
static UBaseType_t ulNextRand;

UBaseType_t uxRand( void )
{
    const uint32_t ulMultiplier = 0x015a4e35UL, ulIncrement = 1UL;

    /* Utility function to generate a pseudo random number. */

    ulNextRand = ( ulMultiplier * ulNextRand ) + ulIncrement;
    return( ( int ) ( ulNextRand ) & 0x7fffUL );
}

BaseType_t xApplicationGetRandomNumber( uint32_t * pulNumber )
{
    *pulNumber = ( uint32_t ) uxRand();

    return pdTRUE;
}

#if ( ipconfigIPv4_BACKWARD_COMPATIBLE == 1 )
    void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent )
#else
    void vApplicationIPNetworkEventHook_Multi( eIPCallbackEvent_t eNetworkEvent,
                                               struct xNetworkEndPoint * pxEndPoint )
#endif
/* *INDENT-ON* */
{
    static BaseType_t xTasksAlreadyCreated = pdFALSE;

    /* If the network has just come up...*/
    if( ( eNetworkEvent == eNetworkUp ) && ( xTasksAlreadyCreated == pdFALSE ) )
    {
        /* Do nothing. Just a stub. */

        xTasksAlreadyCreated = pdTRUE;
    }
}

size_t xPortGetMinimumEverFreeHeapSize( void )
{
    return 0;
}

BaseType_t xApplicationDNSQueryHook_Multi( struct xNetworkEndPoint * pxEndPoint,
                                           const char * pcName )
{
}

void vApplicationPingReplyHook( ePingReplyStatus_t eStatus,
                                uint16_t usIdentifier )
{
    /* Provide a stub for this function. */
}

const char * pcApplicationHostnameHook( void )
{
    /* This function will be called during the DHCP: the machine will be registered
        * with an IP address plus this name. */
    return "mainHOST_NAME";
}

#if ( ipconfigUSE_DHCP_HOOK != 0 )
    #if ( ipconfigIPv4_BACKWARD_COMPATIBLE == 1 )
        eDHCPCallbackAnswer_t xApplicationDHCPHook( eDHCPCallbackPhase_t eDHCPPhase,
                                                    uint32_t ulIPAddress )
    #else
        eDHCPCallbackAnswer_t xApplicationDHCPHook_Multi( eDHCPCallbackPhase_t eDHCPPhase,
                                                          struct xNetworkEndPoint * pxEndPoint,
                                                          IP_Address_t * pxIPAddress )
    #endif
    {
        /* Provide a stub for this function. */
        return eDHCPContinue;
    }
#endif /* ( ipconfigUSE_DHCP_HOOK != 0 ) */

int ipFOREVER( void ) {
    return 0;
}

uint32_t ulApplicationTimeHook( void )
{
    /** @brief The function time() counts since 1-1-1970.  The DHCPv6 time-stamp however
     * uses a time stamp that had zero on 1-1-2000. */
    return 946684800U;
}