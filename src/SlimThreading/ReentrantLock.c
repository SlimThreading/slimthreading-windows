// Copyright 2011 Carlos Martins
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
// http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "StInternal.h"

//
// Initializes the reentrant lock.
//

VOID
WINAPI
StReentrantLock_Init (
    __out PST_REENTRANT_LOCK RLock,
    __in ULONG SpinCount
    )
{
    StLock_Init(&RLock->Lock, SpinCount);
    RLock->Owner = INVALID_THREAD_ID;
    RLock->Count = 0;
}

//
// Tries to acquire the reentrant lock if it is free or
// it is already owned by the current thread.
// 

static
FORCEINLINE
BOOLEAN
TryAcquireInternal (
    __inout PST_REENTRANT_LOCK RLock,
    __out PULONG ThreadId
    )
{
    *ThreadId = GetCurrentThreadId();
    if (TryAcquireLockInternal(&RLock->Lock)) {
        RLock->Owner = *ThreadId;
        return TRUE;
    }
    if (RLock->Owner == *ThreadId) {
        RLock->Count++;
        return TRUE;
    }
    return FALSE;
}

//
// Tries to acquire the reentrant lock within the specified time interval.
//

BOOL
WINAPI
StReentrantLock_TryEnterEx (
    __inout PST_REENTRANT_LOCK RLock,
    __in ULONG Timeout
    )
{
    ULONG ThreadId;

    if (TryAcquireInternal(RLock, &ThreadId)) {
        return TRUE;
    }

    //
    // The lock is busy; so, if a null timeout was specified,
    // return failure.
    //

    if (Timeout == 0) {
        return FALSE;
    }

    //
    // Try to acquire the busy associated non-reentrant lock.
    //
        
    if (Lock_TryEnterTail(&RLock->Lock, Timeout)) {

        _ASSERTE(RLock->Owner == INVALID_THREAD_ID && RLock->Count == 0);

        //
        // Set owner thread, since that the recursive acquisition
        // counter is already set to zero.
        //
        
        RLock->Owner = ThreadId;
        return TRUE;
    }

    //
    // The specified timeout is expired, so return failure.
    //
        
    return FALSE;
}

BOOL
WINAPI
StReentrantLock_TryEnter (
    __inout PST_REENTRANT_LOCK RLock
    )
{
    ULONG ThreadId;

    return TryAcquireInternal(RLock, &ThreadId);
}

//
// Acquires the reentrant lock unconditionally.
//

BOOL
WINAPI
StReentrantLock_Enter (
    __inout PST_REENTRANT_LOCK RLock
    )
{
    ULONG ThreadId;

    if (TryAcquireInternal(RLock, &ThreadId)) {
        return TRUE;
    }
    if (Lock_TryEnterTail(&RLock->Lock, INFINITE)) {

        _ASSERTE(RLock->Owner == INVALID_THREAD_ID && RLock->Count == 0);

        //
        // Set owner thread, since that the recursive acquisition
        // counter is already set to zero.
        //
        
        RLock->Owner = ThreadId;
        return TRUE;
    }
    return FALSE;
}

//
// Exits the reentrant lock.
//

VOID
WINAPI
StReentrantLock_Exit (
    __inout PST_REENTRANT_LOCK RLock
    )
{
    _ASSERTE(RLock->Owner == GetCurrentThreadId());
    
    if (RLock->Count == 0) {

        //
        // Clear the owner thread and release the associated
        // non-reentrant lock.
        //
        
        RLock->Owner = INVALID_THREAD_ID;
        StLock_Exit(&RLock->Lock);
    } else {

        //
        // This is a recursive leave; so, decrement the recursive
        // acquisition counter.
        //

        RLock->Count--;	
    }
}

/*++
 *
 * Virtual functions used by the condition variables.
 *
 --*/

//
// Returns true if the reentrant lock is owned by the current thread.
//

static
BOOLEAN
FASTCALL
_IsOwned (
    __in PVOID RLock
    )
{
    return ((PST_REENTRANT_LOCK)RLock)->Owner == GetCurrentThreadId();
}

//
// Leaves completely the reentrant lock and returns the current
// lock's state.
//

static
LONG
FASTCALL
_ReleaseCompletely (
    __inout PVOID RLock
    )
{
    LONG PreviousCount;
    PST_REENTRANT_LOCK RLockx = (PST_REENTRANT_LOCK)RLock;

    //
    // Save the current state of the reentrant lock.
    //

    PreviousCount = RLockx->Count;

    //
    // Clear the owner thread, set the recursive acquisition counter
    // to zero and release the associated non-reentrant lock.
    //
        
    RLockx->Count = 0;
    RLockx->Owner = INVALID_THREAD_ID;	
    StLock_Exit(&RLockx->Lock);

    //
    // Return the previous state of the reentrant lock.
    //

    return PreviousCount;
}

//
// Reacquires the reentrant lock and restores its previous state.
//

static
VOID
FASTCALL
_Reacquire (
    __inout PVOID RLock,
    __in ULONG Ignored,
    __in LONG PreviousCount
    )
{
    
    PST_REENTRANT_LOCK RLockx = (PST_REENTRANT_LOCK)RLock;
    
    //
    // Acquire the associated non-reentrant lock.
    //

    do {
        if (StLock_Enter(&RLockx->Lock)) {
            break;
        }
        Sleep(500);
    } while (TRUE);

    _ASSERTE(RLockx->Owner == INVALID_THREAD_ID);
        
    //
    // Set the owner thread and restore the previous recursive
    // acquisition counter.
    //
        
    RLockx->Owner = GetCurrentThreadId();
    RLockx->Count = PreviousCount;
}

//
// Enqueues a wait block in the non-reentrant lock's wait list.
//

VOID
FASTCALL
EnqueueLockWaiter (
    __inout PVOID Lock,
    __inout PWAIT_BLOCK WaitBlock
    );

//
// Enqueues the specified wait block in the associated exclusive
// lock's wait list.
//
// NOTE: When this method is called, we know that the reentrant
//		 lock is owned by the current thread.
//

static
VOID
FASTCALL
_EnqueueWaiter (
    __inout PVOID RLock,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    PST_REENTRANT_LOCK RLockx = (PST_REENTRANT_LOCK)RLock;

    _ASSERTE(RLockx->Owner == GetCurrentThreadId());
    EnqueueLockWaiter(&RLockx->Lock, WaitBlock);
}

//
// Virtual function table to used by the reentrant lock.
//

ILOCK_VTBL ILockVtblForReentrantLock = {
    _IsOwned,
    _ReleaseCompletely,
    _Reacquire,
    _EnqueueWaiter
};
