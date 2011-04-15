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
// The reader/writer lock's state is used as follows:
// - bits 0-29: hold the current number of read locks.
// - bit 30: is set when the write lock is held.
// - bit 31: unused.
//

#define WRITING			(1 << 30)
#define MAX_READERS		(WRITING - 1)

//
// The types of lock acquire requests.
//

#define LOCK_READ			1
#define LOCK_WRITE			2

//
// Tries to acquire the read lock on behalf of the current thread.
//

static
FORCEINLINE
BOOLEAN
TryAcquireReadLockInternal (
    __inout PST_READ_WRITE_LOCK RWLock
    )
{
    do {
        LONG State;
        if ((State = RWLock->State) >= MAX_READERS || !IsLockedQueueEmpty(&RWLock->Queue)) {
            return FALSE;
        }
        if (CasLong(&RWLock->State, State, State + 1)) {
            return TRUE;
        }
    } while (TRUE);
}

//
// Tries to acquire the read lock on behalf of the thread that
// is at that front of the r/w lock's queue.
// 

static
FORCEINLINE
BOOLEAN
TryAcquireReadLockInternalQueued (
    __inout PST_READ_WRITE_LOCK RWLock
    )
{
    do {
        LONG State;
        if ((State = RWLock->State) >= MAX_READERS) {
            return FALSE;
        }
        if (CasLong(&RWLock->State, State, State + 1)) {
            return TRUE;
        }
    } while (TRUE);
}

//
// Tries to acquire the write lock on behalf of the current thread.
// 

static
FORCEINLINE
BOOLEAN
TryAcquireWriteLockInternal (
    __inout PST_READ_WRITE_LOCK RWLock
    )
{
    do {
        LONG State;
        if ((State = RWLock->State) != 0 || !IsLockedQueueEmpty(&RWLock->Queue)) {
            return FALSE;
        }
        if (CasLong(&RWLock->State, 0, WRITING)) {
            return TRUE;
        }
    } while (TRUE);
}

//
// Returns true with the r/w lock's queue locked if any
// waiter can be release; otherwise, returns false with
// the r/w lock's queue unlocked.
//

static
FORCEINLINE
BOOLEAN
IsReleasePending (
    __inout PST_READ_WRITE_LOCK RWLock
    )
{

    LONG FrontRequest;

    FrontRequest = RWLock->Queue.FrontRequest;
    return (FrontRequest != 0 && (RWLock->State == 0 || (FrontRequest == LOCK_READ &&
                                                         RWLock->State < MAX_READERS)) &&
            TryLockLockedQueue(&RWLock->Queue));
}

//
// Releases the appropriate waiters and unlocks the
// r/w lock's queue.
//

static
VOID
FASTCALL
ReleaseWaitersAndUnlockQueue (
    __inout PST_READ_WRITE_LOCK RWLock
    )
{
    PLIST_ENTRY_ Entry;
    PLIST_ENTRY_ ListHead;
    PWAIT_BLOCK WaitBlock;
    PST_PARKER Parker;
    LONG Request;

    ListHead = &RWLock->Queue.Head;
    do {

        //
        // Release all waiters that can be released with the
        // actual r/w lock's state.
        //

        while (RWLock->State < MAX_READERS && (Entry = ListHead->Flink) != ListHead) {

            WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            Parker = WaitBlock->Parker;

            //
            // Try to satisfy the request of the first waiter.
            //

            if (WaitBlock->WaitType == WaitAny) {
                Request = WaitBlock->Request & MAX_REQUEST;
                if (Request == LOCK_READ) {

                    //
                    // Try to acquire the read lock.
                    //

                    if (TryAcquireReadLockInternalQueued(RWLock)) {

                        //
                        // We acquire the read lock. So, remove the wait block from
                        // the r/w lock's queue and try to lock parker and, if succeed,
                        // unpark the parker's owner thread.
                        //

                        RemoveEntryList(Entry);
                        if (TryLockParker(Parker) || WaitBlock->Request < 0) {
                            UnparkThread(Parker, WaitBlock->WaitKey);
                        } else {

                            //
                            // The lock acquire attempt was cancelled; so, mark the
                            // wait block as unlinked and undo the previous read
                            // lock acquire.
                            //

                            Entry->Flink = Entry;
                            InterlockedDecrement(&RWLock->State);
                        }
                    } else {
                        
                        //
                        // The read lock cant's be acquired; so, exit the
                        // release loop.
                        //
                        
                        break; 
                    }
                } else {

                    //
                    // This is a write lock acquire attempt (unlocked or
                    // locked), so try to acquire the write lock.
                    //

                    if (CasLong(&RWLock->State, 0, WRITING)) {

                        //
                        // We acquired the write lock. So, remove the wait block
                        // from the r/w lock's queue and try to lock the associated
                        // parker; if succeed, unpark the waiter thread and
                        // exit the release loop.
                        //

                        RemoveEntryList(Entry);
                        if (TryLockParker(Parker) || WaitBlock->Request < 0) {
                            UnparkThread(Parker, WaitBlock->WaitKey);
                            break;
                        } else {

                            //
                            // The lock attempt was cancelled, mark the wait block
                            // as unlinked and undo the previous lock acquire.
                            //

                            Entry->Flink = Entry;
                            InterlockedExchange(&RWLock->State, 0);
                        }
                    } else {
                        
                        //
                        // The write lock can't be acquired; so, exit the
                        // release loop.
                        //

                        break;
                    }
                }
            } else {

                //
                // Wait-all: if the write lock is free, remove the wait block from
                // the r/w lock's queue and lock the parker; if this is the
                // last cooperative release, unpark the waiter thread; anyway, mark
                // the wait block as unlinked.
                //

                if (RWLock->State == 0) {
                    RemoveEntryList(Entry);
                    if (TryLockParker(Parker)) {
                        UnparkThread(Parker, WaitBlock->WaitKey);
                    }
                    Entry->Flink = Entry;
                } else {

                    //
                    // We can't acquire the write lock is busy; so, exit the
                    // release loop.
                    //

                    break;
                }
            }
        }

        //
        // Unlock the r/w lock queue. However, if there are new waiters
        // and the write lock isn't taken, repeat the release loop.
        //

        if (!TryUnlockLockedQueue(&RWLock->Queue, RWLock->State >= MAX_READERS)) {
            continue;
        }

        //
        // Check if any waiter can be released and, if so, repeat the
        // release processing.
        //
        
        if (!IsReleasePending(RWLock)) {
            return;
        }
    } while (TRUE);
}

//
// Cancels the specified lock acquire.
//

static
FORCEINLINE
VOID
CancelAcquire (
    __inout PST_READ_WRITE_LOCK RWLock,
    __inout PLIST_ENTRY_ Entry
    )
{
    if (Entry->Flink != Entry && LockLockedQueue(&RWLock->Queue, Entry)) {
        if (Entry->Flink != Entry) {
            RemoveEntryList(Entry);
        }
        ReleaseWaitersAndUnlockQueue(RWLock);
    }
}

//
// Enqueues a lock read attempt.
//

static
FORCEINLINE
ULONG
EnqueueLockRead (
    __inout PST_READ_WRITE_LOCK RWLock,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    BOOLEAN FrontHint;

    if (!EnqueueInLockedQueue(&RWLock->Queue, WaitBlock, &FrontHint)) {
        goto Exit;
    }
    if (RWLock->State >= MAX_READERS || !FrontHint) {

        //
        // The current thread must wait. So, unlock the r/w lock's
        // queue and return if there aren't releases pending.
        //

        TryUnlockLockedQueue(&RWLock->Queue, TRUE);
        if (!IsReleasePending(RWLock)) {
            goto Exit;
        }
    }

    //
    // There are pending releases, so execute the release
    // process and unlock the r/w lock's queue.
    //

    ReleaseWaitersAndUnlockQueue(RWLock);

Exit:
    return FrontHint ? RWLock->SpinCount : 0;
}

//
// Enqueues a lock write attempt.
//

static
FORCEINLINE
ULONG
EnqueueLockWrite (
    __inout PST_READ_WRITE_LOCK RWLock,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    BOOLEAN FrontHint;

    if (!EnqueueInLockedQueue(&RWLock->Queue, WaitBlock, &FrontHint)) {
        goto Exit;
    }
    if (!FrontHint || RWLock->State != 0) {

        //
        // The current thread must wait. So, unlock the r/w lock's
        // queue and return if there aren't releases pending.
        //

        TryUnlockLockedQueue(&RWLock->Queue, TRUE);
        if (!IsReleasePending(RWLock)) {
            goto Exit;
        }
    }

    //
    // There are pending releases, so execute the release
    // process and unlock the r/w lock's queue.
    //

    ReleaseWaitersAndUnlockQueue(RWLock);

Exit:
    return FrontHint ? RWLock->SpinCount : 0;
}

//
// Excutes the try enter read tail processing.
//

static
FORCEINLINE
ULONG
TryEnterReadTail (
    __inout PST_READ_WRITE_LOCK RWLock,
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
    // wait block it in r/w lock's queue.
    //

    InitializeParkerAndWaitBlock(&WaitBlock, &Parker, LOCK_READ, WaitAny, WAIT_SUCCESS);
    SpinCount = EnqueueLockRead(RWLock, &WaitBlock);

    //
    // Park the current thread.
    //

    if ((WaitStatus = ParkThreadEx(&Parker, SpinCount, Timeout, Alerter)) == WAIT_SUCCESS) {
        return WAIT_SUCCESS;
    }

    //
    // The enter read lock attempt was cancelled due to timeout;
    // so, cancel the enter read lock attempt and return failure.
    //

    CancelAcquire(RWLock, &WaitBlock.WaitListEntry);
    return WaitStatus;
}

//
// Tries to acquire the the read lock, activating the specified cancellers.
//

ULONG
WINAPI
StReadWriteLock_TryEnterReadEx (
    __inout PST_READ_WRITE_LOCK RWLock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    //
    // If the read lock is free, acquire it and return success.
    //

    if (TryAcquireReadLockInternal(RWLock)) {
        return WAIT_SUCCESS;
    }

    //
    // The read lock is busy. So, if a zero timeout was specified,
    // return failure. Otherwise, execute the try enter read tail
    // processing.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }
    return TryEnterReadTail(RWLock, Timeout, Alerter);
}

//
// Tries to acquire the the read lock immediately.
//

BOOL
WINAPI
StReadWriteLock_TryEnterRead (
    __inout PST_READ_WRITE_LOCK RWLock
    )
{
    return TryAcquireReadLockInternal(RWLock);
}

//
// Acquires the read lock unconditionally.
//

BOOL
WINAPI
StReadWriteLock_EnterRead (
    __inout PST_READ_WRITE_LOCK RWLock
    )
{
    return (TryAcquireReadLockInternal(RWLock) ||
            TryEnterReadTail(RWLock, INFINITE, NULL) == WAIT_SUCCESS);
}

//
// Executes the try enter write tail processing.
//

static
FORCEINLINE
ULONG
TryEnterWriteTail (
    __inout PST_READ_WRITE_LOCK RWLock,
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
    // wait block in the r/w lock's queue.
    //

    InitializeParkerAndWaitBlock(&WaitBlock, &Parker, LOCK_WRITE, WaitAny, WAIT_SUCCESS);
    SpinCount = EnqueueLockWrite(RWLock, &WaitBlock);

    //
    // Park the current thread.
    //

    if ((WaitStatus = ParkThreadEx(&Parker, SpinCount, Timeout, Alerter)) == WAIT_SUCCESS) {
        return WAIT_SUCCESS;
    }

    //
    // The write lock attempt was cancelled due to timeout, so cancel
    // the acquire lock attempt and return failure.
    //

    CancelAcquire(RWLock, &WaitBlock.WaitListEntry);
    return WaitStatus;
}

//
// Tries to acquire the write lock, activating the specified cancellers.
//

ULONG
WINAPI
StReadWriteLock_TryEnterWriteEx (
    __inout PST_READ_WRITE_LOCK RWLock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{

    //
    // If the write lock is free, acquire it and return success.
    //

    if (TryAcquireWriteLockInternal(RWLock)) {
        return WAIT_SUCCESS;
    }

    //
    // The write lock is busy. So, return failure if a null timeout
    // was specified. Otherwise, execute the try enter write tail
    // processing.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }
    return TryEnterWriteTail(RWLock, Timeout, Alerter);
}

//
// Tries to acquire the write lock immediately.
//

BOOL
WINAPI
StReadWriteLock_TryEnterWrite (
    __inout PST_READ_WRITE_LOCK RWLock
    )
{
    return TryAcquireWriteLockInternal(RWLock);
}

//
// Acquire the write lock unconditionally.
//

BOOL
WINAPI
StReadWriteLock_EnterWrite (
    __inout PST_READ_WRITE_LOCK RWLock
    )
{

    return (TryAcquireWriteLockInternal(RWLock) ||
            TryEnterWriteTail(RWLock, INFINITE, NULL) == WAIT_SUCCESS);
}

//
// Releases the read lock.
//

VOID
WINAPI
StReadWriteLock_ExitRead (
    __in PST_READ_WRITE_LOCK RWLock
    )
{

    //
    // Release the read lock. If the current thread is the last reader
    // and there are waiters that can be released, try to lock the
    // r/w lock's queue and release the appropriate waiters.
    //

    if (InterlockedDecrement(&RWLock->State) == 0 && IsReleasePending(RWLock)) {
        ReleaseWaitersAndUnlockQueue(RWLock);
    }
}

//
// Releases the write lock.
//

VOID
WINAPI
StReadWriteLock_ExitWrite (
    __in PST_READ_WRITE_LOCK RWLock
    )
{

    //
    // Releases the write lock. If there are waiters, try to lock the
    // r/w lock's queue and release the appropriate waiters.
    //

    InterlockedExchange(&RWLock->State, 0);
    if (IsReleasePending(RWLock)) {
        ReleaseWaitersAndUnlockQueue(RWLock);
    }
}

/*++
 *
 * Virtual functions used by the condition variables.
 * 
 --*/

//
// Returns true if the write lock is busy.
//

static
BOOLEAN
FASTCALL
_IsOwned (
    __in PVOID Lock
    )
{
    return (((PST_READ_WRITE_LOCK)Lock)->State == WRITING);
}

//
// Exits the write lock.
//

static
LONG
FASTCALL
_ReleaseLockCompletely (
    __inout PVOID Lock
    )
{
    StReadWriteLock_ExitWrite((PST_READ_WRITE_LOCK)Lock);
    return 0;
}

//
// Reenters the write lock.
//

static
VOID
FASTCALL
_ReacquireLock (
    __inout PVOID Lock,
    __in ULONG WaitStatus,
    __in LONG Ignored
    )
{
    if (WaitStatus != WAIT_SUCCESS) {
        do {
            if (StReadWriteLock_EnterWrite((PST_READ_WRITE_LOCK)Lock)) {
                break;
            }
            Sleep(500);
        } while (TRUE);
    }
}

//
// Enqueues the specified wait block as a locked write lock
// attempt in the r/w lock's queue.
//

static
VOID
FASTCALL
_EnqueueLockWaiter (
    __inout PVOID Lock,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    
    //
    // Mark the request as a locked acquire write lock request
    // and insert in in the r/w lock's queue.
    //

    WaitBlock->Request = (LOCKED_REQUEST | LOCK_WRITE);
    EnqueueLockWrite((PST_READ_WRITE_LOCK)Lock, WaitBlock);
}

//
// Virtual function table used by the condition variables.
//

ILOCK_VTBL ILockVtblForReadWriteLock = {
    _IsOwned,
    _ReleaseLockCompletely,
    _ReacquireLock,
    _EnqueueLockWaiter
};

/*++
 *
 * Virtual functions that support the Waitable functionality.
 *
 --*/

//
// Returns true if the write lock can be acquired immediately.
//

static
BOOLEAN
FASTCALL
_AllowsAcquire (
    __inout PST_WAITABLE Waitable
    )
{
    PST_READ_WRITE_LOCK RWLock = (PST_READ_WRITE_LOCK)Waitable;

    return (RWLock->State == 0 && IsLockedQueueEmpty(&RWLock->Queue));
}

//
// Tries to acquire the write lock.
//

static
BOOLEAN
FASTCALL
_TryAcquire (
    __inout PST_WAITABLE Waitable
    )
{
    return TryAcquireWriteLockInternal((PST_READ_WRITE_LOCK)Waitable);
}

//
// Releases the write lock.
//

static
BOOLEAN
FASTCALL
_Release (
    __inout PST_WAITABLE Waitable
    )
{
    StReadWriteLock_ExitWrite((PST_READ_WRITE_LOCK)Waitable);
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
    PST_READ_WRITE_LOCK RWLock = (PST_READ_WRITE_LOCK)Waitable;

    //
    // Try to acquire the write lock if it is free.
    //

    if (TryAcquireWriteLockInternal(RWLock)) {
        
        //
        // Try to self lock the parker and, if succeed, self
        // unpark the current thread.
        //

        if (TryLockParker(Parker)) {
            UnparkSelf(Parker, WaitKey);
        } else {

            //
            // The wait-any operation was already satisfied on synchronizer,
            // so we must undo the previous acquire.
            //

            StReadWriteLock_ExitWrite(RWLock);
        }

        //
        // Return true to signal that the wait-any operation
        // was already satisfied.
        //
        
        return TRUE;
    }

    //
    // The write lock is busy. So initialize the wait-any wait block and
    // insert it in the r/w lock's queue.
    //

    InitializeWaitBlock(WaitBlock, Parker, LOCK_WRITE, WaitAny, WaitKey);
    *SpinCount = EnqueueLockWrite(RWLock, WaitBlock);

    //
    // Return false, to signal that the wait block was inserted in
    // the r/w lock's queue.
    //

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
    PST_READ_WRITE_LOCK RWLock = (PST_READ_WRITE_LOCK)Waitable;

    if (_AllowsAcquire(&RWLock->Waitable)) {
        
        //
        // The write lock is free; so, lock the parker and,
        // if this is the last cooperative release, self unpark the
        // current thread.
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
    // The write lock is busy. So, initialize the wait-all wait block,
    // insert it in the r/w lock's queue and return false to signal that
    // the wait block was enqueued.
    //

    InitializeWaitBlock(WaitBlock, Parker, LOCK_WRITE, WaitAll, WAIT_STATE_CHANGED);
    *SpinCount = EnqueueLockWrite(RWLock, WaitBlock);
    return FALSE;
}

//
// Undoes a previous acquire operation, this is, releases
// the write lock.
//

static
VOID
FASTCALL
_UndoAcquire (
    __inout PST_WAITABLE Waitable
    )
{
    StReadWriteLock_ExitWrite((PST_READ_WRITE_LOCK)Waitable);
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
    CancelAcquire((PST_READ_WRITE_LOCK)Waitable, &WaitBlock->WaitListEntry);
}


//
// Virtual function table that implements the waitable functionality
// for the r/w lock.
//

static
WAITABLE_VTBL ReadWriteLockVtbl = {
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
// Initializes the specified read write lock.
//

VOID
WINAPI
StReadWriteLock_Init (
    __out PST_READ_WRITE_LOCK RWLock,
    __in ULONG SpinCount
    )
{
    RWLock->Waitable.Vptr = &ReadWriteLockVtbl;
    InitializeLockedQueue(&RWLock->Queue);
    RWLock->State = 0;
    RWLock->SpinCount = IsMultiProcessor() ? SpinCount : 0;
}
