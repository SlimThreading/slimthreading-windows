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
// The value of request used by acquire operations.
//

#define ACQUIRE	1

//
// Tries to acquire the event on behalf of the current thread.
// 

static
FORCEINLINE
BOOLEAN
TryAcquireInternal (
    __inout PST_SYNCHRONIZATION_EVENT Event
    )
{
    do {
        if (Event->State == 0 || !IsLockedQueueEmpty(&Event->Queue)) {
            return FALSE;
        }
        if (CasLong(&Event->State, 1, 0)) {
            return TRUE;
        }
    } while (TRUE);
}

//
// Tries to acquire the event on behalf of the thread that is
// at the front of the event's queue.
//

static
FORCEINLINE
BOOLEAN
TryAcquireInternalQueued (
    __inout PST_SYNCHRONIZATION_EVENT Event
    )
{
    return (Event->State == 1 && CasLong(&Event->State, 1, 0));
}

//
// Sets the specified event, but only if it is non-signalled.
//

static
FORCEINLINE
BOOLEAN
SetInternal (
    __inout PST_SYNCHRONIZATION_EVENT Event
    )
{
    return (Event->State == 0 && CasLong(&Event->State, 0, 1));
}

//
// Returns true with the event's queue locked if any
// waiter can be release; otherwise, returns false with
// the event's queue unlocked.
//

static
FORCEINLINE
BOOLEAN
IsReleasePending (
    __inout PST_SYNCHRONIZATION_EVENT Event
    )
{

    return (Event->Queue.FrontRequest != 0 && Event->State != 0 &&
            TryLockLockedQueue(&Event->Queue));
}

//
// Releases the appropriate waiters and unlocks the
// event's queue.
//

static
BOOLEAN
FASTCALL
ReleaseWaitersAndUnlockQueue (
    __inout PST_SYNCHRONIZATION_EVENT Event,
    __in BOOLEAN TrySet
    )
{
    PLIST_ENTRY_ Entry;
    PLIST_ENTRY_ ListHead;
    PWAIT_BLOCK WaitBlock;
    PST_PARKER Parker;
    BOOLEAN TrySetUsed;

    //
    // Initialize the local variables.
    //

    ListHead = &Event->Queue.Head;
    TrySetUsed = FALSE;

    do {
        while ((Event->State != 0 || TrySet) && (Entry = ListHead->Flink) != ListHead) {

            //
            // Get the wait block that is at the front of the event's queue.
            //

            WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            Parker = WaitBlock->Parker;

            //
            // Try to satisfy the request of the waiter that is at the
            // front of the queue.
            //
            
            if (WaitBlock->WaitType == WaitAny) {

                //
                // Try to acquire the event on behalf of the thread that is at
                // front of the queue. First, we consider the TrySet flag and,
                // then, the event' state.
                //

                if (TrySet || TryAcquireInternalQueued(Event)) {
                    if (TrySet) {
                        TrySet = FALSE;
                        TrySetUsed = TRUE;
                    }

                    //
                    // Remove the wait block from the event's queue and try to
                    // lock the associated parker; if succeed, unpark the
                    // waiter thread.
                    //

                    RemoveEntryList(Entry);
                    if (TryLockParker(Parker)) {
                        UnparkThread(Parker, WaitBlock->WaitKey);
                    } else {
                
                        //
                        // The request was cancelled; so, undo the previous acquire
                        // and mark the wait block as unlinked.
                        // 

                        if (TrySetUsed ||
                            Event->State == 1 || !CasLong(&Event->State, 0, 1)) {
                            TrySet = TRUE;
                            TrySetUsed = FALSE;
                        }

                        Entry->Flink = Entry;
                    }
                } else {

                    //
                    // The event is non-signalled; so, break the inner loop.
                    //

                    break;
                }
            } else {

                //
                // Wait-all.
                // Since that the event seems signalled, remove the wait block
                // from the event's queue and lock the associated parker;
                // if this is the last cooperative release, unpark the waiter
                // thread; anyway, mark the wait block as unlinked.
                //

                RemoveEntryList(Entry);
                if (TryLockParker(Parker)) {
                    UnparkThread(Parker, WaitBlock->WaitKey);
                }
                Entry->Flink = Entry;
            }
        }
        
        //
        // It seems that no more waiters can be released, so try to
        // unlock the event's queue.
        //
        // NOTE: The unlock fails if there are waiters in the lock's
        //		 wait list and the event is signalled.
        //

        if (!TryUnlockLockedQueue(&Event->Queue, (Event->State == 0 && !TrySet))) {

            //
            // It seems that more waiters can be release, so recheck the
            // event's state and queue.
            //

            continue;
        }

        //
        // If the TrySet flag is still true and the event is non-signalled,
        // we try to signal the event; if we fail, the set operation is
        // ignored.
        //

        if (TrySet && Event->State == 0 && CasLong(&Event->State, 0, 1)) {
            TrySet = FALSE;
            TrySetUsed = TRUE;
        }

        //
        // If now we detect that any more waiter can be released,
        // we must repeat the release process.
        //

        if (!IsReleasePending(Event)) {
            break;
        }
    } while (TRUE);
        
    //
    // If the TrySet flag was true, return the previous state of the
    // event; this is, if we used the TrySet to wake up one waiter,
    // return false; otherwise, return true;
    //

    return !TrySetUsed;
}

//
// Cancels the specified event acquire attempt.
//

static
FORCEINLINE
VOID
CancelAcquire (
    __inout PST_SYNCHRONIZATION_EVENT Event,
    __inout PLIST_ENTRY_ Entry
    )
{

    if (Entry->Flink != Entry && LockLockedQueue(&Event->Queue, Entry)) {
        if (Entry->Flink != Entry) {
            RemoveEntryList(Entry);
        }
        ReleaseWaitersAndUnlockQueue(Event, FALSE);
    }
}

//
// Enqueues an wait attempt.
//

static
FORCEINLINE
ULONG
EnqueueWait (
    __inout PST_SYNCHRONIZATION_EVENT Event,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    BOOLEAN FrontHint;

    if (!EnqueueInLockedQueue(&Event->Queue, WaitBlock, &FrontHint)) {
        goto Exit;
    }
    if (!FrontHint || Event->State == 0) {

        //
        // The current thread must wait. So, unlock the event's
        // queue and return if there aren't releases pending.
        //

        TryUnlockLockedQueue(&Event->Queue, TRUE);
        if (!IsReleasePending(Event)) {
            goto Exit;
        }
    }

    //
    // There are pending releases, so execute the release
    // process and unlock the event's queue.
    //

    ReleaseWaitersAndUnlockQueue(Event, FALSE);

    //
    // Return the number of spin cycles.
    //

Exit:
    return FrontHint ? Event->SpinCount : 0;
}

//
// Executes the wait tail processing, blocking the current thread until
// the event is signalled or the wait is cancelled. 
//

FORCEINLINE
ULONG
WaitTail (
    __inout PST_SYNCHRONIZATION_EVENT Event,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    ULONG SpinCount;
    ULONG WaitStatus;

    //
    // Initialize a parker and a wait-any wait block and insert the
    // wait block in the event's queue.
    //

    InitializeParkerAndWaitBlock(&WaitBlock, &Parker, ACQUIRE, WaitAny, WAIT_SUCCESS);
    SpinCount = EnqueueWait(Event, &WaitBlock);
    
    //
    // Park the current thread.
    //
    
    WaitStatus = ParkThreadEx(&Parker, SpinCount, Timeout, Alerter);

    //
    // If we acquired the event, return success.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        return WAIT_SUCCESS;
    }
    
    //
    // The request was cancelled due to timeout or alert, so cancel
    // the acquire attempt and return the appropriate failure status.
    //

    CancelAcquire(Event, &WaitBlock.WaitListEntry);
    return WaitStatus;
}

//
// Waits until the event is signalled, activating the specified cancellers. 
//

ULONG
WINAPI
StSynchronizationEvent_WaitEx (
    __inout PST_SYNCHRONIZATION_EVENT Event,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{

    //
    // Try to acquire the event and, if succeed, return immediately.
    //

    if (TryAcquireInternal(Event)) {
        return WAIT_SUCCESS;
    }

    //
    // The current thread must wait; so return failure if a null
    // timeout was specified. Otherwise, execute the wait tail
    // processing.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }
    return WaitTail(Event, Timeout, Alerter);
}

//
// Waits unconditionally until the event is signalled. 
//

BOOL
WINAPI
StSynchronizationEvent_Wait (
    __inout PST_SYNCHRONIZATION_EVENT Event
    )
{
    return (TryAcquireInternal(Event) || WaitTail(Event, INFINITE, NULL) == WAIT_SUCCESS);
}

//
// Sets the specified event.
//

BOOL
WINAPI
StSynchronizationEvent_Set (
    __inout PST_SYNCHRONIZATION_EVENT Event
    )
{

    if (SetInternal(Event)) {

        //
        // The event was set to the signalled state. So, if there waiters,
        // try to lock the event's queue and, if we succeed, release
        // the appropriate waiters.
        //

        if (IsReleasePending(Event)) {
            ReleaseWaitersAndUnlockQueue(Event, FALSE);
        }

        //
        // Return the previous state of the event.
        //

        return FALSE;
    }

    //
    // The event is already set. In order to decide if this set
    // is ignored or not, we must lock event queue's and take
    // into account the waiters.
    //

    LockLockedQueue(&Event->Queue, NULL);
    return ReleaseWaitersAndUnlockQueue(Event, TRUE);
}

//
// Resets the specified event.
//

BOOL
WINAPI
StSynchronizationEvent_Reset (
    __inout PST_SYNCHRONIZATION_EVENT Event
    )
{
    
    LONG OldState;
    
    if ((OldState = Event->State) != 0) {
        Event->State = 0;
    }
    return OldState != 0;
}

//
// Returns the state of the specified event.
//

BOOL
WINAPI
StSynchronizationEvent_IsSet (
    __in PST_SYNCHRONIZATION_EVENT Event
    )
{
    LONG State = Event->State;
    _ReadBarrier();
    return (State != 0);
}

/*++
 *
 * Virtual functions that implements the Waitable functionality.
 *
 --*/

//
// Returns true if the event is signaled and its queue is empty.
//

static
BOOLEAN
FASTCALL
_AllowsAcquire (
    __in PST_WAITABLE Waitable
    )
{
    PST_SYNCHRONIZATION_EVENT Event = (PST_SYNCHRONIZATION_EVENT)Waitable;

    return (Event->State != 0 && IsLockedQueueEmpty(&Event->Queue));
}

//
// Tries to acquire the specified event.
//

static
BOOLEAN
FASTCALL
_TryAcquire (
    __inout PST_WAITABLE Waitable
    )
{
    return TryAcquireInternal((PST_SYNCHRONIZATION_EVENT)Waitable);
}

//
// Sets the specified event.
//

static
BOOLEAN
FASTCALL
_Release (
    __inout PST_WAITABLE Waitable
    )
{
    StSynchronizationEvent_Set((PST_SYNCHRONIZATION_EVENT)Waitable);
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
    PST_SYNCHRONIZATION_EVENT Event = (PST_SYNCHRONIZATION_EVENT)Waitable;

    //
    // Try to acquire the event.
    //

    if (TryAcquireInternal(Event)) {
        
        //
        // We acquired the event, so try to lock the parker
        // object and, if we succeed, call the unpark method.
        //

        if (TryLockParker(Parker)) {
            UnparkSelf(Parker, WaitKey);
        } else {

            //
            // The wait-any operation was satisfied by another
            // synchronizer, so we must undo the previous acquire.
            //

            StSynchronizationEvent_Set(Event);
        }

        //
        // Return true to signal that the wait-any operation
        // was satisfied without inserting the wait block in the
        // event's wait list.
        //

        return TRUE;
    }

    //
    // The event is non-signalled. Initialize the wait-any
    // wait block, insert it in the event's queue and return
    // false to signal that the wait block was enqueued.
    //

    InitializeWaitBlock(WaitBlock, Parker, ACQUIRE, WaitAny, WaitKey);
    *SpinCount = EnqueueWait(Event, WaitBlock);
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
    PST_SYNCHRONIZATION_EVENT Event = (PST_SYNCHRONIZATION_EVENT)Waitable;
 
    if (_AllowsAcquire(&Event->Waitable)) {
        
        //
        // The current thread can acquire the event; so, self locks
        // the parker and, if this is the last cooperative release,
        // self unpark the current thread.
        //

        if (TryLockParker(Parker)) {
            UnparkSelf(Parker, WAIT_STATE_CHANGED);
        }

        //
        // Mark the wait block as unlinked and return.
        //

        WaitBlock->WaitListEntry.Flink = &WaitBlock->WaitListEntry;
        return TRUE;
    }

    //
    // The event seems non-signalled. Initialize the wait-all
    // wait block, insert it in the event's queue and return
    // false to signal that the wait block was enqueued.
    //

    InitializeWaitBlock(WaitBlock, Parker, ACQUIRE, WaitAll, WAIT_STATE_CHANGED);
    *SpinCount = EnqueueWait(Event, WaitBlock);
    return FALSE;
}

//
// Undoes a previous waitable acquire operation.
//

static
VOID
FASTCALL
_UndoAcquire (
    __inout PST_WAITABLE Waitable
    )
{

    StSynchronizationEvent_Set((PST_SYNCHRONIZATION_EVENT)Waitable);
}

//
// Cancels the specified waitable acquire attempt.
//

static
VOID
FASTCALL
_CancelAcquire (
    __inout PST_WAITABLE Waitable,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    CancelAcquire((PST_SYNCHRONIZATION_EVENT)Waitable, &WaitBlock->WaitListEntry);
}


//
// Virtual function table of the synchronization event.
//

WAITABLE_VTBL SynchronizationEventVtbl = {
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
// Initializes the specified synchronization event.
//

VOID
WINAPI
StSynchronizationEvent_Init (
    __out PST_SYNCHRONIZATION_EVENT Event,
    __in BOOL InitialState,
    __in ULONG SpinCount
    )
{
    Event->Waitable.Vptr = &SynchronizationEventVtbl;
    InitializeLockedQueue(&Event->Queue);
    Event->State = (InitialState) ? 1 : 0;
    Event->SpinCount = IsMultiProcessor() ? SpinCount : 0;
}
