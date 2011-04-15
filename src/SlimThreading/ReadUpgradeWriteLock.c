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
// Thrad identifier used for owner when the locks are free.
//

#define	UNOWNED	0

//
// The amount of spinning when trying to acquirie the spinlock.
//

#define	SPIN_LOCK_SPINS	100


//
// Initialized the r/u/w lock.
//

VOID
WINAPI
StReadUpgradeWriteLock_InitializeEx (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock,
    __in BOOL SupportsReentrancy,
    __in ULONG SpinCount
    )
{
    InitializeSpinLock(&Lock->SLock, SPIN_LOCK_SPINS);
    InitializeListHead(&Lock->ReadersQueue);
    InitializeListHead(&Lock->WritersQueue);
    InitializeListHead(&Lock->UpgradersQueue);
    Lock->UpgradeToWriteWaiter = NULL;
    Lock->Readers = 0;
    Lock->Writer = Lock->Upgrader = UNOWNED;
    Lock->IsReentrant = SupportsReentrancy;
    Lock->SpinCount = IsMultiProcessor() ? SpinCount : 0;
}

//
// Tries to wakeup a waiting writer.
//

FORCEINLINE
BOOLEAN
TryWakeupWriter (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock
    )
{
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;

    ListHead = &Lock->WritersQueue;
    if (!IsListEmpty(ListHead)) {
        Entry = ListHead->Flink;
        do {
            PWAIT_BLOCK WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            RemoveEntryList(Entry);
            if (TryLockParker(WaitBlock->Parker)) {

                //
                // The waiter becomes the next owner of the write lock.
                // So, release the spinlock, unpark the waiter and return true.
                //

                Lock->Writer = WaitBlock->Request;
                ReleaseSpinLock(&Lock->SLock);
                UnparkThread(WaitBlock->Parker, WAIT_SUCCESS);
                return TRUE;
            }

            //
            // The enter attempt was cancelled; so, mark the wait block
            // as unlinked.
            //

            Entry->Flink = Entry;
        } while ((Entry = ListHead->Flink) != ListHead);
    }
    
    //
    // Since that no writer was released, return false still
    // holding the spinlock.
    //

    return FALSE;
}

//
// Tries to wakeup a waiting upgrader and/or all waiting readers.
//

BOOLEAN
FASTCALL
TryWakeupUpgraderAndReaders (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock
    )
{
    WAKE_LIST WakeList;
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    PWAIT_BLOCK WaitBlock;
    BOOLEAN Released;

    //
    // Initialize the wakeup list and the released flag.
    //

    InitializeWakeList(&WakeList);
    Released = FALSE;
    ListHead = &Lock->UpgradersQueue;

    //
    // If the lock isn't in the upgrade read mode, try to wake up
    // a waiting upgrader.
    //

    if (!IsListEmpty(ListHead)) {
        Entry = ListHead->Flink;
        do {
            WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            RemoveEntryList(Entry);
            if (TryLockParker(WaitBlock->Parker)) {

                //
                // Make the locked waiter as the current upgrader and add its
                // wait block to the wake up list.
                //

                Lock->Upgrader = WaitBlock->Request;
                AddEntryToWakeList(&WakeList, Entry);
                Released = TRUE;
                break;
            }
            Entry->Flink = Entry;
        } while ((Entry = ListHead->Flink) != ListHead);
    }

    //
    // Even when the r/w lock state changes to the upgrade read mode,
    // all the waiting readers can enter the read lock.
    //

    ListHead = &Lock->ReadersQueue;
    if (!IsListEmpty(ListHead)) {
        Entry = ListHead->Flink;
        do {
            WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            if (TryLockParker(WaitBlock->Parker)) {
                    
                //
                // Account for one more active reader and add its wait block
                // to the wake up list.
                //
                // NOTE: It is the reader that, later, adds an entry to its
                //		 private resource table.
                //
                
                Lock->Readers++;
                AddEntryToWakeList(&WakeList, Entry);
                Released = TRUE;
            } else {
                Entry->Flink = Entry;
            }
        } while ((Entry = ListHead->Flink) != ListHead); 
    }
    
    //
    // If no thread was released, return false holding the
    // spinlock; otherwise, release the spinlock, unpark all
    // threads inserted in the wake up list and return true.
    //

    if (!Released) {
        return FALSE;
    }
    ReleaseSpinLock(&Lock->SLock);
    UnparkWakeList(&WakeList);
    return TRUE;
}

//
// Unlinks the specified entry from the wait where it is inserted.
//

static
FORCEINLINE
VOID
UnlinkListEntry (
    __inout PSPINLOCK SLock,
    __in PLIST_ENTRY_ Entry
    )
{
    if (Entry->Flink != Entry) {
        AcquireSpinLock(SLock);
        if (Entry->Flink != Entry) {
            RemoveEntryList(Entry);
        }
        ReleaseSpinLock(SLock);
    }
}

//
// Tries to enter the read lock, activating the specified cancellers.
//

ULONG
WINAPI
StReadUpgradeWriteLock_TryEnterReadEx (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ULONG ThreadId;
    PRESOURCE_TABLE ResourceTable;
    PRESOURCE_TABLE_ENTRY OwnerEntry;
    PLIST_ENTRY_ ListHead;
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    ULONG SpinCount;
    ULONG WaitStatus;

    //
    // Get current thread id and lookup the thread's resource table
    // for an entry associated to this lock.
    //

    ThreadId = GetCurrentThreadId();
    OwnerEntry = LookupEntryOnResourceTable(Lock, &ResourceTable);
    if (!Lock->IsReentrant) {
        if (ThreadId == Lock->Writer || OwnerEntry != NULL) {

            //
            // Error conditions:
            // - Enter read lock after enter write lock isn't allowed;
            // - Recursive enter read not allowed.
            //

            SetLastError(ERROR_NOT_SUPPORTED);
            return WAIT_FAILED;
        }
    } else {

        //
        // If this is a recursive enter, increment the recursive acquisition
        // counter and return success.
        //

        if (OwnerEntry != NULL) {
            OwnerEntry->RefCount++;
            return WAIT_SUCCESS;
        }
    }

    //
    // Acquire the spinlock that protected the r/u/w lock shared state.
    //

    AcquireSpinLock(&Lock->SLock);

    //
    // If the current thread is the lock upgrader, it can also enter
    // the read lock and this fact reported by the *UpgraderIdReader*
    // flag. So, release the spinlock, add an entry to the thread's resource
    // table and return success.
    //

    if (Lock->Upgrader == ThreadId) {
        ReleaseSpinLock(&Lock->SLock);
        Lock->UpgraderIsReader = TRUE;
        goto AddEntryAndReturnSuccess;
    }

    //
    // The read lock can be entered, if the r/w lock isn't in write
    // mode and no thread is waiting to enter the writer mode.
    // Also, if the r/w lock is reentrant and the current thread is
    // the lock writer, the read lock can also be entered.
    //
    // So, if the
    // read lock can be entered, increment the number of active readers,
    // add an entry to the thread's resource table, release the spinlock
    // and return success.
    //

    if ((Lock->Writer == UNOWNED && IsListEmpty(&Lock->WritersQueue) &&
         Lock->UpgradeToWriteWaiter == NULL) || (Lock->IsReentrant && Lock->Writer == ThreadId)) {               

        //
        // Increment the number of active readers, release the spinlock,
        // add an entry to th thread's resource table and return success.
        //
                    
        Lock->Readers++;
        ReleaseSpinLock(&Lock->SLock);
        goto AddEntryAndReturnSuccess;
    }

    //
    // The current thread can't enter the read lock immediately.
    // So, if a null timeout was specified, release the spinlock
    // and return failure.
    //

    if (Timeout == 0) {
        ReleaseSpinLock(&Lock->SLock);
        return WAIT_TIMEOUT;
    }

    //
    // Initialize a parker and a wait node  and insert it in
    // the readers queue, computing also the amount of spinning.
    //

    ListHead = &Lock->ReadersQueue;
    SpinCount = IsListEmpty(ListHead) ? Lock->SpinCount : 0;
    InitializeParkerAndWaitBlock(&WaitBlock, &Parker, ThreadId, WaitAny, WAIT_SUCCESS);
    InsertTailList(ListHead, &WaitBlock.WaitListEntry);

    //
    // Release the spinlock and park the current thread, activating
    // the specified cancellers and spinning if appropriate.
    //

    ReleaseSpinLock(&Lock->SLock);
    WaitStatus = ParkThreadEx(&Parker, SpinCount, Timeout, Alerter);

    //
    // If we entered the read lock, add an entry to the thread's
    // resource table and return success.
    //

    if (WaitStatus == WAIT_SUCCESS) {

AddEntryAndReturnSuccess:

        AddEntryToResourceTable(ResourceTable, Lock);
        return WAIT_SUCCESS;
    }

    //
    // The enter attempt was cancelled. So, ensure that the wait block
    // is unlinked from the readers queue and return the failure status.
    //

    UnlinkListEntry(&Lock->SLock, &WaitBlock.WaitListEntry);
    return WaitStatus;
}

//
// Tries to enter the read lock immediately.
//

BOOL
WINAPI
StReadUpgradeWriteLock_TryEnterRead (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock
    )
{
    return StReadUpgradeWriteLock_TryEnterReadEx(Lock, 0, NULL) == WAIT_SUCCESS;
}

//
// Enters the read lock unconditionally.
//

BOOL
WINAPI
StReadUpgradeWriteLock_EnterRead (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock
    )
{
    return StReadUpgradeWriteLock_TryEnterReadEx(Lock, INFINITE, NULL) == WAIT_SUCCESS;
}

//
// Exits the read lock.
//

BOOL
WINAPI
StReadUpgradeWriteLock_ExitRead (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock
    )
{
    PRESOURCE_TABLE ResourceTable;
    PRESOURCE_TABLE_ENTRY OwnerEntry;
    PLIST_ENTRY_ Entry;

    //
    // Lookup for an entry in the thread's resource table associated
    // to the lock.
    //

    OwnerEntry = LookupEntryOnResourceTable(Lock, &ResourceTable);
    if (OwnerEntry == NULL) {

        //
        // Return failure because the current thread isn't an owner
        // of the read lock.
        //

        SetLastError(ERROR_NOT_OWNER);
        return FALSE;
    }

    //
    // If this is a recursive exit, decrement the recursive acquisition
    // counter and return success.
    //

    if (--OwnerEntry->RefCount > 0) {
        return TRUE;
    }

    //
    // The read lock was exited by the current thread. So, if the current
    // thread is also the upgrader clear the UpgraderIsReader flag and return,
    // because no waiter thread can be released.
    //

    if (Lock->Upgrader == GetCurrentThreadId()) {
        Lock->UpgraderIsReader = FALSE;
        return TRUE;
    }

    //
    // Acquire the spinlock that protects the shared r/u/w lock shared state.
    // Then, decrement the number of active readers; if this is not the last
    // reader, release the spinlock and return.
    //

    AcquireSpinLock(&Lock->SLock);
    if (--Lock->Readers > 0) {
        ReleaseSpinLock(&Lock->SLock);
        return TRUE;
    }
    
    //
    // Here, we know that the r/w lock doesn't have active readers.
    // First, check the current upgarder is waiting to enter the write lock,
    // and, if so, try to release it.
    //

    Entry = Lock->UpgradeToWriteWaiter;
    if (Entry != NULL) {
        PWAIT_BLOCK WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
        Lock->UpgradeToWriteWaiter = NULL;
        if (TryLockParker(WaitBlock->Parker)) {
            Lock->Writer = WaitBlock->Request;
            ReleaseSpinLock(&Lock->SLock);
            UnparkThread(WaitBlock->Parker, WAIT_SUCCESS);
            return TRUE;
        }
        Entry->Flink = Entry;
    }

    //
    // If the r/w lock isn't in the upgrade read mode nor in write mode,
    // try to wake up a waiting writer; failing that, try to wake up a
    // waiting and/or all waiting readers. Anyway, release the spinlock
    // and return success.
    //

    if (!(Lock->Upgrader == UNOWNED && Lock->Writer == UNOWNED &&
         (TryWakeupWriter(Lock) || TryWakeupUpgraderAndReaders(Lock)))) {
        ReleaseSpinLock(&Lock->SLock);
    }
    return TRUE;
}

//
// Tries to enter the write lock, activating the specified cancellers.
//

ULONG
WINAPI
StReadUpgradeWriteLock_TryEnterWriteEx (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ULONG ThreadId;
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    ULONG SpinCount;
    ULONG WaitStatus;

    ThreadId = GetCurrentThreadId();
    if (!Lock->IsReentrant) {
        if (Lock->Writer == ThreadId || LookupEntryOnResourceTable(Lock, NULL) == NULL) {

            //
            // Error conditions:
            // - Recursive enter write not allowed.
            // - Write after read not allowed.
            //

            SetLastError(ERROR_NOT_SUPPORTED);
            return WAIT_FAILED;
        }
    } else {

        //
        // If this is a recursive enter, increment the recursive acquisition
        // counter and return success.
        //

        if (Lock->Writer == ThreadId) {
            Lock->WrCount++;
            return WAIT_SUCCESS;
        }
    }

    //
    // Acquire the spinlock that protects the r/w lock shared state.
    //

    AcquireSpinLock(&Lock->SLock);

    //
    // If the write lock can be entered - this is, there are no active
    // readers, no writer, no upgrader or the current thread is the
    // upgrader -, enter the write lock, release the spinlock and
    // return success.
    //

    if (Lock->Readers == 0 && Lock->Writer == UNOWNED &&
        (Lock->Upgrader == UNOWNED || Lock->Upgrader == ThreadId)) {		
        Lock->Writer = ThreadId;
        ReleaseSpinLock(&Lock->SLock);
        Lock->WrCount = 1;
        return WAIT_SUCCESS;
    }

    //
    // If the current thread isn't the current upgrader but it is
    // a lock reader, release the spinlock and return failure.
    //

    if (Lock->Upgrader != ThreadId && LookupEntryOnResourceTable(Lock, NULL) != NULL) {

        //
        // Write after read not allowed.
        //

        ReleaseSpinLock(&Lock->SLock);
        SetLastError(ERROR_NOT_SUPPORTED);
        return WAIT_FAILED;
    }

    //
    // The write lock can't be entered immediately. So, if a null timeout
    // was specified, release the spinlock and return failure.
    //

    if (Timeout == 0) {
        ReleaseSpinLock(&Lock->SLock);
        return WAIT_TIMEOUT;
    }

    //
    // The current thread needs to wait, so initialize a parker
    // and a wait block. If the current thread isn't the lock upgrader,
    // the wait block is inserted in the writer's queue; otherwise, the
    // wait block becomes referenced by the *UpgradeToWriteWaiter* field.
    //

    InitializeParkerAndWaitBlock(&WaitBlock, &Parker, ThreadId, WaitAny, WAIT_SUCCESS);
    if (Lock->Upgrader == ThreadId) {
        Lock->UpgradeToWriteWaiter = &WaitBlock.WaitListEntry;
        WaitBlock.WaitListEntry.Flink = NULL;
        SpinCount = Lock->SpinCount;
    } else {
        PLIST_ENTRY_ ListHead = &Lock->WritersQueue;
        SpinCount = IsListEmpty(ListHead) ? Lock->SpinCount : 0;
        InsertTailList(ListHead, &WaitBlock.WaitListEntry);
    }

    //
    // Release the spinlock and park the current thread, activating
    // the specified cancellers and spinning if appropriate.
    //

    ReleaseSpinLock(&Lock->SLock);
    WaitStatus = ParkThreadEx(&Parker, SpinCount, Timeout, Alerter);

    //
    // If the thread entered the write lock, initialize the recursive
    // acquisition counter and return success.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        Lock->WrCount = 1;
        return WAIT_SUCCESS;
    }

    //
    // The enter attempt was cancelled. If the wait block was already
    // unlinked, return the failure status. Otherwise, unlink the wait
    // block from the queue and take into account the other waiters.
    //

    if (WaitBlock.WaitListEntry.Flink == &WaitBlock.WaitListEntry) {
        return WaitStatus;
    }
    AcquireSpinLock(&Lock->SLock);
    if (WaitBlock.WaitListEntry.Flink != &WaitBlock.WaitListEntry) {
        if (Lock->UpgradeToWriteWaiter == &WaitBlock.WaitListEntry) {
            Lock->UpgradeToWriteWaiter = NULL;
        } else {
            RemoveEntryList(&WaitBlock.WaitListEntry);
        
            //
            // If the writers queue becomes empty, it is possible that there
            // is a waiting upgrader or waiting reader threads that can now
            // proceed.
            //

            if (Lock->Writer == UNOWNED && Lock->Upgrader == UNOWNED &&
                IsListEmpty(&Lock->WritersQueue) && TryWakeupUpgraderAndReaders(Lock)) {
                return WaitStatus;
            }
        }
    }
    ReleaseSpinLock(&Lock->SLock);
    return WaitStatus;
}

//
// Tries to enter the write lock immediately.
//

BOOL
WINAPI
StReadUpgradeWriteLock_TryEnterWrite (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock
    )
{
    return StReadUpgradeWriteLock_TryEnterWriteEx(Lock, 0, NULL) == WAIT_SUCCESS;
}

//
// Enters the write lock unconditionally.
//

BOOL
WINAPI
StReadUpgradeWriteLock_EnterWrite (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock
    )
{
    return StReadUpgradeWriteLock_TryEnterWriteEx(Lock, INFINITE, NULL) == WAIT_SUCCESS;
}

//
// Exits the write lock.
//

BOOL
WINAPI
StReadUpgradeWriteLock_ExitWrite (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock
    )
{
    ULONG ThreadId;

    ThreadId = GetCurrentThreadId();
    if (Lock->Writer != ThreadId) {

        //
        // Mismatched write.
        //
        
        SetLastError(ERROR_NOT_OWNER);
        return FALSE;
    }

    //
    // If this is a recursive exit, decrement the recursive acquistion
    // counter and return.
    //

    if (--Lock->WrCount > 0) {
        return TRUE;
    }

    //
    // Acquire the spinlock that protects the r/w lock shared state.
    //

    AcquireSpinLock(&Lock->SLock);

    //
    // Clear the current writer field and if the current thread isn't
    // also the lock upgrader, try to wake up an upgrader and/or all
    // waiting readers. Failing that, try to wake up a waiting writer.
    // Anyway, release the spinlock and return success.

    Lock->Writer = UNOWNED;
    if (!(Lock->Upgrader == UNOWNED &&
        (TryWakeupUpgraderAndReaders(Lock) || TryWakeupWriter(Lock)))) {
        ReleaseSpinLock(&Lock->SLock);
    }
    return TRUE;
}

//
// Tries to enter the lock in upgrade read mode, activating the
// specified cancellers.
// 

ULONG
WINAPI
StReadUpgradeWriteLock_TryEnterUpgradeableReadEx (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    PRESOURCE_TABLE_ENTRY OwnerEntry;
    ULONG ThreadId;
    PLIST_ENTRY_ ListHead;
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    ULONG SpinCount;
    ULONG WaitStatus;

    //
    // Get the current thread and lookup for an entry in the thread's
    // resource table associated to this lock.
    //

    ThreadId = GetCurrentThreadId();
    OwnerEntry = LookupEntryOnResourceTable(Lock, NULL);
    if (!Lock->IsReentrant) {
        if (ThreadId == Lock->Upgrader || ThreadId == Lock->Writer || OwnerEntry != NULL) {
            
            //
            // Error conditions:
            // - Recursive upgrade not allowed;
            // - Upgrade after write not allowed;
            // - Upgrade after read not allowed.
            //

            SetLastError(ERROR_NOT_SUPPORTED);
            return WAIT_FAILED;
        }
    } else {

        //
        // If the current thread is lock upgrader, increment the recursive
        // acquisition counter and return success.
        //

        if (Lock->Upgrader == ThreadId) {
            Lock->UpgCount++;
            return WAIT_SUCCESS;
        }

        //
        // If the current thread is the lock writer, it can also becomes
        // the upgrader. If it is also a reader, it will be accounted as
        // a reader in the *UpgraderIsReader* flag.
        //

        if (Lock->Writer == ThreadId) {
            Lock->Upgrader = ThreadId;
            Lock->UpgCount = 1;
            if (OwnerEntry != NULL) {
                Lock->UpgraderIsReader = TRUE;
            }
            return WAIT_SUCCESS;
        }

        //
        // Enter upgradeable read mode after enter read lock isn't allowed.
        //

        if (OwnerEntry != NULL) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return WAIT_FAILED;
        }
    }

    //
    // Acquire the spinlock that protects the r/w lock shared state.
    //

    AcquireSpinLock(&Lock->SLock);

    //
    // If the lock isn't in write or upgrade read mode, the current
    // thread becomes the lock upgrader. Then, release the spinlock
    // and return success.
    //

    if (Lock->Writer == UNOWNED && Lock->Upgrader == UNOWNED) {
        Lock->Upgrader = ThreadId;
        ReleaseSpinLock(&Lock->SLock);
        Lock->UpgraderIsReader = FALSE;
        Lock->UpgCount = 1;
        return WAIT_SUCCESS;
    }

    //
    // The upgrade read lock can't be acquired immediately.
    // So, return failure, if a null timeout was specified.
    //

    if (Timeout == 0) {
        ReleaseSpinLock(&Lock->SLock);
        return WAIT_TIMEOUT;
    }

    //
    // The current thread must wait. So, initialize a parker and
    // a wait block and insert it in the upgraders queue.
    //

    ListHead = &Lock->UpgradersQueue; 
    SpinCount = (IsListEmpty(ListHead) && IsListEmpty(&Lock->WritersQueue)) ? Lock->SpinCount : 0;
    InitializeParkerAndWaitBlock(&WaitBlock, &Parker, ThreadId, WaitAny, WAIT_SUCCESS);
    InsertTailList(ListHead, &WaitBlock.WaitListEntry);

    // 
    // Release the spinlock and park the current thread activating the specified
    // cancellers and spinning if appropriate.
    //

    ReleaseSpinLock(&Lock->SLock);
    WaitStatus = ParkThreadEx(&Parker, SpinCount, Timeout, Alerter);

    //
    // If the upgrade lock was entered, initialize the recursive acquisition
    // counter, clear the *UpgraderIsReader* flag and return success.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        Lock->UpgCount = 1;
        Lock->UpgraderIsReader = FALSE;
        return WAIT_SUCCESS;
    }

    //
    // The enter attempt was cancelled. So, ensure that the wait block
    // is unlinked and return the failure status.
    //

    UnlinkListEntry(&Lock->SLock, &WaitBlock.WaitListEntry);
    return WaitStatus;
}

//
// Tries to enter the lock in upgrade read mode immediately.
// 

BOOL
WINAPI
StReadUpgradeWriteLock_TryEnterUpgradeableRead (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock
    )
{
    return StReadUpgradeWriteLock_TryEnterUpgradeableReadEx(Lock, 0, NULL) == WAIT_SUCCESS;
}

//
// Enters the lock in upgrade read mode unconditionally.
// 

BOOL
WINAPI
StReadUpgradeWriteLock_EnterUpgradeableRead (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock
    )
{
    return StReadUpgradeWriteLock_TryEnterUpgradeableReadEx(Lock, INFINITE, NULL) == WAIT_SUCCESS;
}

//
// Exits the upgradeable read lock.
//

BOOL
WINAPI
StReadUpgradeWriteLock_ExitUpgradeableRead (
    __inout PST_READ_UPGRADE_WRITE_LOCK Lock
    )
{
    ULONG ThreadId;

    ThreadId = GetCurrentThreadId();
    if (Lock->Upgrader != ThreadId) {
        
        //
        // Mismatched upgrade.
        //

        SetLastError(ERROR_NOT_OWNER);
        return FALSE;
    }

    //
    // If this is a recursive exit, decrement the recursive acquisition
    // counter and return success.
    //

    if (--Lock->UpgCount > 0) {
        return TRUE;
    }

    //
    // Acquire the spinlock that protects the r/w lock shared state.
    // If the upgrader is also a lock reader, increment the number
    // of active readers.
    //

    AcquireSpinLock(&Lock->SLock);
    if (Lock->UpgraderIsReader) {
        Lock->Readers++;
    }
    Lock->Upgrader = UNOWNED;

    //
    // If there are no active readers or writer, try to wake up a writer;
    // failing that, try to wakeup a waiting upgrader and  waiting readers.
    //

    if (!(Lock->Readers == 0 && Lock->Writer == UNOWNED &&
          (TryWakeupWriter(Lock) || TryWakeupUpgraderAndReaders(Lock)))) {
        ReleaseSpinLock(&Lock->SLock);
    }
    return TRUE;
}
