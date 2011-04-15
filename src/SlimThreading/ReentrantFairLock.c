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
// Tries to acquire the reentrant fair lock if it is free or
// is owned by the current thread.
// 

static
FORCEINLINE
BOOLEAN
TryAcquireInternal (
    __inout PST_REENTRANT_FAIR_LOCK RLock,
    __out PULONG ThreadId
    )
{
    *ThreadId = GetCurrentThreadId();
    if (TryAcquireFairLockInternal(&RLock->Lock)) {
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
// Tries to acquire the reentrant fair lock, activating the
// specified cancellers. 
//

ULONG
WINAPI
StReentrantFairLock_TryEnterEx (
    __inout PST_REENTRANT_FAIR_LOCK RLock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ULONG ThreadId;
    ULONG WaitStatus;

    //
    // Try to acquire the lock if it is free or already owned
    // by the current thread.
    //

    if (TryAcquireInternal(RLock, &ThreadId)) {
        return WAIT_SUCCESS;
    }

    //
    // The lock is busy; so return failure if a null timeout
    // was specified.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }

    //
    // Try to acquire the associated busy non-reentrant fair lock.
    //

    if ((WaitStatus = FairLock_TryEnterTail(&RLock->Lock, Timeout, Alerter)) == WAIT_SUCCESS) {
        RLock->Owner = ThreadId;
        return WAIT_SUCCESS;
    }
    return WaitStatus;
}

//
// Tries to acquire the reentrant fair lock immediately. 
//

BOOL
WINAPI
StReentrantFairLock_TryEnter (
    __inout PST_REENTRANT_FAIR_LOCK RLock
    )
{
    ULONG ThreadId;

    //
    // Try to acquire the lock if it is free or already owned
    // by the current thread and return the result.
    //

    return TryAcquireInternal(RLock, &ThreadId);
}

//
// Acquire the reentrant fair lock unconditionally. 
//

BOOL
WINAPI
StReentrantFairLock_Enter (
    __inout PST_REENTRANT_FAIR_LOCK RLock
    )
{
    ULONG ThreadId;

    //
    // Try to acquire the lock if it is free or already owned
    // by the current thread.
    //

    if (TryAcquireInternal(RLock, &ThreadId)) {
        return TRUE;
    }

    //
    // Acquires unconditionally the associated busy non-reentrant
    // fair lock.
    //

    if (FairLock_TryEnterTail(&RLock->Lock, INFINITE, NULL) == WAIT_SUCCESS) {
        RLock->Owner = ThreadId;
        return TRUE;
    }
    return FALSE; 
}

//
// Exits the reentrant fair lock.
//

BOOL
WINAPI
StReentrantFairLock_Exit (
    __inout PST_REENTRANT_FAIR_LOCK RLock
    )
{
    if (RLock->Owner != GetCurrentThreadId()) {
        _ASSERTE(!"The reentrant lock isn't owned by the current thread");
        SetLastError(ERROR_NOT_OWNER);
        return FALSE;
    }

    //
    // If this is the last release, release the associated
    // non-reentrant fair lock; otherwise, decrement the recursive
    // acquisition counter.
    //

    if (RLock->Count == 0) {
        RLock->Owner = INVALID_THREAD_ID;
        StFairLock_Exit(&RLock->Lock);
    } else {
        RLock->Count--;
    }
    return TRUE;
}

/*++
 *
 * Virtual functions used by the condition variables.
 *
 --*/

//
// Returns true if the reentrant fair lock is owned by
// the current thread.
//

static
BOOLEAN
FASTCALL
_IsOwned (
    __in PVOID Lock
    )
{
    return (((PST_REENTRANT_FAIR_LOCK)Lock)->Owner == GetCurrentThreadId());
}

//
// Leaves completely the reentrant fair lock, saving its state.
//

static
LONG
FASTCALL
_ReleaseCompletely (
    __inout PVOID Lock
    )
{
    LONG PreviousState;
    PST_REENTRANT_FAIR_LOCK RLock = (PST_REENTRANT_FAIR_LOCK)Lock;

    //
    // Save the current state of the lock.
    //

    PreviousState = RLock->Count;
    
    //
    // Release the associated non-reentrant fair lock and
    // return the previous lock state.
    //
        
    RLock->Count = 0;
    RLock->Owner = INVALID_THREAD_ID;
    StFairLock_Exit(&RLock->Lock);
    return PreviousState;
}

//
// Reenters the reentrant fair lock and restores its previous state.
//

static
VOID
FASTCALL
_Reacquire (
    __inout PVOID Lock,
    __in ULONG WaitStatus,
    __in LONG PreviousState
    )
{
    
    PST_REENTRANT_FAIR_LOCK RLock = (PST_REENTRANT_FAIR_LOCK)Lock;

    //
    // If the wait on condition variable was cancelled, the lock
    // isn't owned by the current thread, so acquire it;
    // otherwise, the current thread already owns the lock, but
    // the *Owner* field isn't set.
    // Anyway, restore the previous recursive acquisition counter.
    //

    if (WaitStatus != WAIT_SUCCESS) {
        do {
            if (StReentrantFairLock_Enter(RLock)) {
                break;
            }
            Sleep(500);
        } while (TRUE);
    } else {
        _ASSERTE(RLock->Owner == INVALID_THREAD_ID);
        RLock->Owner = GetCurrentThreadId();
    }
    RLock->Count = PreviousState;
}

//
// Enqueues the specified wait block in the lock's queue.
// 
// NOTE: When this function is called, we know that the lock
//		 is owned by the current thread as mandates the monitor
//		 Notify's semantics.
//

static
VOID
FASTCALL
_EnqueueWaiter (
    __inout PVOID Lock,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    extern ILOCK_VTBL ILockVtblForFairLock;

    PST_REENTRANT_FAIR_LOCK RLock = (PST_REENTRANT_FAIR_LOCK)Lock;

    _ASSERTE(RLock->Owner == GetCurrentThreadId());
    (*ILockVtblForFairLock.EnqueueWaiter)(&RLock->Lock, WaitBlock);
}

//
// Virtual function table used by the condition variables.
//

ILOCK_VTBL ILockVtblForReentrantFairLock = {
    _IsOwned,
    _ReleaseCompletely,
    _Reacquire,
    _EnqueueWaiter
};

/*++
 *
 * Virtual functions that support the Waitable functionality.
 *
 --*/

//
// Returns true if the reentrant fair lock can be immediately
// acquired by the current thread.
//

static
BOOLEAN
FASTCALL
_AllowsAcquire (
    __in PST_WAITABLE Waitable
    )
{
    PST_REENTRANT_FAIR_LOCK RLock = (PST_REENTRANT_FAIR_LOCK)Waitable;

    return (RLock->Owner == GetCurrentThreadId() || ALLOWS_ACQUIRE(&RLock->Lock.Waitable));
}

//
// Tries to acquire the reentrant fair lock.
//

static
BOOLEAN
FASTCALL
_TryAcquire (
    __inout PST_WAITABLE Waitable
    )
{
    ULONG ThreadId;
    return TryAcquireInternal((PST_REENTRANT_FAIR_LOCK)Waitable, &ThreadId);
}

//
// Releases the reentrant fair lock if it is owned by the current
// thread; otherwise, does nothing and returns failure.
//

static
BOOLEAN
FASTCALL
_Release (
    __inout PST_WAITABLE Waitable
    )
{
    PST_REENTRANT_FAIR_LOCK RLock = (PST_REENTRANT_FAIR_LOCK)Waitable;

    if (RLock->Owner != GetCurrentThreadId()) {
        SetLastError(ERROR_NOT_OWNER);
        return FALSE;
    }

    StReentrantFairLock_Exit(RLock);
    return TRUE;
}

//
// Executes the WaitAny prologue.
//

static
BOOLEAN
FASTCALL
_WaitAnyPrologue (
    __inout PST_WAITABLE Waitable,
    __inout PST_PARKER Parker,
    __out PWAIT_BLOCK WaitBlock,
    __in ULONG WaitKey,
    __out PULONG SpinCount
    )
{
    ULONG ThreadId;
    PST_REENTRANT_FAIR_LOCK RLock = (PST_REENTRANT_FAIR_LOCK)Waitable;

    //
    // If the reentrant fair lock is free, try to acquire it.
    // if succeed, try to self lock the parker and, if
    // succeed, self unpark the current thread.
    //

    if (TryAcquireInternal(RLock, &ThreadId)) {
        if (TryLockParker(Parker)) {
            UnparkSelf(Parker, WaitKey);
        } else {

            //
            // The parker is already locked because the wait-any
            // operation was satisfied on another waitable; so, release
            // the lock, undoing the previous acquire.
            //

            StReentrantFairLock_Exit(RLock);
        }

        //
        // Return true to signal that the wait-any operation was
        // already satisfied.
        //

        return TRUE;
    }

    //
    // Since that the lock is busy, execute the WaitAny prologue on
    // the associated non-reentrant fair lock and return its results.
    //

    return WAIT_ANY_PROLOGUE(&RLock->Lock.Waitable, Parker, WaitBlock, WaitKey, SpinCount);
}

//
// Executes the WaitAll prologue.
//

static
BOOLEAN
FASTCALL
_WaitAllPrologue (
    __inout PST_WAITABLE Waitable,
    __inout PST_PARKER Parker,
    __out PWAIT_BLOCK WaitBlock,
    __out PULONG SpinCount
    )
{
    PST_REENTRANT_FAIR_LOCK RLock = (PST_REENTRANT_FAIR_LOCK)Waitable;

    //
    // If the lock can be immediately acquired by the current thread,
    // lock the parker and, if this is the last cooperative
    // release, self unpark the current thread .
    //

    if (_AllowsAcquire(&RLock->Waitable))  {
        if (TryLockParker(Parker)) {
            UnparkSelf(Parker, WAIT_STATE_CHANGED);
        }

        //
        // Mark the wait block as unlinked and return true.
        //

        WaitBlock->WaitListEntry.Flink = &WaitBlock->WaitListEntry;
        return TRUE;
    }

    //
    // Since that the lock can't be immediately acquired, execute the
    // WaitAll prologue on the associated non-reentrant fair lock and
    // return its results.
    //

    return WAIT_ALL_PROLOGUE(&RLock->Lock.Waitable, Parker, WaitBlock, SpinCount);
}

//
// Sets the lock's owner field.
//

static
VOID
FASTCALL
_WaitEpilogue (
    __inout PST_WAITABLE Waitable
    )
{
    PST_REENTRANT_FAIR_LOCK RLock = (PST_REENTRANT_FAIR_LOCK)Waitable;

    _ASSERTE(RLock->Owner == INVALID_THREAD_ID || RLock->Owner == GetCurrentThreadId());
    RLock->Owner = GetCurrentThreadId();
}

//
// Releases the lock, undoing a previous acquire.
//

static
VOID
FASTCALL
_UndoAcquire (
    __inout PST_WAITABLE Waitable
    )
{
    StReentrantFairLock_Exit((PST_REENTRANT_FAIR_LOCK)Waitable);
}

//
// Cancels the waitable acquire attempt.
//

static
VOID
FASTCALL
_CancelAcquire (
    __inout PST_WAITABLE Waitable,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    
    //
    // Cancels the acquire attempt on the associated non-reentrant
    // fair lock.
    //

    CANCEL_ACQUIRE(&((PST_REENTRANT_FAIR_LOCK)Waitable)->Lock.Waitable, WaitBlock);
}

//
// Virtual function table for the reentrant fair lock.
//

WAITABLE_VTBL ReentrantFairLockVtbl = {
    _AllowsAcquire,
    _TryAcquire,
    _Release,
    _WaitAnyPrologue,	
    _WaitAllPrologue,
    _WaitEpilogue,
    _UndoAcquire,
    _CancelAcquire
};

//
// Initializes the reentrant fair lock.
//

VOID
WINAPI
StReentrantFairLock_Init (
    __out PST_REENTRANT_FAIR_LOCK RLock,
    __in ULONG SpinCount
    )
{
    RLock->Waitable.Vptr = &ReentrantFairLockVtbl;
    StFairLock_Init(&RLock->Lock, SpinCount);
    RLock->Owner = INVALID_THREAD_ID;
    RLock->Count = 0;
}
