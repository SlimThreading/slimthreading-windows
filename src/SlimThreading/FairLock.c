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
// The request type used for lock acquire.
//

#define ACQUIRE		1

//
// Returns true with the lock's queue locked if a waiter can
// be release; otherwise, returns false with the lock's queue
// unlocked.
//

static
FORCEINLINE
BOOLEAN
IsReleasePending (
    __inout PST_FAIR_LOCK Lock
    )
{
    return (Lock->Queue.FrontRequest != 0 && Lock->State != FLOCK_BUSY &&
            TryLockLockedQueue(&Lock->Queue));
}

//
// Releases the appropriate waiters and unlocks the
// lock's queue.
//

static
VOID
FASTCALL
ReleaseWaitersAndUnlockQueue (
    __inout PST_FAIR_LOCK Lock
    )
{
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    PWAIT_BLOCK WaitBlock;
    PST_PARKER Parker;

    ListHead = &Lock->Queue.Head;
    do {

        //
        // Loop while the first waiter can be released; that is, the
        // lock is free and the lock's queue isn't empty.
        //

        while (Lock->State != FLOCK_BUSY && (Entry = ListHead->Flink) != ListHead) {

            WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            Parker = WaitBlock->Parker;
            if (WaitBlock->WaitType == WaitAny) {

                //
                // Try to acquire the lock on behalf of the waiter thread.
                // If the lock was acquired, remove the wait block from queue
                // and try to lock the associated parker; if succeed,
                // unpark the waiter thread. If a thread is released, exit
                // the release loop.
                //

                if (CasLong(&Lock->State, FLOCK_FREE, FLOCK_BUSY)) {
                    RemoveEntryList(Entry);
                    if (TryLockParker(Parker) || WaitBlock->Request < 0) {
                        UnparkThread(Parker, WaitBlock->WaitKey);
                        break;
                    } else {
                
                        //
                        // The acquire attempt is cancelled; so, mark the wait block
                        // as unlinked and undo the previous acquired.
                        //

                        Entry->Flink = Entry;
                        InterlockedExchange(&Lock->State, FLOCK_FREE);
                    }
                } else {

                    //
                    // The lock is now busy, so exit the release loop.
                    //

                    break;
                }
            } else {

                //
                // Wait-all.
                // Since that the lock seems free on the check above, remove the
                // wait block from the queue and lock the parker; if this
                // is the last cooperative release, unpark the waiter thread;
                // anyway, mark the wait block as unlinked.
                //

                RemoveEntryList(Entry);
                if (TryLockParker(Parker)) {
                    UnparkThread(Parker, WaitBlock->WaitKey);
                }
                Entry->Flink = Entry;
            }
        }

        //
        // It seems that no more waiters can be released; so, unlock
        // the wait queue.
        //

        if (!TryUnlockLockedQueue(&Lock->Queue, Lock->State != FLOCK_BUSY)) {

            //
            // We get here when new wait blocks were inserted in the lock's
            // queue when the lock is free; so, repeat the release
            // processing.
            //

            continue;
        }

        //
        // If, after unlock the wait queue, it seems that the first
        // waiter can be released, repeat the release processing.
        //

        if (!IsReleasePending(Lock)) {
            return;
        }
    } while (TRUE);
}

//
// Cancels the specified lock acquire attempt.
//

FORCEINLINE
VOID
CancelAcquire (
    __inout PST_FAIR_LOCK Lock,
    __inout PLIST_ENTRY_ Entry
    )
{

    if (Entry->Flink != Entry && LockLockedQueue(&Lock->Queue, Entry)) {
        if (Entry->Flink != Entry) {
            RemoveEntryList(Entry);
        }
        ReleaseWaitersAndUnlockQueue(Lock);
    }
}
//
// Enqueues a lock acquire attempt.
//

static
FORCEINLINE
ULONG
EnqueueAcquire (
    __inout PST_FAIR_LOCK Lock,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    BOOLEAN FrontHint;

    //
    // Enqueue the wait block on the wait queue. If we didn't
    // lock the queue, return.
    //

    if (!EnqueueInLockedQueue(&Lock->Queue, WaitBlock, &FrontHint)) {
        goto Exit;
    }

    //
    // The wait block was inserted in the wait queue and we own
    // the queue´s lock. So, if the lock is busy or the wait block
    // wasn't inserted at the front of the queue, unlock the queue.
    //

    if (Lock->State == FLOCK_BUSY || !FrontHint) {
        TryUnlockLockedQueue(&Lock->Queue, TRUE);
        if (!IsReleasePending(Lock)) {
            goto Exit;
        }
    }

    //
    // We get here when it seems that a waiter (including ourselves)
    // can be released; so, execute the release processing.
    //

    ReleaseWaitersAndUnlockQueue(Lock);

Exit:

    //
    // Compute the number of spin cycles and return.
    //

    return FrontHint ? Lock->SpinCount : 0;
}

//
// Tries to acquire the lock when it is busy, activating the
// specified cancellers.
//

ULONG
FASTCALL
FairLock_TryEnterTail (
    __inout PST_FAIR_LOCK Lock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    ULONG SpinCount;
    ULONG WaitStatus;

    //
    // Initialize a wait block and a parker and insert it in the
    // lock's queue.
    //

    InitializeParkerAndWaitBlock(&WaitBlock, &Parker, ACQUIRE, WaitAny, WAIT_SUCCESS);
    SpinCount = EnqueueAcquire(Lock, &WaitBlock);
    
    //
    // Park the current thread, activating the specified cancellers and
    // spinning, if appropriate.
    //
    
    WaitStatus = ParkThreadEx(&Parker, SpinCount, Timeout, Alerter);

    //
    // If we acquired the lock, return success.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        return WAIT_SUCCESS;
    }
    
    //
    // The acquire attempt was cancelled due to timeout or alert.
    // So, cancel the lock acquire attempt and return the appropriate
    // failure status.
    //

    CancelAcquire(Lock, &WaitBlock.WaitListEntry);
    return WaitStatus;
}

//
// Tries to acquire the lock, activating the specified cancellers.
//

ULONG
WINAPI
StFairLock_TryEnterEx (
    __inout PST_FAIR_LOCK Lock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{

    //
    // Try to acquire the lock immediately and, if it is free.
    //

    if (TryAcquireFairLockInternal(Lock)) {
        return WAIT_SUCCESS;
    }

    //
    // The lock is busy; so return failure if a null timeout was
    // specified. Otherwise execute the try acquire tail processing.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }
    return FairLock_TryEnterTail(Lock, Timeout, Alerter);
}

//
// Tries to acquire the lock immediatelly.
//

BOOL
WINAPI
StFairLock_TryEnter (
    __inout PST_FAIR_LOCK Lock
    )
{
    return TryAcquireFairLockInternal(Lock);
}

//
// Acquires the lock unconditionally.
//

BOOL
WINAPI
StFairLock_Enter (
    __inout PST_FAIR_LOCK Lock
    )
{

    //
    // Try to acquire the lock immediately and, if it is free.
    // Otherwise, execute the enter tail processing.
    //

    return (TryAcquireFairLockInternal(Lock) ||
            FairLock_TryEnterTail(Lock, INFINITE, NULL) == WAIT_SUCCESS);
}

//
// Exits the lock.
//

VOID
WINAPI
StFairLock_Exit (
    __inout PST_FAIR_LOCK Lock
    )
{

    //
    // Release the lock and, if the first waiter can be released,
    // execute the release processing.
    //

    InterlockedExchange(&Lock->State, FLOCK_FREE);
    if (IsReleasePending(Lock)) {
        ReleaseWaitersAndUnlockQueue(Lock);
    }
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
    return (((PST_FAIR_LOCK)Lock)->State == FLOCK_BUSY);
}

//
// Leaves completely the lock.
//

static
LONG
FASTCALL
_ReleaseCompletely (
    __inout PVOID Lock
    )
{		
    StFairLock_Exit((PST_FAIR_LOCK)Lock);
    return 0;
}

//
// Reacquires the lock.
//

static
VOID
FASTCALL
_Reacquire (
    __inout PVOID Lock,
    __in ULONG WaitStatus,
    __in LONG Ignored
    )
{
    
    PST_FAIR_LOCK Lockx = (PST_FAIR_LOCK)Lock;

    //
    // If the wait on condition variable was cancelled, acquire the lock
    // unconditionally; otherwise, we know that the lock is already owned
    // by the current thread.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        return;
    }
    do {
        if (StFairLock_Enter(Lockx)) {
            break;
        }
        Sleep(500);
    } while (TRUE);
}

//
// Enqueues the specified wait block in the lock's wait queue
// as a locked acquire attempt.
// 
// NOTE: When this function is called, we know that the lock is
//		 owned by the current thread.
//

static
VOID
FASTCALL
_EnqueueWaiter (
    __inout PVOID Lock,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    WaitBlock->Request = LOCKED_REQUEST | ACQUIRE;
    EnqueueAcquire((PST_FAIR_LOCK)Lock, WaitBlock);
}

//
// Virtual function table used by the condition variables.
//

ILOCK_VTBL ILockVtblForFairLock = {
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
// Returns true if the lock can be immediately acquired.
//

static
BOOLEAN
FASTCALL
_AllowsAcquire (
    __in PST_WAITABLE Waitable
    )
{
    PST_FAIR_LOCK Lockx = (PST_FAIR_LOCK)Waitable;

    return (Lockx->State != FLOCK_BUSY && IsLockedQueueEmpty(&Lockx->Queue));
}

//
// Tries to acquire the lock immediately.
//

static
BOOLEAN
FASTCALL
_TryAcquire (
    __inout PST_WAITABLE Waitable
    )
{
    return TryAcquireFairLockInternal((PST_FAIR_LOCK)Waitable);
}

//
// Releases the specified lock if it is busy; otherwise,
// does nothing and returns failure.
//

static
BOOLEAN
FASTCALL
_Release (
    __inout PST_WAITABLE Waitable
    )
{
    StFairLock_Exit((PST_FAIR_LOCK)Waitable);
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
    PST_FAIR_LOCK Lock = (PST_FAIR_LOCK)Waitable;

    //
    // Try to acquire the lock if it is free. If we acquired
    // the lock, try to self lock the parker and, if succeed,
    // self unpark the current thread.
    //

    if (TryAcquireFairLockInternal(Lock)) {
        if (TryLockParker(Parker)) {
            UnparkSelf(Parker, WaitKey);
        } else {

            //
            // If the parker is already locked, it means
            // that the wait-any was satisfied on another waitable;
            // so, undo the previous lock acquire.
            //

            StFairLock_Exit(Lock);
        }

        //
        // The wait-any was already accomplished, so return true.
        //

        return TRUE;
    }

    //
    // The lock is busy, so initialize a wait-any wait block,
    // insert it in the lock's queue and return false.
    //

    InitializeWaitBlock(WaitBlock, Parker, ACQUIRE, WaitAny, WaitKey);
    *SpinCount = EnqueueAcquire(Lock, WaitBlock);
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
    PST_FAIR_LOCK Lock = (PST_FAIR_LOCK)Waitable;

    //
    // If the lock can be immediately acquired, lock the thread
    // parker and, if this is the last cooperative release, self
    // unpark the current thread.
    //

    if (_AllowsAcquire(&Lock->Waitable))  {
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
    // The lock is busy, so initialize a wait-all wait block,
    // insert it in the wait queue and return false.
    //

    InitializeWaitBlock(WaitBlock, Parker, ACQUIRE, WaitAll, WAIT_STATE_CHANGED);
    *SpinCount = EnqueueAcquire(Lock, WaitBlock);
    return FALSE;
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
    StFairLock_Exit((PST_FAIR_LOCK)Waitable);
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
    CancelAcquire((PST_FAIR_LOCK)Waitable, &WaitBlock->WaitListEntry);
}

//
// Virtual function table for the fair lock.
//

static
WAITABLE_VTBL FairLockVtbl = {
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
// Initializes the fair lock.
//

VOID
WINAPI
StFairLock_Init (
    __out PST_FAIR_LOCK Lock,
    __in ULONG SpinCount
    )
{
    Lock->Waitable.Vptr = &FairLockVtbl;
    InitializeLockedQueue(&Lock->Queue);
    Lock->SpinCount = IsMultiProcessor() ? SpinCount : 0;
    Lock->State = FLOCK_FREE;
}
