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
// Tries to acquire the specified number of permits on behalf
// of the current thread.
// 

static
FORCEINLINE
BOOLEAN
TryAcquireInternal (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG AcquireCount
    )
{
    LONG State;
    LONG NewState;

    do {
        NewState = (State = Semaphore->State) - AcquireCount;
        if (NewState < 0 || !IsLockedQueueEmpty(&Semaphore->Queue)) {
            return FALSE;
        }
        if (CasLong(&Semaphore->State, State, NewState)) {
            return TRUE;
        }
    } while (TRUE);
}

//
// Tries to acquire the specified number of permits on behalf
// of the thread that is at front of the semaphore's queue.
//

static
FORCEINLINE
BOOLEAN
TryAcquireInternalQueued (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG AcquireCount
    )
{
    do {
        LONG State;
        LONG NewState = (State = Semaphore->State) - AcquireCount;
        if (NewState < 0) {
            return FALSE;
        }
        if (CasLong(&Semaphore->State, State, NewState)) {
            return TRUE;
        }
    } while (TRUE);
}

//
// Undoes a previous acquire.
//

static
FORCEINLINE
VOID
UndoAcquire (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG AcquireCount
    )
{
    do {
        LONG State = Semaphore->State;
        if (CasLong(&Semaphore->State, State, State + AcquireCount)) {
            return;
        }
    } while (TRUE);
}

//
// Releases the specified number of permits.
//

static
FORCEINLINE
BOOLEAN
ReleaseInternal (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG ReleaseCount
    )
{
    do {
        LONG State;
        LONG NewState = (State = Semaphore->State) + ReleaseCount;
        if (NewState < 0 || NewState > Semaphore->MaximumCount) {
            return FALSE;
        }
        if (CasLong(&Semaphore->State, State, NewState)) {
            return TRUE;
        }
    } while (TRUE);
}

//
// Returns true with the semaphore's queue locked if any
// waiter can be release; otherwise, returns false with
// the semaphore's queue unlocked.
//

static
FORCEINLINE
BOOLEAN
IsReleasePending (
    __inout PST_SEMAPHORE Semaphore
    )
{
    LONG FrontRequest = Semaphore->Queue.FrontRequest;
    return (FrontRequest != 0 && FrontRequest <= Semaphore->State &&
            TryLockLockedQueue(&Semaphore->Queue));
}

//
// Releases the appropriate waiter thraeds and unlocks the
// semaphore's queue.
//

static
VOID
FASTCALL
ReleaseWaitersAndUnlockQueue (
    __inout PST_SEMAPHORE Semaphore
    )
{
    PLIST_ENTRY_ Entry;
    PLIST_ENTRY_ ListHead;
    PWAIT_BLOCK WaitBlock;
    PST_PARKER Parker;
    LONG Request;

    ListHead = &Semaphore->Queue.Head;
    do {
        while (Semaphore->State > 0 && (Entry = ListHead->Flink) != ListHead) {

            WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            Parker = WaitBlock->Parker;

            if (WaitBlock->WaitType == WaitAny) {				
                Request = WaitBlock->Request & MAX_REQUEST;

                //
                // Try to acquire the requested number of permits on behalf
                // of the waiter thread. If we acquired the permits, remove the
                // wait block from the semaphore's queue and try to lock the
                // associated parker; if succeed, unpark the waiter thread.
                //

                if (TryAcquireInternalQueued(Semaphore, Request)) {
                    RemoveEntryList(Entry);
                    if (TryLockParker(Parker) || WaitBlock->Request < 0) {
                        UnparkThread(Parker, WaitBlock->WaitKey);
                    } else {
                
                        //
                        // The request was cancelled; so, mark the wait block as
                        // unlinked and undo the previous acquire.
                        //

                        Entry->Flink = Entry;
                        UndoAcquire(Semaphore, Request);
                    }
                } else {
                    
                    //
                    // The request of the thread that is at front of the queue
                    // can't be satisfied; so, exit the realease loop.
                    //

                    break;
                }
            } else {

                //
                // Wait-all.
                // Since that the semaphore seems to have has at least one
                // available permit, remove the wait block from the semaphore's
                // queue and lock associated parker; if this is the last
                // cooperative release, unpark the waiter thread; anyway, mark
                // the wait block as unlinked.
                //

                RemoveEntryList(Entry);
                if (TryLockParker(Parker)) {
                    UnparkThread(Parker, WaitBlock->WaitKey);
                }
                Entry->Flink = Entry;
            }
        }

        //
        // It seems that no more waiters can be released, so  unlock
        // the semaphore's queue. The unlock fails, the number of available
        // permits isn't zero, new waiters arrived when the queue was locked
        // and the semaphore's queue is empty.
        //

        if (!TryUnlockLockedQueue(&Semaphore->Queue, Semaphore->State == 0)) {

            //
            // It seems that more waiters can be release; so, repeat the
            // release processing.
            //

            continue;
        }

        //
        // If any waiter can now be released, repeat the release
        // processing.
        //

        if (!IsReleasePending(Semaphore)) {
            return;
        }
    } while (TRUE);
}

//
// Cancels the specified acquire attempt.
//

static
FORCEINLINE
VOID
CancelAcquire (
    __inout PST_SEMAPHORE Semaphore,
    __inout PLIST_ENTRY_ Entry
    )
{

    if (Entry->Flink != Entry &&  LockLockedQueue(&Semaphore->Queue, Entry)) {
        if (Entry->Flink != Entry) {
            RemoveEntryList(Entry);
        }
        ReleaseWaitersAndUnlockQueue(Semaphore);
    }
}

//
// Enqueues an acquire attempt.
//

static
FORCEINLINE
ULONG
EnqueueAcquire (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG AcquireCount,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    BOOLEAN FrontHint;

    //
    // Insert the wait block in the semaphore's queue. If the queue is
    // locked, the enqueue remains pending and, therefore, the current
    // thread will go directly to wait.
    //

    if (!EnqueueInLockedQueue(&Semaphore->Queue, WaitBlock, &FrontHint)) {
        goto Exit;
    }
    if (AcquireCount > Semaphore->State || !FrontHint) {

        //
        // The current thread must wait. So, unlock the semaphore's
        // queue and return, if no waiter must be released.
        //

        TryUnlockLockedQueue(&Semaphore->Queue, TRUE);
        if (!IsReleasePending(Semaphore)) {
            goto Exit;
        }
    }

    //
    // It seems that the first waiter (including ourselves) can be
    // released, so, execute the release processing.
    //

    ReleaseWaitersAndUnlockQueue(Semaphore);

    //
    // Return the number of spin cycles for the current thread.
    //

Exit:
    return FrontHint ? Semaphore->SpinCount : 0;
}

//
// Executes the acquire tail processing. 
//

static
FORCEINLINE
ULONG
AcquireTail (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG AcquireCount,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    ULONG SpinCount;
    ULONG WaitStatus;

    //
    // Initialize a parker and a wait block and insert the wait block 
    // in the semaphore's queue.
    //

    InitializeParkerAndWaitBlock(&WaitBlock, &Parker, AcquireCount, WaitAny, WAIT_SUCCESS);
    SpinCount = EnqueueAcquire(Semaphore, AcquireCount, &WaitBlock);
    
    //
    // Park the current thread, activating the specified cancellers.
    //
    
    if ((WaitStatus = ParkThreadEx(&Parker, SpinCount, Timeout, Alerter)) == WAIT_SUCCESS) {
        return WAIT_SUCCESS;
    }
    
    //
    // The request was cancelled due to timeout or alert; so, cancel
    // the acquire attempt and return the appropriate failure status.
    //

    CancelAcquire(Semaphore, &WaitBlock.WaitListEntry);
    return WaitStatus;
}


//
// Tries to acquire the specified permits on the semaphore,
// activating the specified alerters. 
//

ULONG
WINAPI
StSemaphore_TryAcquireEx (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG AcquireCount,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    //
    // Validate the acquire count argument.
    //

    if (AcquireCount <= 0 || AcquireCount > Semaphore->MaximumCount ||
        AcquireCount > MAX_REQUEST) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }

    //
    // Try to acquire the requested permits; if succeed, return success.
    //

    if (TryAcquireInternal(Semaphore, AcquireCount)) {
        return WAIT_SUCCESS;
    }

    //
    // There aren't avaialable permits; so, return failure if a null
    // timeout was specified; otherwise, execute the acquire tail
    // processing.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }
    return AcquireTail(Semaphore, AcquireCount, Timeout, Alerter);
}

//
// Tries to acquires the specified permits on the semaphore immediately. 
//

BOOL
WINAPI
StSemaphore_TryAcquire (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG AcquireCount
    )
{
    //
    // Validate the acquire count argument.
    //

    if (AcquireCount <= 0 || AcquireCount > Semaphore->MaximumCount ||
        AcquireCount > MAX_REQUEST) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    //
    // Try to acquire the requested permits; if succeed, return success.
    // Othrewise, execute the acquire tail processing.
    //

    return TryAcquireInternal(Semaphore, AcquireCount);
}

//
// Acquires unconditionally the specified permits on the semaphore. 
//

BOOL
WINAPI
StSemaphore_Acquire (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG AcquireCount
    )
{

    //
    // Validate the acquire count argument.
    //

    if (AcquireCount <= 0 || AcquireCount > Semaphore->MaximumCount ||
        AcquireCount > MAX_REQUEST) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    //
    // Acquire unconditionally the requested permits.
    //

    return (TryAcquireInternal(Semaphore, AcquireCount) ||
            AcquireTail(Semaphore, AcquireCount, INFINITE, NULL) == WAIT_SUCCESS);
}

//
// Releases the specified number of permits.
//

BOOL
WINAPI
StSemaphore_Release (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG ReleaseCount
    )
{

    //
    // Update the semaphore's state.
    //

    if (!ReleaseInternal(Semaphore, ReleaseCount)) {
        SetLastError(ERROR_TOO_MANY_POSTS);
        return FALSE;
    }

    //
    // If the first waiter can be released, execute the release processing.
    //

    if (IsReleasePending(Semaphore)) {
        ReleaseWaitersAndUnlockQueue(Semaphore);
    }
    return TRUE;
}

//
// Returns the number of available permits on the semaphore.
//

LONG
WINAPI
StSemaphore_Read (
    __in PST_SEMAPHORE Semaphore
    )
{
    LONG State = Semaphore->State;
    _ReadBarrier();
    return State;
}


/*++
 *
 * Virtual functions that implements the Waitable functionality.
 *
 --*/

//
// Returns true if there is at least a permite available and
// the semaphore's queue is empty.
//

static
BOOLEAN
FASTCALL
_AllowsAcquire (
    __in PST_WAITABLE Waitable
    )
{
    PST_SEMAPHORE Semaphore = (PST_SEMAPHORE)Waitable;

    return (Semaphore->State != 0 && IsLockedQueueEmpty(&Semaphore->Queue));
}

//
// Tries to acquire one permit immediately.
//

static
BOOLEAN
FASTCALL
_TryAcquire (
    __inout PST_WAITABLE Waitable
    )
{
    return TryAcquireInternal((PST_SEMAPHORE)Waitable, 1);
}

//
// Releases one permit on the specified semaphore.
//

static
BOOLEAN
FASTCALL
_Release (
    __inout PST_WAITABLE Waitable
    )
{
    return StSemaphore_Release((PST_SEMAPHORE)Waitable, 1);
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
    PST_SEMAPHORE Semaphore = (PST_SEMAPHORE)Waitable;

    //
    // Try to acquire one permit on the semaphore; if we acquired,
    // try to lock the parker and, if succeed, self unpark the current
    // thread.
    //

    if (TryAcquireInternal(Semaphore, 1)) {
        if (TryLockParker(Parker)) {
            UnparkSelf(Parker, WaitKey);
        } else {

            //
            // The wait-any operation was satisfied by another waitable;
            // so, undo the previous acquire.
            //

            UndoAcquire(Semaphore, 1);
            if (IsReleasePending(Semaphore)) {
                ReleaseWaitersAndUnlockQueue(Semaphore);
            }
        }

        //
        // Return true to signal that the wait-any operation was
        // accomplished.
        //

        return TRUE;
    }

    //
    // Initialize the wait-any wait block, insert it in the
    // semaphore's queue and return false to report that the
    // wait block was inserted in the semaphore's queue.
    //

    InitializeWaitBlock(WaitBlock, Parker, 1, WaitAny, WaitKey);
    *SpinCount = EnqueueAcquire(Semaphore, 1, WaitBlock);
    return FALSE;
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
    PST_SEMAPHORE Semaphore = (PST_SEMAPHORE)Waitable;
 
    if (_AllowsAcquire(&Semaphore->Waitable)) {
        
        //
        // The current thread can acquire one permite from the semaphore.
        // So self lock the parker and, if this is the last
        // cooperative release, unparks the current thread.
        //

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
    // Initialize the wait-all wait block, insert it on the semaphore's
    // queue and return false to signal that a wait block was inserted
    // in the semaphore's queue.
    //

    InitializeWaitBlock(WaitBlock, Parker, 1, WaitAll, WAIT_STATE_CHANGED);
    *SpinCount = EnqueueAcquire(Semaphore, 1, WaitBlock);
    return FALSE;
}

//
// Undoes a previous acquire operation.
//

static
VOID
FASTCALL
_UndoAcquire (
    __inout PST_WAITABLE Waitable
    )
{
    PST_SEMAPHORE Semaphore = (PST_SEMAPHORE)Waitable;

    UndoAcquire(Semaphore, 1);
    if (IsReleasePending(Semaphore)) {
        ReleaseWaitersAndUnlockQueue(Semaphore);
    }
}

//
// Cancels the specified acquire attempt.
//

static
VOID
FASTCALL
_CancelAcquire (
    __inout PST_WAITABLE Waitable,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    CancelAcquire((PST_SEMAPHORE)Waitable, &WaitBlock->WaitListEntry);
}

//
// Waitable virtual function table for the semaphore.
//

WAITABLE_VTBL SemaphoreVtbl = {
    _AllowsAcquire,
    _TryAcquire,
    _Release,
    _WaitAnyPrologue,	
    _WaitAllPrologue,
    NULL,
    _UndoAcquire,
    _CancelAcquire
};

//
// Initializes the specified semaphore.
//

VOID
WINAPI
StSemaphore_Init (
    __out PST_SEMAPHORE Semaphore,
    __in LONG InitialCount,
    __in LONG MaximumCount,
    __in ULONG SpinCount
    )
{
    Semaphore->Waitable.Vptr = &SemaphoreVtbl;
    InitializeLockedQueue(&Semaphore->Queue);
    Semaphore->State = InitialCount;
    Semaphore->MaximumCount = MaximumCount;
    Semaphore->SpinCount = IsMultiProcessor() ? SpinCount : 0;
}
