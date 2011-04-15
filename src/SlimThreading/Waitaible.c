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
// Signals the specified Waitable.
//

BOOL
WINAPI
StWaitable_Signal (
    __inout PVOID Object
    )
{
    PST_WAITABLE Waitable = (PST_WAITABLE)Object;
    return RELEASE(Waitable);
}

//
// Waits until the Waitable is signalled, activating the specified
// cancellers.
//

ULONG
WINAPI
StWaitable_WaitOneEx (
    __in PVOID Object,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    ULONG WaitStatus;
    ULONG SpinCount;
    PST_WAITABLE Waitable = (PST_WAITABLE)Object;

    //
    // Try the immediate acquire and return if success.
    //

    if (TRY_ACQUIRE(Waitable)) {
        return WAIT_SUCCESS;
    }

    //
    // The immediate acquire isn't possible; so, if a null timeout was
    // specified, return failure.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }

    //
    // Initialize the parker and execute the WaitAny prologue.
    //

    InitializeParker(&Parker, 1);
    WAIT_ANY_PROLOGUE(Waitable, &Parker, &WaitBlock, WAIT_SUCCESS, &SpinCount);
    
    //
    // Park the current thread, activating the specified cancellers.
    //

    WaitStatus = ParkThreadEx(&Parker, SpinCount, Timeout, Alerter);

    //
    // If the acquire succeed, execute the wait-epilogue and
    // return success.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        WAIT_EPILOGUE(Waitable);
        return WAIT_SUCCESS;
    }

    //
    // The wait operation was cancelled due to timeout or alert.
    // So, cancel the acquire attempt and return the appropriate
    // failure status.
    //

    CANCEL_ACQUIRE(Waitable, &WaitBlock);
    return WaitStatus;
}

//
// Waits unconditionally until the Waitable is signalled.
//

BOOL
WINAPI
StWaitable_WaitOne (
    __in PVOID Object
    )
{
    return StWaitable_WaitOneEx(Object, INFINITE, NULL) == WAIT_SUCCESS;
}

//
// Waits until one of the specified Waitables is signalled,
// activating the specified cancellers.
//

ULONG
WINAPI
StWaitable_WaitAnyEx (
    __in ULONG Count,
    __in PVOID Objects[],
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlockArray[MAXIMUM_WAIT_OBJECTS];
    PST_WAITABLE Waitable;
    PST_WAITABLE AcquiredWaitable;
    ULONG Defined;
    LONG Index;
    ULONG GlobalSpinCount;
    ULONG SpinCount;
    LONG LastVisited;
    ULONG WaitStatus;

    //
    // Validate parameters.
    //
    // NOTE: We support null pointers on the *Objects* array, provided
    //		 that the array contains at least a non-null pointer.
    //

    if (Count > MAXIMUM_WAIT_OBJECTS) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }

    //
    // First, try the immediate acquire one the specified Waitables.
    //

    Defined = 0;
    for (Index = 0; Index < (LONG)Count; Index++) {
        Waitable = (PST_WAITABLE)Objects[Index];
        if (Waitable != NULL) {			
            if (TRY_ACQUIRE(Waitable)) {

                //
                // We acquired the current Waitable, so return the
                // appropriate wait status.
                //

                return (WAIT_OBJECT_0 + Index);
            }
            Defined++;
        }
    }

    //
    // If the *Objects* array doesn't contain any non-null
    // pointers, return failure.
    //

    if (Defined == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }

    //
    // Since that none of the Waitables allows an immediate acquire,
    // return failure, f a nul timeout was specified.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }
        
    //
    // Initialize the parker and execute the WaitAny prologue
    // on the specified Waitables.
    //

    InitializeParker(&Parker, 1);
    LastVisited = -1;
    GlobalSpinCount = 0;
    for (Index = 0; !IsParkerLocked(&Parker) && Index < (LONG)Count; Index++) {
        Waitable = (PST_WAITABLE)Objects[Index];
        if (Waitable != NULL) {

            //
            // Execute the WaitAny prologue for the current Waitable;
            // break the loop if the it was acquired.
            //

            if (WAIT_ANY_PROLOGUE(Waitable, &Parker, &WaitBlockArray[Index],
                                  WAIT_OBJECT_0 + Index, &SpinCount)) {
                break;
            }

            //
            // Adjust the global spin count.
            //

            if (SpinCount > GlobalSpinCount) {
                GlobalSpinCount = SpinCount;
            }
            LastVisited = Index;
        }
    }

    //
    // Park the current thread, activating the specified cancellers.
    //

    WaitStatus = ParkThreadEx(&Parker, GlobalSpinCount, Timeout, Alerter);
    
    //
    // If we got a successful wait status, compute the Waitable that
    // was acquire and execute the Wait epilogue on it.
    //

    AcquiredWaitable = NULL;
    if (WaitStatus >= WAIT_OBJECT_0 && WaitStatus < WAIT_OBJECT_0 + Count) {
        AcquiredWaitable = (PST_WAITABLE)(Objects[WaitStatus - WAIT_OBJECT_0]);
        WAIT_EPILOGUE(AcquiredWaitable);
    }

    //
    // Cancel the acquire attempt on the Waitable where we executed the
    // WaitAny prologue, except the one that we acquired.
    //

    for (Index = 0; Index <= LastVisited; Index++) {
        Waitable = (PST_WAITABLE)Objects[Index];
        if (Waitable != NULL && Waitable != AcquiredWaitable) {
            PWAIT_BLOCK WaitBlock = &WaitBlockArray[Index];
            if (WaitBlock->WaitListEntry.Flink != &WaitBlock->WaitListEntry) {
                CANCEL_ACQUIRE(Waitable, &WaitBlockArray[Index]);
            }
        }
    }

    //
    // Return the appropriate wait status.
    //

    return WaitStatus;
}

//
// Waits unconditionally until one of the specified Waitables
// is signalled.
//

ULONG
WINAPI
StWaitable_WaitAny (
    __in ULONG Count,
    __in PVOID Objects[]
    )
{
    return StWaitable_WaitAnyEx(Count, Objects, INFINITE, NULL);
}


//
// Sorts the specified Waitables by its address and at the
// same time checks if all of them allow the an immediate
// acquire.
//
// NOTE: The notification events are all moved to the end of
//		 sorted array, in spite of its addresses.
//

static
FORCEINLINE
LONG
SortAndCheckForAllowAcquire (
    __in PVOID Objects[],
    __out PST_WAITABLE SortedWaitables[],
    __in LONG Count
    )
{
    LONG i, j, jj, k;
    PST_WAITABLE Waitable;
    BOOLEAN CanAcquireAll;

    CanAcquireAll = TRUE;

    //
    // Find the first Waitable that isn't a notification event,
    // in order to start insertion sort.
    //

    jj = Count;
    for (i = 0; i < Count; i++) {
        Waitable = (PST_WAITABLE)Objects[i];
        
        //
        // Update the can acquire all flag.
        //

        CanAcquireAll &= ALLOWS_ACQUIRE(Waitable);

        //
        // If the current waitable is a notification event, insert
        // it at the end of the ordered array; otherwise, insert it
        // on the begin of the array and break the loop.
        //

        if (IsNotificationEvent(Waitable)) {
            SortedWaitables[--jj] = Waitable;
        } else {
            SortedWaitables[0] = Waitable;
            break;
        }
    }

    //
    // If all waitable are notification events, return.
    //

    if (i == Count) {
        return CanAcquireAll ? 1 : 0;
    }

    //
    // Order the remaining waitables, using the insertion sort
    // algorithm skipping the notification event.
    //

    for (k = 1, i++; i < Count; i++, k++) {
        Waitable = (PST_WAITABLE)Objects[i];

        //
        // Update the can acquire all flag.
        //

        CanAcquireAll &= ALLOWS_ACQUIRE(Waitable);

        if (IsNotificationEvent(Waitable)) {

            //
            // Insert the notification event at the end of
            // ordered array.
            //

            SortedWaitables[--jj] = Waitable;
        } else {

            //
            // Find the insertion position for the current waitable.
            //

            SortedWaitables[k] = Waitable;
            j = k - 1;
            while (j >= 0 && SortedWaitables[j] > Waitable) {
                SortedWaitables[j + 1] = SortedWaitables[j];
                j--;
            }

            //
            // Insert at j+1 position.
            //

            SortedWaitables[j + 1] = Waitable;

            //
            // Check for duplicates.
            //

            if (SortedWaitables[k - 1] == SortedWaitables[k]) {
                return -1;
            }
        }
    }
    return (CanAcquireAll ? 1 : 0);
}

//
// Waits until all of the specified Waitable allow the acquire,
// the specified timeout expires or the wait is alerted.
//

ULONG
WINAPI
StWaitable_WaitAllEx (
    __in ULONG Count,
    __in PVOID Objects[],
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    PST_WAITABLE SortedWaitables[MAXIMUM_WAIT_OBJECTS];
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlockArray[MAXIMUM_WAIT_OBJECTS];
    LONG Index;
    ULONG WaitStatus;
    ULONG LastTime;
    LONG WaitHint;
    ULONG GlobalSpinCount;
    ULONG SpinCount;
    SPIN_WAIT Spin;

    //
    // Validate the parameters and sort the specified waitables.
    //

    if (Count == 0 || Count > MAXIMUM_WAIT_OBJECTS ||
        (WaitHint = SortAndCheckForAllowAcquire(Objects, SortedWaitables, Count)) < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }

    //
    // If at leas one of the waitables doesn't allow an immedtate
    // acquire and a null timeout was specified, return failure.
    //

    if (WaitHint == 0 && Timeout == 0) {
        return WAIT_TIMEOUT;
    }

    //
    // if a timeout was specified, get the current time
    // in order to adjust the timeout value later.
    //

    LastTime = (Timeout != INFINITE) ? GetTickCount() : 0;
    do {
        if (WaitHint == 0) {

            //
            // Initialize the parker for cooperative release
            // and execute the WaitAll prologue on all waitables.
            //
        
            InitializeParker(&Parker, (USHORT)Count);
            GlobalSpinCount = 1;
            for (Index = 0; Index < (LONG)Count; Index++) {
                if (!WAIT_ALL_PROLOGUE(SortedWaitables[Index], &Parker,
                                       &WaitBlockArray[Index], &SpinCount)) {

                    //
                    // Adjust the global spin count.
                    //

                    if (GlobalSpinCount != 0) {
                        if (SpinCount == 0) {
                            GlobalSpinCount = 0;
                        } else if (SpinCount > GlobalSpinCount) {
                            GlobalSpinCount = SpinCount;
                        }
                    }
                }
            }

            //
            // Park the current thread, activating the specified cancellers.
            //

            WaitStatus = ParkThreadEx(&Parker, GlobalSpinCount, Timeout, Alerter);

            //
            // If the wait was cancelled due to timeout or alert, cancel the
            // acquire attempt on all waitables and return the appropriate
            // failure status.
            //
    
            if (WaitStatus != WAIT_STATE_CHANGED) {
                for (Index = 0; Index < (LONG)Count; Index++) {
                    PWAIT_BLOCK WaitBlock = &WaitBlockArray[Index];
                    if (WaitBlock->WaitListEntry.Flink != &WaitBlock->WaitListEntry) {
                        CANCEL_ACQUIRE(SortedWaitables[Index], WaitBlock);
                    }
                }
                return WaitStatus;
            }
            
            //
            // Before continue, we spin until all wait blocks are
            // marked as unlinked, which ensures that the wait blocks
            // can be reused because no other thread is using them.
            //

            InitializeSpinWait(&Spin);
            for (Index = 0; Index < (LONG)Count; Index++) {
                PLIST_ENTRY_ Entry = &WaitBlockArray[Index].WaitListEntry;
                while (Entry->Flink != Entry) {
                    SpinOnce(&Spin);
                }
            }
        }

        //
        // Try to satisfy the acquire on all waitables.
        //

        for (Index = 0; Index < (LONG)Count; Index++) {
            if (!TRY_ACQUIRE(SortedWaitables[Index])) {
                break;
            }
        }

        //
        // If all waitables were acquired, return success.
        //

        if (Index == (LONG)Count) {
            return WAIT_SUCCESS;
        }

        //
        // We failed the acquire-all, so undo the acquires
        // that we did above.
        //

        while (--Index >= 0) {
            UNDO_ACQUIRE(SortedWaitables[Index]);
        }

        //
        // If a timeout was specified, adjust the timeout value
        // that will be used on the next wait.
        //

        if (Timeout != INFINITE) {
            ULONG Now = GetTickCount();
            ULONG Elapsed = (Now == LastTime) ? 1 : (Now - LastTime);
            if (Timeout <= Elapsed) {
                return WAIT_TIMEOUT;
            }
            Timeout -= Elapsed;
            LastTime = Now;
        }
        WaitHint = 0;
    } while (TRUE);
}

//
// Waits unconditionally until all of the specified Waitable are
// signalled.
//

BOOL
WINAPI
StWaitable_WaitAll (
    __in ULONG Count,
    __in PVOID Objects[]
    )
{
    return StWaitable_WaitAllEx(Count, Objects, INFINITE, NULL) == WAIT_SUCCESS;
}

//
// Signals a synchronizer and waits atomically one another until is
// is signalled, activating the specified cancellers.
// 

ULONG
WINAPI
StWaitable_SignalAndWaitEx (
    __inout PVOID ObjectToSignal,
    __inout PVOID ObjectToWaitOn,
    __in ULONG Timeout,
    __in PST_ALERTER Alerter
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    ULONG WaitStatus;
    ULONG SpinCount;
    PST_WAITABLE WaitableToSignal = (PST_WAITABLE)ObjectToSignal;
    PST_WAITABLE WaitableToWaitOn = (PST_WAITABLE)ObjectToWaitOn;

    //
    // Initialize the parker and execute the WaitAny prologue
    // on the waitable where we want to wait on.
    //

    InitializeParker(&Parker, 1);
    WAIT_ANY_PROLOGUE(WaitableToWaitOn, &Parker, &WaitBlock, WAIT_SUCCESS, &SpinCount);

    //
    // Execute the signal operation on the "to signal" waitable.
    //
    // If the signal fails, try to cancel the acquire attempt on the
    // parker; if succeed, cancel the acquire attempt on the
    // waitable; otherwise, we must wait unconditionally until the
    // current thread is unparked and, then, undo the acquire.
    //

    if (!RELEASE(WaitableToSignal)) {
        if (TryCancelParker(&Parker)) {
            CANCEL_ACQUIRE(WaitableToWaitOn, &WaitBlock);
        } else {
            ParkThread(&Parker);
            UNDO_ACQUIRE(WaitableToWaitOn);
        }

        //
        // The reason for failure was set using the SetLastError API.
        // Here, we simply return failure.
        //

        return WAIT_FAILED;
    }

    //
    // Park the current thread, activating the specified cancellers.
    //

    WaitStatus = ParkThreadEx(&Parker, SpinCount, Timeout, Alerter);

    //
    // If the acquire succeed, execute the wait-epilogue and
    // return success.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        WAIT_EPILOGUE(WaitableToWaitOn);
        return WAIT_SUCCESS;
    }
    
    //
    // The acquire was cancelled due to timeout or alert.
    // So, cancel the acquire attempt on the waitable and
    // return the appropriate failure status.
    //
    
    CANCEL_ACQUIRE(WaitableToWaitOn, &WaitBlock);
    return WaitStatus;
}

//
// Atomically, signals a synchronizer and waits unconditionally on anoter.
// 

BOOL
WINAPI
StWaitable_SignalAndWait (
    __inout PVOID ObjectToSignal,
    __inout PVOID ObjectToWaitOn
    )
{
    return (StWaitable_SignalAndWaitEx(ObjectToSignal, ObjectToWaitOn,
                                       INFINITE, NULL) == WAIT_SUCCESS);
}
