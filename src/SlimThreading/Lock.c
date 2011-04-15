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
// Types of lock acquire requests.
//

#define ACQUIRE				1
#define LOCKED_ACQUIRE		(LOCKED_REQUEST | ACQUIRE)

//
// Initializes the specified lock object.
//

VOID
WINAPI
StLock_Init (
    __out PST_LOCK Lock,
    __in ULONG SpinCount
    )
{
    Lock->State = LOCK_FREE;
    Lock->SpinCount = IsMultiProcessor() ? SpinCount : 0;
}

//
// Unparks the specifed list of waiting threads.
//

static
FORCEINLINE
VOID
UnparkWaitList (
    __in PLIST_ENTRY_ Entry
    )
{
    PLIST_ENTRY_ WakeStack;
    PLIST_ENTRY_ Next;
    PWAIT_BLOCK WaitBlock;

    //
    // If the specified list is empty, return.
    //

    if (Entry == LOCK_BUSY || Entry == LOCK_FREE) {
        return;
    }

    //
    // Since the wait list is implemented as a stack, we build
    // another stack with the locked waiter threads in order to
    // unpark them in FIFO order.
    //
    
    WakeStack = NULL;
    do {
        Next = Entry->Flink;
        WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
        if (WaitBlock->Request < 0 || TryLockParker(WaitBlock->Parker)) {			
            
            //
            // Add the waiter thread to the wake up stack.
            //
            
            Entry->Blink = WakeStack;
            WakeStack = Entry;
        } else {

            //
            // Mark the wait block as unlinked.
            //

            Entry->Flink = Entry;
        }		
    } while ((Entry = Next) != LOCK_BUSY);
    
    //
    // Unpark all locked waiter threads.
    //
    
    while (WakeStack != NULL) {
        Next = WakeStack->Blink;
        WaitBlock = CONTAINING_RECORD(WakeStack, WAIT_BLOCK, WaitListEntry);
        UnparkThread(WaitBlock->Parker, WaitBlock->WaitKey);
        WakeStack = Next;
    }
}	

//
// Unlinks the specified list entry from the lock's wait list.
//

static
FORCEINLINE
VOID
UnlinkListEntry (
     __in PST_LOCK Lock,
     __in PLIST_ENTRY_ Entry
     )
{
    PLIST_ENTRY_ State;
    SPIN_WAIT Spin;

    do {

        //
        // If the entry was already unlinked, return.
        //
            
        if (Entry->Flink == Entry) {
            return;
        }

        //
        // If the lock's wait list seems empty, we must wait until
        // the list entry is marked as unlinked.
        //
            
        if ((State = Lock->State) == LOCK_BUSY || State == LOCK_FREE) {
            break;
        }
            
        //
        // Try to grab the lock's wait list.
        //

        if (CasPointer(&Lock->State, State, LOCK_BUSY)) {

            //
            // If this entry is the only entry of the wait list, return.
            //

            if (State == Entry && Entry->Flink == LOCK_BUSY) {
                return;
            }

            //
            // We grabbed the lock's wait list, so unpark all waiting
            // threads and wait until detect that our wait block is
            // marked as unlinked.
            //

            UnparkWaitList(State);
            break;
        }
    } while (TRUE);
    
    //
    // Spin unconditionally until we detect that the list entry
    // is marked as unlinked.
    //

    InitializeSpinWait(&Spin);
    while (Entry->Flink != Entry) {
        SpinOnce(&Spin);
    }
}

//
// Tries to acquire the lock when it is busy, within the
// specified timeout.
//


BOOLEAN
FASTCALL
Lock_TryEnterTail (
    __inout PST_LOCK Lock,
    __in ULONG Timeout
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    PLIST_ENTRY_ State;
    ULONG LastTime;
    ULONG WaitStatus;
    ULONG SpinCount;

    //
    // Get a time reference in order to adjust the timeout value
    // when the current thread needs to re-wait.
    //

    LastTime = (Timeout != INFINITE) ? GetTickCount() : 0;

    //
    // Mark the wait block as uninitialized.
    //

    WaitBlock.Parker = NULL;
    do {

        //
        // Try to acquire the lock spinning, the specified number of
        // cycles but only if the lock's wait list isn't empty.
        //

        SpinCount = Lock->SpinCount;
        do {
            if ((State = Lock->State) == LOCK_FREE) {
                if (CasPointer(&Lock->State, LOCK_FREE, LOCK_BUSY)) {
                    return TRUE;
                }
                continue;
            }
            if (State != LOCK_BUSY || SpinCount-- == 0) {
                break;
            }
            SpinWait(1);
        } while (TRUE);

        //
        // If the wait block is not initialized, initialize it.
        //
        
        if (WaitBlock.Parker == NULL) {
            InitializeWaitBlock(&WaitBlock, &Parker, ACQUIRE, WaitAny, WAIT_SUCCESS);
        }

        //
        // Initialize the parker for exclusive release.
        //

        InitializeParker(&Parker, 1);

        //
        // Try to insert the wait block in the lock's wait list if
        // the lock is busy; otherwise, try to acquire the lock.
        //

        do {

            //
            // If the lock seems free, try to acquire it.
            //

            if ((State = Lock->State) == LOCK_FREE) {
                if (CasPointer(&Lock->State, LOCK_FREE, LOCK_BUSY)) {
                    return TRUE;
                }
                continue;
            }

            //
            // The lock seems busy, so try to insert our wait block
            // in the lock's wait list.
            //

            WaitBlock.WaitListEntry.Flink = State;
            if (CasPointer(&Lock->State, State, &WaitBlock.WaitListEntry)) {
                break;
            }
        } while (TRUE);

        //
        // Park the current thread.
        //

        WaitStatus = ParkThreadEx(&Parker, 0, Timeout, NULL);

        //
        // If the wait was cancelled due to timeout, break the loop.
        //

        if (WaitStatus != WAIT_SUCCESS) {
            break;
        }

        //
        // Try to acquire the lock.
        //

        if (Lock->State == LOCK_FREE && CasPointer(&Lock->State, LOCK_FREE, LOCK_BUSY)) {
            return TRUE;
        }

        //
        // If a timeout was specified, we must adjust the timeout value
        // that will be used in the next wait.
        //

        if (Timeout != INFINITE) {
            ULONG Now = GetTickCount();
            ULONG Elapsed = (Now == LastTime) ? 1 : (Now - LastTime);
            if (Timeout <= Elapsed) {
                return FALSE;
            }
            Timeout -= Elapsed;
            LastTime = Now;
        }
    } while (TRUE);

    //
    // The specified timeout was expired, so unlink our wait block
    // and return failure.
    //

    UnlinkListEntry(Lock, &WaitBlock.WaitListEntry);
    return FALSE;
}

//
// Tries to acquire the lock within the specified timeout.
//

BOOL
WINAPI
StLock_TryEnterEx (
    __inout PST_LOCK Lock,
    __in ULONG Timeout
    )
{

    //
    // Try to acquire the lock immediately.
    //

    if (Lock->State == LOCK_FREE && CasPointer(&Lock->State, LOCK_FREE, LOCK_BUSY)) {
        return TRUE;
    }

    //
    // If a null timeout was specified, return failure. Otherwise,
    // execute the try acquire tail processing.
    //

    if (Timeout == 0) {
        return FALSE;
    }
    return Lock_TryEnterTail(Lock, Timeout);
}

//
// Tries to acquire the lock immediately.
//

BOOL
WINAPI
StLock_TryEnter (
    __inout PST_LOCK Lock
    )
{

    return (Lock->State == LOCK_FREE && CasPointer(&Lock->State, LOCK_FREE, LOCK_BUSY));
}

//
// Acquires the lock.
//

BOOL
WINAPI
StLock_Enter (
    __inout PST_LOCK Lock
    )
{

    //
    // Try to acquire the lock immediately if it is free; otherwise,
    // execute the try enter tail processing without cancellers.
    //
    // NOTE: The enter can fail because the thread can't create a park spot
    //		 to block, due to resource exhaustion.
    //

    if (Lock->State == LOCK_FREE && CasPointer(&Lock->State, LOCK_FREE, LOCK_BUSY)) {
        return TRUE;
    }
    return Lock_TryEnterTail(Lock, INFINITE);
}

//
// Exits the lock.
//

VOID
WINAPI
StLock_Exit (
    __inout PST_LOCK Lock
    )
{

    //
    // Grab the wait list, freeing the lock, and wake up all waiter threads.
    //

    UnparkWaitList(InterlockedExchangePointer(&Lock->State, LOCK_FREE));
}

/*++
 *
 * Virtual functions used by the condition variables.
 * 
 --*/

//
// Returns true if the lock is busy.
//

static
BOOLEAN
FASTCALL
_IsOwned (
    __in PVOID Lock
    )
{
    return ((PST_LOCK)Lock)->State != LOCK_FREE;
}

//
// Exits the lock completely, returning the current lock state.
//

static
LONG
FASTCALL
_ReleaseCompletely (
    __inout PVOID Lock
    )
{
    StLock_Exit((PST_LOCK)Lock);
    return 1;
}

//
// Reenters the lock restoring the previous lock state.
//

static
VOID
FASTCALL
_Reacquire (
    __inout PVOID Lock,
    __in ULONG Ignored,
    __in LONG Ignored2
    )
{
    do {
        if (StLock_Enter((PST_LOCK)Lock)) {
            return;
        }
        Sleep(500);
    } while (TRUE);
}

//
// Enqueue the specified wait block in the lock queue.
//
// NOTE: When this function is called, we know that the lock
//		 is owned by the current thread.
//

VOID
FASTCALL
EnqueueLockWaiter (
    __inout PVOID Lock,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    PST_LOCK Lockx = (PST_LOCK)Lock;
    PLIST_ENTRY_ State;
    
    //
    // Mark the request as an already locked request.
    //

    WaitBlock->Request = LOCKED_ACQUIRE;
    do {
        WaitBlock->WaitListEntry.Flink = (State = Lockx->State);
        if (CasPointer(&Lockx->State, State, &WaitBlock->WaitListEntry)) {
            return;
        }
    } while (TRUE);
}

//
// Virtual function table to be used by condition variables.
//

ILOCK_VTBL ILockVtblForLock = {
    _IsOwned,
    _ReleaseCompletely,
    _Reacquire,
    EnqueueLockWaiter
};
