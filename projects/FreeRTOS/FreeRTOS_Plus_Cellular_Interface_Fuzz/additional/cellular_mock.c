#include "cellular_platform.h"
#include "cellular_types.h"

void MockPlatformMutex_Lock( PlatformMutex_t * pMutex )
{
    ( void ) pMutex;
}

void MockPlatformMutex_Unlock( PlatformMutex_t * pMutex )
{
    ( void ) pMutex;
}

uint16_t MockPlatformEventGroup_Delete( PlatformEventGroupHandle_t groupEvent )
{
    ( void ) groupEvent;
    return 0U;
}

uint16_t MockPlatformEventGroup_WaitBits( PlatformEventGroupHandle_t groupEvent,
                                          EventBits_t uxBitsToWaitFor,
                                          BaseType_t xClearOnExit,
                                          BaseType_t xWaitForAllBits,
                                          TickType_t xTicksToWait )
{
    ( void ) groupEvent;
    ( void ) uxBitsToWaitFor;
    ( void ) xClearOnExit;
    ( void ) xWaitForAllBits;
    ( void ) xTicksToWait;

    return 0;
}

void * mock_malloc( size_t size )
{
    return ( void * ) malloc( size );
}

void dummyDelay( uint32_t milliseconds )
{
    ( void ) milliseconds;
}

uint16_t MockPlatformEventGroup_SetBits( PlatformEventGroupHandle_t groupEvent,
                                         EventBits_t event )
{
    ( void ) groupEvent;
    ( void ) event;

    return 0;
}

int32_t MockPlatformEventGroup_SetBitsFromISR( PlatformEventGroupHandle_t groupEvent,
                                               EventBits_t event,
                                               BaseType_t * pHigherPriorityTaskWoken )
{
    int32_t ret = pdFALSE;

    ( void ) groupEvent;
    ( void ) event;
    ( void ) pHigherPriorityTaskWoken;

    return ret;
}

uint16_t MockPlatformEventGroup_GetBits( PlatformEventGroupHandle_t groupEvent )
{
    ( void ) groupEvent;

    return 0;
}

void MockPlatformMutex_Destroy( PlatformMutex_t * pMutex )
{
    pMutex->created = false;
}


uint16_t MockvQueueDelete( QueueHandle_t queue )
{
    free( queue );
    queue = NULL;
    return 1;
}

void dummyTaskENTER_CRITICAL( void )
{
}

void dummyTaskEXIT_CRITICAL( void )
{
}

bool MockPlatformMutex_Create( PlatformMutex_t * pNewMutex,
                               bool recursive )
{
    bool ret = false;

    pNewMutex->created = ret;
    return ret;
}

QueueHandle_t MockxQueueCreate( int32_t uxQueueLength,
                                uint32_t uxItemSize )
{
    ( void ) uxQueueLength;
    ( void ) uxItemSize;

    return NULL;
}

BaseType_t MockxQueueReceive( QueueHandle_t queue,
                              void * data,
                              uint32_t time )
{
    ( void ) queue;
    ( void ) time;

    return false;
}

MockPlatformEventGroupHandle_t MockPlatformEventGroup_Create( void )
{
    return NULL;
}

uint16_t MockPlatformEventGroup_ClearBits( PlatformEventGroupHandle_t xEventGroup,
                                           TickType_t uxBitsToClear )
{
    ( void ) xEventGroup;
    ( void ) uxBitsToClear;
    return 0;
}

BaseType_t MockxQueueSend( QueueHandle_t queue,
                           void * data,
                           uint32_t time )
{
    ( void ) queue;
    ( void ) time;

    return false;
}

bool Platform_CreateDetachedThread( void ( * threadRoutine )( void * pArgument ),
                                    void * pArgument,
                                    size_t priority,
                                    size_t stackSize )
{
    ( void ) pArgument;
    ( void ) priority;
    ( void ) stackSize;
    return false;
}

CellularError_t Cellular_ModuleInit( const CellularContext_t * pContext,
                                     void ** ppModuleContext )
{
    return CELLULAR_SUCCESS;
}


CellularError_t Cellular_ModuleCleanUp( const CellularContext_t * pContext )
{
    return CELLULAR_SUCCESS;
}


CellularError_t Cellular_ModuleEnableUE( CellularContext_t * pContext )
{
    return CELLULAR_SUCCESS;
}


CellularError_t Cellular_ModuleEnableUrc( CellularContext_t * pContext )
{
    return CELLULAR_SUCCESS;
}