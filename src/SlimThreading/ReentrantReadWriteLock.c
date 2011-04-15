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

/*++
 * 
 * Read Lock Section
 * 
 --*/

//
// Tries to acquire the read lock if this is the first reader
// or a recursive acquisition.
//

static
FORCEINLINE
BOOLEAN
TryEnterReadLockInternal (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock,
    __in ULONG ThreadId,
    __out PRESOURCE_TABLE *ResourceTable
    )
{
    PRESOURCE_TABLE_ENTRY Entry;

    //
    // Try to acquire the read lock if we are the first reader.
    //

    if (RWLock->Lock.State == 0 && CasLong(&RWLock->Lock.State, 0, 1)) {
        RWLock->FirstReader = ThreadId;
        RWLock->FirstReaderCount = 1;
        return TRUE;
    }

    //
    // Check if this is a recursive acquisition of the first reader thread.
    //

    if (RWLock->FirstReader == ThreadId) {
        RWLock->FirstReaderCount++;
        return TRUE;
    }

    //
    // Check if the current thread is already a read lock owner.
    //

    Entry = LookupEntryOnResourceTable(RWLock, ResourceTable); 
    if (Entry != NULL) {
        Entry->RefCount++;
        return TRUE;
    }
    return FALSE;
}



//====

//
// Executes the try enter read tail processing.
//

static
FORCEINLINE
ULONG
TryEnterReadTail (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock,
    __in PRESOURCE_TABLE ResourceTable,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    PRESOURCE_TABLE_ENTRY Entry;
    ULONG WaitStatus;

    //
    // We add an entry on the current thread's resource table before
    // enter the read lock to put a possible memory allocation out of
    // the read critical section.
    //

    if ((Entry = AddEntryToResourceTable(ResourceTable, RWLock)) == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        return WAIT_FAILED;
    }

    //
    // Enter the read lock on the associated non-reentrant r/w lock
    // and return, if succeed.
    //

    WaitStatus = StReadWriteLock_TryEnterReadEx(&RWLock->Lock, Timeout, Alerter);
    if (WaitStatus  == WAIT_SUCCESS) {
        return WAIT_SUCCESS;
    }

    //
    // The enter attempt was cancelled. So, remove the allocated entry from
    // the current thread's resource table and return the appropriate
    // failure status.
    //

    FreeResourceTableEntry(ResourceTable, Entry);
    return WaitStatus;
}

//
// Tries to enter the read lock, activating the specified cancellers.
//

ULONG
WINAPI
StReentrantReadWriteLock_TryEnterReadEx (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    PRESOURCE_TABLE ResourceTable;
    ULONG ThreadId;

    ThreadId = GetCurrentThreadId();
    if (TryEnterReadLockInternal(RWLock, ThreadId, &ResourceTable)) {
        return WAIT_SUCCESS;
    }

    //
    // The read lock can't be entered immediately. So, return failure,
    // a null timeout was specified.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }

    return TryEnterReadTail(RWLock, ResourceTable, Timeout, Alerter);
}

//
// Tries to enter the read lock immediately.
//

BOOL
WINAPI
StReentrantReadWriteLock_TryEnterRead (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock
    )
{
    PRESOURCE_TABLE ResourceTable;
    ULONG ThreadId;

    ThreadId = GetCurrentThreadId();
    return TryEnterReadLockInternal(RWLock, ThreadId, &ResourceTable);
}

//
// Enters the read lock unconditionally.
//

BOOL
WINAPI
StReentrantReadWriteLock_EnterRead (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock
    )
{
    PRESOURCE_TABLE ResourceTable;
    ULONG ThreadId;

    ThreadId = GetCurrentThreadId();
    return (TryEnterReadLockInternal(RWLock, ThreadId, &ResourceTable) ||
            TryEnterReadTail(RWLock, ResourceTable, INFINITE, NULL) == WAIT_SUCCESS);
}

//
// Releases the read lock.
//

BOOL
WINAPI
StReentrantReadWriteLock_ExitRead (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock
    )
{
    PRESOURCE_TABLE ResourceTable;
    PRESOURCE_TABLE_ENTRY Entry;
    ULONG ThreadId;

    if ((ThreadId = GetCurrentThreadId()) == RWLock->FirstReader) {
        if (--RWLock->FirstReaderCount == 0) {
            RWLock->FirstReader = INVALID_THREAD_ID;
            StReadWriteLock_ExitRead(&RWLock->Lock);
        }
        return TRUE;
    }
    if ((Entry = LookupEntryOnResourceTable(RWLock, &ResourceTable)) != NULL) {
        if (--Entry->RefCount == 0) {
            StReadWriteLock_ExitRead(&RWLock->Lock);
            FreeResourceTableEntry(ResourceTable, Entry);
        }
        return TRUE;
    }

    //
    // The current isn't owner of the read lock, so return failue.
    //

    _ASSERTE(!"The Read Lock isn't owned by the current thread");
    SetLastError(ERROR_NOT_OWNER);
    return FALSE;
}

/*++
 * 
 * Write Lock Section
 *
 --*/

//
// Tries to acquire the write lock if it is free or if this
// is a recursive acquisition.
//

static
FORCEINLINE
BOOLEAN
TryEnterWriteInternal (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock,
    __in ULONG ThreadId
    )
{

#define WRITING		(1 << 30)		// Keep in synch with ReadWriteLock.c

    if (RWLock->Lock.State == 0 && CasLong(&RWLock->Lock.State, 0, WRITING)) {

        //
        // We acquired the write lock, so set the write lock owner
        // and initilize the recursive acquisition counter.
        //

        RWLock->Writer = ThreadId;
        RWLock->Count = 1;
        return TRUE;
    }

    //
    // If this is a recursive acquisition, increment the recursive
    // acquisition counter.
    //

    if (RWLock->Writer == ThreadId) {
        RWLock->Count++;
        return TRUE;
    }
    return FALSE;
}

//
// Executes the tail processing when entering the write lock.
//

static
FORCEINLINE
ULONG
TryEnterWriteTail (
    __in ULONG ThreadId,
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    LONG WaitStatus;

    if (TryEnterWriteInternal(RWLock, ThreadId = GetCurrentThreadId())) {
        return WAIT_SUCCESS;
    }

    //
    // The read lock can't be entered immediately. So, return failure,
    // if a null timeout was specified.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }

    //
    // Try to enter the associated non-reentrant r/w lock.
    //

    WaitStatus = StReadWriteLock_TryEnterWriteEx(&RWLock->Lock, Timeout, Alerter);
    if (WaitStatus == WAIT_SUCCESS) {

        //
        // The current thread is now the owner of the write lock.
        //

        RWLock->Writer = ThreadId;
        RWLock->Count = 1;
        return WAIT_SUCCESS;
    }
    
    //
    // The enter attempt was cancelled. So, return the appropriate
    // failure status.
    //

    return WaitStatus;
}

//
// Tries to enter the write lock, activating the specified cancellers.
//

ULONG
WINAPI
StReentrantReadWriteLock_TryEnterWriteEx (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ULONG ThreadId;

    if (TryEnterWriteInternal(RWLock, ThreadId = GetCurrentThreadId())) {
        return WAIT_SUCCESS;
    }

    //
    // The read lock can't be entered immediately. So, return failure,
    // if a null timeout was specified. Otherwise, execute the try enter
    // write tail processing.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }
    return TryEnterWriteTail(ThreadId, RWLock, Timeout, Alerter);
}

//
// Tries to enter the write lock immediately.
//

BOOL
WINAPI
StReentrantReadWriteLock_TryEnterWrite (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock
    )
{

    return TryEnterWriteInternal(RWLock, GetCurrentThreadId());
}

//
// Enters the write lock unconditionally.
//

BOOL
WINAPI
StReentrantReadWriteLock_EnterWrite (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock
    )
{
    ULONG ThreadId;

    return (TryEnterWriteInternal(RWLock, ThreadId = GetCurrentThreadId()) ||
            TryEnterWriteTail(ThreadId, RWLock, INFINITE, NULL) == WAIT_SUCCESS);
}

//
// Exits the write lock
//

BOOL
WINAPI
StReentrantReadWriteLock_ExitWrite (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock
    )
{
    if (GetCurrentThreadId() != RWLock->Writer) {

        _ASSERTE(!"The Write Lock isn't owned by the current thread");
        SetLastError(ERROR_NOT_OWNER);
        return FALSE;
    }
    if (--RWLock->Count == 0) {
        RWLock->Writer = INVALID_THREAD_ID;
        StReadWriteLock_ExitWrite(&RWLock->Lock);
    }
    return TRUE;
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
    return (((PST_REENTRANT_READ_WRITE_LOCK)Lock)->Writer == GetCurrentThreadId());
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
    LONG Count;
    PST_REENTRANT_READ_WRITE_LOCK RWLock = (PST_REENTRANT_READ_WRITE_LOCK)Lock;
    
    Count = RWLock->Count;
    RWLock->Count = 1;
    StReentrantReadWriteLock_ExitWrite(RWLock);
    return Count;
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
    __in LONG Count
    )
{
    PST_REENTRANT_READ_WRITE_LOCK RWLock = (PST_REENTRANT_READ_WRITE_LOCK)Lock;

    if (WaitStatus != WAIT_SUCCESS) {
        do {
            if (StReadWriteLock_EnterWrite(&RWLock->Lock)) {
                break;
            }
            Sleep(500);
        } while (TRUE);
    }
    RWLock->Writer = GetCurrentThreadId();
    RWLock->Count = Count;
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
    extern ILOCK_VTBL ILockVtblForReadWriteLock;
    
    (*ILockVtblForReadWriteLock.EnqueueWaiter)(&((PST_REENTRANT_READ_WRITE_LOCK)Lock)->Lock,
                                               WaitBlock);
}

//
// Virtual function table used by the condition variables.
//

ILOCK_VTBL ILockVtblForReentrantReadWriteLock = {
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
    PST_REENTRANT_READ_WRITE_LOCK RWLock = (PST_REENTRANT_READ_WRITE_LOCK)Waitable;

    return (RWLock->Writer == GetCurrentThreadId() && ALLOWS_ACQUIRE(&RWLock->Lock.Waitable));
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
    return TryEnterWriteInternal((PST_REENTRANT_READ_WRITE_LOCK)Waitable, GetCurrentThreadId());
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
    PST_REENTRANT_READ_WRITE_LOCK RWLock = (PST_REENTRANT_READ_WRITE_LOCK)Waitable;
    if (RWLock->Writer != GetCurrentThreadId()) {
        SetLastError(ERROR_NOT_OWNER);
        return FALSE;
    }

    StReentrantReadWriteLock_ExitWrite(RWLock);
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
    __out_opt PULONG SpinCount
    )
{
    PST_REENTRANT_READ_WRITE_LOCK RWLock = (PST_REENTRANT_READ_WRITE_LOCK)Waitable;

    //
    // Try to acquire the write lock if it is free.
    //

    if (TryEnterWriteInternal(RWLock, GetCurrentThreadId())) {
        
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

            StReentrantReadWriteLock_ExitWrite(RWLock);
        }

        //
        // Return true to signal that the wait-any operation
        // was already satisfied.
        //
        
        return TRUE;
    }

    //
    // The write lock is busy, so execute the WaitAny prologue
    // on the associated non-reentrant r/w lock.
    //

    return WAIT_ANY_PROLOGUE(&RWLock->Lock.Waitable, Parker, WaitBlock, WaitKey, SpinCount);
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
    PST_REENTRANT_READ_WRITE_LOCK RWLock = (PST_REENTRANT_READ_WRITE_LOCK)Waitable;

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
    // The write lock is busy...
    //

    return WAIT_ALL_PROLOGUE(&RWLock->Lock.Waitable, Parker, WaitBlock, SpinCount);
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
    StReentrantReadWriteLock_ExitWrite((PST_REENTRANT_READ_WRITE_LOCK)Waitable);
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
    PST_REENTRANT_READ_WRITE_LOCK RWLock = (PST_REENTRANT_READ_WRITE_LOCK)Waitable;
    CANCEL_ACQUIRE(&RWLock->Lock.Waitable, WaitBlock);
}


//
// Sets the write lock owner field and recursive acquisition count.
//

static
VOID
FASTCALL
_WaitEpilogue (
    __inout PST_WAITABLE Waitable
    )
{
    PST_REENTRANT_READ_WRITE_LOCK RWLock = (PST_REENTRANT_READ_WRITE_LOCK)Waitable;

    //
    // Set the current thread as the mutex's owner.
    //
    // NOTE: The write lock owner field can already be set.
    //

    _ASSERTE(RWLock->Writer == INVALID_THREAD_ID || RWLock->Writer == GetCurrentThreadId());
    RWLock->Writer = GetCurrentThreadId();
    RWLock->Count = 1;
}

//
// Virtual function table that implements the waitable functionality
// for the reentrant r/w lock.
//

WAITABLE_VTBL ReentrantReadWriteLockVtbl = {
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
// Initializes the reentrant read write lock.
//

VOID
WINAPI
StReentrantReadWriteLock_Init (
    __out PST_REENTRANT_READ_WRITE_LOCK RWLock,
    __in ULONG SpinCount
    )
{
    RWLock->Waitable.Vptr = &ReentrantReadWriteLockVtbl;
    StReadWriteLock_Init(&RWLock->Lock, SpinCount);
    RWLock->FirstReader = RWLock->Writer = INVALID_THREAD_ID;
}
