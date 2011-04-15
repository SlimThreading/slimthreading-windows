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
// The format of the event state field.
//

typedef union _EVENT_STATE {
    struct {
        volatile ULONG Lock : 1;
        volatile ULONG Set : 1;
    };
    volatile ULONG Bits : 2;
    volatile PLIST_ENTRY_ State;
} EVENT_STATE, *PEVENT_STATE;

#define SET_STATE				((PLIST_ENTRY_)((ULONG_PTR)(1 << 1)))
#define RESET_LOCKED_STATE		((PLIST_ENTRY_)((ULONG_PTR)(1 << 0)))

//
// Unparks the waiter thread owner of the specified list entry.
//

static
FORCEINLINE
VOID
UnparkListEntry (
    __inout PLIST_ENTRY_ Entry
    )
{

    PWAIT_BLOCK WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);

    if (WaitBlock->WaitType == WaitAny) {
        if (TryLockParker(WaitBlock->Parker)) {
            UnparkThread(WaitBlock->Parker, WaitBlock->WaitKey);
        } else {
            Entry->Flink = Entry;
        }
    } else {
        if (TryLockParker(WaitBlock->Parker)) {
            UnparkThread(WaitBlock->Parker, WaitBlock->WaitKey);
        }
        Entry->Flink = Entry;
    }
}

//
// Unparks all threads inserted in the specified wait list.
//

static
VOID
FASTCALL
UnparkWaitList(
    __in PST_NOTIFICATION_EVENT Event,
    __in PLIST_ENTRY_ List
    )
{
    PLIST_ENTRY_ Next;

    //
    // If the wait list is empty, return immediately.
    //

    if (List == NULL) {
        return;
    }
    
    //
    // If spinning is set and there are more than one thread in the
    // wait list, we unpark first the last thread, since that it is
    // the only one that spins.
    //
    
    if (Event->SpinCount != 0 && List->Flink != NULL) {
        PLIST_ENTRY_ Previous = List;
        while ((Next = Previous->Flink) != NULL && Next->Flink != NULL) {
            Previous = Next;
        }
        if (Next != NULL) {
            Previous->Flink = NULL;
            UnparkListEntry(Next);
        }
    }

    //
    // Unpark all thread inserted in the wait list.
    //

    do {
        Next = List->Flink;
        UnparkListEntry(List);
    } while ((List = Next) != NULL);
}

//
// Unlinks a list entry from the event's wait list - slow path.
//

static
VOID
FASTCALL
SlowUnlinkListEntry (
     __inout PST_NOTIFICATION_EVENT Event,
     __inout PLIST_ENTRY_ Entry
     )
{
    PLIST_ENTRY_ First;
    PLIST_ENTRY_ Last;
    PLIST_ENTRY_ Current;
    PLIST_ENTRY_ Next;
    EVENT_STATE State, NewState;
    SPIN_WAIT Spinner;

    InitializeSpinWait(&Spinner);
    do {

        State.State = Event->State;
        if (Entry->Flink == Entry) {
            return;
        }
        if (State.Lock == 0 && State.Set == 0 && State.State != NULL) {
            if (State.State == Entry && Entry->Flink == NULL) {
                if (CasPointer(&Event->State, State.State, NULL)) {
                    return;
                }
            } else if (CasPointer(&Event->State, State.State, RESET_LOCKED_STATE)) {
                break;
            }
        }
        SpinOnce(&Spinner);
    } while (TRUE);

    //
    // Remove all locked entries from the wait list, building a
    // new list with the non-locked entries.
    //

    First = Last = NULL;
    Current = State.State;
    while (Current != NULL) {
        Next = Current->Flink;
        if (IsParkerLocked(CONTAINING_RECORD(Current, WAIT_BLOCK, WaitListEntry)->Parker)) {

            //
            // Mark the locked entry as unlinked.
            //

            Current->Flink = Current;
        } else {

            //
            // Add the current entry to the new list.
            //

            if (First == NULL) {
                First = Current;
            } else {
                Last->Flink = Current;
            }
            Last = Current;
        }
        Current = Next;
    }

    //
    // Clear the lock bit, returning the non-cancelled wait blocks
    // to the event's wait list.
    //

    do {
        State.State = Event->State;
        if (State.Set != 0) {
            Event->State = SET_STATE;
            State.Bits = 0;
            if (First != NULL) {
                Last->Flink = State.State;
            } else {
                First = State.State;
            }
            UnparkWaitList(Event, First);
            break;
        } else {
            NewState.State = State.State;
            NewState.Lock = 0;
            if (First != NULL) {
                Last->Flink = NewState.State;
                NewState.State = First;
            }
            if (CasPointer(&Event->State, State.State, NewState.State)) {
                break;
            }
        }
    } while (TRUE);

    //
    // Since that it is possible that a thread acquires and releases
    // the lock bit without find its wait block in the event's wait list,
    // we must always check if the wait block was unlinked and spin if not.
    //

    while (Entry->Flink != Entry) {
        SpinOnce(&Spinner);
    }
    return;
}

//
// Unlinks a list entry from the event's wait list.
//

static
FORCEINLINE
VOID
UnlinkListEntry (
     __inout PST_NOTIFICATION_EVENT Event,
     __inout PLIST_ENTRY_ Entry
     )
{
    if (Entry->Flink == Entry ||
        (Event->State == Entry && Entry->Flink == NULL && CasPointer(&Event->State, Entry, NULL))) {
        return;
    }
    SlowUnlinkListEntry(Event, Entry);
}

//
// Executes the wait tail processing.
//

static
FORCEINLINE
ULONG
WaitTail (
    __inout PST_NOTIFICATION_EVENT Event,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    WAIT_BLOCK WaitBlock;
    ST_PARKER Parker;
    EVENT_STATE State, NewState;
    ULONG WaitStatus;

    //
    // Initialize a parker and a wait block to insert in the event's wait
    // list, but only if the event remains non-signalled.
    //

    InitializeParkerAndWaitBlock(&WaitBlock, &Parker, 0, WaitAny, WAIT_SUCCESS);
    do {

        State.State = Event->State;

        //
        // If, meanwhile, the event was signalled, return success.
        //
        
        if (State.Set != 0) {
            return WAIT_SUCCESS;
        }

        //
        // Try to insert our wait block in the event's wait list.
        //
        
        NewState.State = State.State;
        NewState.Lock = 0;
        WaitBlock.WaitListEntry.Flink = NewState.State;
        NewState.State = &WaitBlock.WaitListEntry;
        NewState.Lock = State.Lock;
        if (CasPointer(&Event->State, State.State, NewState.State)) {
            break;
        }
    } while (TRUE);

    //
    // Park the current thread.
    //

    WaitStatus = ParkThreadEx(&Parker, WaitBlock.WaitListEntry.Flink == NULL ? Event->SpinCount : 0,
                              Timeout, Alerter);

    if (WaitStatus == WAIT_SUCCESS) {
        return WAIT_SUCCESS;
    }

    //
    // The wait was cancelled. So, unlink the  wait block from the event's
    // wait list and return the appropriate failure status.
    //

    UnlinkListEntry(Event, &WaitBlock.WaitListEntry);
    return WaitStatus;
}

//
// Waits until the event is signalled, activating the specified
// cancellers.
//

ULONG
WINAPI
StNotificationEvent_WaitEx (
    __inout PST_NOTIFICATION_EVENT Event,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    EVENT_STATE State;

    //
    // If the event is already signalled, return success.
    //

    State.State = Event->State;

    if (State.Set != 0) {
        return WAIT_SUCCESS;
    }
    
    //
    // The event is non-signalled; so, if a zero timeout was specified,
    // return failure. Otherwise execute the wait tail processing.
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
StNotificationEvent_Wait (
    __inout PST_NOTIFICATION_EVENT Event
    )
{
    EVENT_STATE State;

    //
    // If the event is already signalled, return success. Otherwise, execute
    // the wait tail processing.
    //

    State.State = Event->State;
    if (State.Set != 0) {
        return WAIT_SUCCESS;
    }
    return WaitTail(Event, INFINITE, NULL) == WAIT_SUCCESS;
}

//
// Sets the event.
//

BOOL
WINAPI
StNotificationEvent_Set (
    __inout PST_NOTIFICATION_EVENT Event
    )
{
    EVENT_STATE State, NewState;	

    do {
        State.State = Event->State;
        if (State.Set != 0) {
            return TRUE;
        }
        if (State.Lock != 0) {
            NewState.State = State.State;
            NewState.Set = 1;
            if (CasPointer(&Event->State, State.State, NewState.State)) {
                return FALSE;
            }
        } else {
            if (CasPointer(&Event->State, State.State, SET_STATE)) {
                break;
            }
        }
    } while (TRUE);

    //
    // Unpark all threads eventually waiting on the event and
    // return its previous state.
    //

    UnparkWaitList(Event, State.State);
    return FALSE;
}

//
// Resets the event.
//

BOOL
WINAPI
StNotificationEvent_Reset (
    __inout PST_NOTIFICATION_EVENT Event
    )
{
    EVENT_STATE State;
    SPIN_WAIT Spinner;

    InitializeSpinWait(&Spinner);
    do {
        State.State = Event->State;
        if (State.Set == 0) {
            return FALSE;
        }
        if (State.Lock == 0 && CasPointer(&Event->State, SET_STATE, NULL)) {
            return TRUE;
        }
        SpinOnce(&Spinner);
    } while (TRUE);
}

//
// Returns the state of the specified event.
//

BOOL
WINAPI
StNotificationEvent_IsSet (
    __in PST_NOTIFICATION_EVENT Event
    )
{
    EVENT_STATE State;
    State.State = Event->State;
    _ReadBarrier();
    return (State.Set != 0);
}

/*++
 *
 * Virtual functions that support the Waitable functionality.
 *
 --*/

//
// Tries the waitable acquire operation.
// NOTE: This function is also used to implement the _AllowsAcquire
//		 virtual function.
//

static
BOOLEAN
FASTCALL
_TryAcquire (
    __in PST_WAITABLE Waitable
    )
{

    EVENT_STATE State;

    State.State = ((PST_NOTIFICATION_EVENT)Waitable)->State;
    return (State.Set != 0);
}

//
// Sets the event to the signalled state.
//

static
BOOLEAN
FASTCALL
_Release (
    __inout PST_WAITABLE Waitable
    )
{

    StNotificationEvent_Set((PST_NOTIFICATION_EVENT)Waitable);
    return TRUE;
}

//
// Executes the WaitXxx prologue.
//

static
FORCEINLINE
BOOLEAN
WaitXxxPrologue (
    __inout PST_WAITABLE Waitable,
    __inout PST_PARKER Parker,
    __out PWAIT_BLOCK WaitBlock,
    __in WAIT_TYPE WaitType, 
    __in ULONG WaitKey,
    __out_opt PULONG SpinCount
    )
{
    PST_NOTIFICATION_EVENT Event;
    EVENT_STATE State, NewState;

    Event = (PST_NOTIFICATION_EVENT)Waitable;

    //
    // Mark the wait block as uninitialized.
    //

    WaitBlock->Parker = NULL;

    do {

        State.State = Event->State;
        if (State.Set != 0) {

            //
            // The event is already signalled, so try to self lock the parker and,
            // if succeed, self unpark the current thread.
            //
             
            if (TryLockParker(Parker)) {
                UnparkSelf(Parker, WaitKey);
            }

            //
            // Return true to signal that the wait-any operation
            // was already satisfied.
            //

            WaitBlock->WaitListEntry.Flink = &WaitBlock->WaitListEntry;
            return TRUE;
        }

        //
        // The event is still non-signalled, so initialize the wait block
        // if it isn't still initialized and try to insert the wait block
        // in the event's wait list.
        //

        if (WaitBlock->Parker == NULL) {
            InitializeWaitBlock(WaitBlock, Parker, 0, WaitType, WaitKey);
        }

        NewState.State = State.State;
        NewState.Lock = 0;
        WaitBlock->WaitListEntry.Flink = NewState.State;
        NewState.State = &WaitBlock->WaitListEntry; 
        NewState.Lock = State.Lock;
        if (CasPointer(&Event->State, State.State, NewState.State)) {

            //
            // Return the spin count if the out argument was specified.
            //
            
            if (ARGUMENT_PRESENT(SpinCount)) {
                State.Lock = 0;
                *SpinCount = (State.State == NULL) ? Event->SpinCount : 0;	
            } 

            //
            // Return false to signal that the acquire-any wasn't satisfied.
            //

            return FALSE;
        }
    } while (TRUE);
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
    __out_opt PULONG SpinCount
    )
{
    return WaitXxxPrologue(Waitable, Parker, WaitBlock, WaitAny, WaitKey, SpinCount);
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
    __out_opt PULONG SpinCount
    )
{
    return WaitXxxPrologue(Waitable, Parker, WaitBlock, WaitAll, WAIT_STATE_CHANGED, SpinCount);
}

//
// Cancels the specified waitable acquire.
//

static
VOID
FASTCALL
_CancelAcquire (
    __inout PST_WAITABLE Waitable,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    UnlinkListEntry((PST_NOTIFICATION_EVENT)Waitable, &WaitBlock->WaitListEntry);
}

//
// Virtual function table for the notification event.
//

//static
WAITABLE_VTBL NotificationEventVtbl = {
    _TryAcquire,
    _TryAcquire,
    _Release,
    _WaitAnyPrologue,	
    _WaitAllPrologue,
    NULL,
    NULL,
    _CancelAcquire
};

//
// Initializes the specified notification event.
//

VOID
WINAPI
StNotificationEvent_Init (
    __out PST_NOTIFICATION_EVENT Event,
    __in BOOL InitialState,
    __in ULONG SpinCount
    )
{
    Event->Waitable.Vptr = &NotificationEventVtbl;
    Event->State = InitialState ? SET_STATE : NULL;
    Event->SpinCount = IsMultiProcessor() ? SpinCount : 0;
}
