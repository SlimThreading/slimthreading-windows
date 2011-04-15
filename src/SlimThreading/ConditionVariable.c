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
// Initializes the specified condition variable to be used
// on a monitor protected by a Lock.
//

VOID
WINAPI
StConditionVariable_InitForLock (
  __out PST_CONDITION_VARIABLE Condition
    )
{
    extern ILOCK_VTBL ILockVtblForLock;

    Condition->LockVtbl = &ILockVtblForLock;
    InitializeListHead(&Condition->WaitList);
}

//
// Initializes the specified condition variable to be used
// on a monitor protected by a reentrant lock.
//

VOID
WINAPI
StConditionVariable_InitForReentrantLock (
  __out PST_CONDITION_VARIABLE Condition
    )
{
    extern ILOCK_VTBL ILockVtblForReentrantLock;

    Condition->LockVtbl = &ILockVtblForReentrantLock;
    InitializeListHead(&Condition->WaitList);
}

//
// Initializes the specified condition variable to be used on a monitor
// protected by a fair lock.
//

VOID
WINAPI
StConditionVariable_InitForFairLock (
  __out PST_CONDITION_VARIABLE Condition
    )
{
    extern ILOCK_VTBL ILockVtblForFairLock;

    Condition->LockVtbl = &ILockVtblForFairLock;
    InitializeListHead(&Condition->WaitList);
}

//
// Initializes the specified condition variable to be used on a monitor
// protected by a Read Write Lock.
//

VOID
WINAPI
StConditionVariable_InitForReadWriteLock (
  __out PST_CONDITION_VARIABLE Condition
    )
{
    extern ILOCK_VTBL ILockVtblForReadWriteLock;

    Condition->LockVtbl = &ILockVtblForReadWriteLock;
    InitializeListHead(&Condition->WaitList);
}

//
// Initializes the specified condition variable to be used on a monitor
// protected by a Reentrant Read Write Lock.
//

VOID
WINAPI
StConditionVariable_InitForReentrantReadWriteLock (
  __out PST_CONDITION_VARIABLE Condition
    )
{
    extern ILOCK_VTBL ILockVtblForReentrantReadWriteLock;

    Condition->LockVtbl = &ILockVtblForReentrantReadWriteLock;
    InitializeListHead(&Condition->WaitList);
}

//
// Waits on the condition variable until notification, activating
// the specified cancellers.
//

ULONG
WINAPI
StConditionVariable_WaitEx (
  __inout PST_CONDITION_VARIABLE Condition,
  __inout PVOID Lock,
  __in ULONG Timeout,
  __inout PST_ALERTER Alerter
    )
{
    PILOCK_VTBL LockVtbl;
    ST_PARKER Parker; 
    WAIT_BLOCK WaitBlock;
    LONG LockState;
    ULONG WaitStatus;

    //
    // Initialize the local variable.

    LockVtbl = Condition->LockVtbl;

    //
    // If the current thread is not the owner of the specified lock,
    // return a failure status.
    //
    
    if (!(*LockVtbl->IsOwned)(Lock)) {
        SetLastError(ERROR_NOT_OWNER);
        return WAIT_FAILED;	
    }
    
    //
    // Initialize the parker and the wait block and insert the wait
    // block in the condition's wait list.
    //
    
    InitializeParkerAndWaitBlockEx(&WaitBlock, &Parker, 0, Lock, WaitAny, WAIT_SUCCESS);
    InsertTailList(&Condition->WaitList, &WaitBlock.WaitListEntry);
    
    //
    // Release the associated lock completedly and save its current state.
    //
    
    LockState = (*LockVtbl->ReleaseCompletely)(Lock);
    
    //
    // Park the current thread, activating the specified cancellers.
    //
    
    WaitStatus = ParkThreadEx(&Parker, 0, Timeout, Alerter);
        
    //
    // Re-acquire the associated lock, restoring its previous state.
    //
    
    (*LockVtbl->Reacquire)(Lock, WaitStatus, LockState);
    
    //
    // If the current thread was actually notified, return success.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        return WAIT_SUCCESS;
    }

    //
    // The wait was cancelled due to timeout or alert. So, remove our
    // wait block from the condition's queue, if it is still inserted,
    // and return the appropriate failure status.
    //

    if (WaitBlock.WaitListEntry.Flink != &WaitBlock.WaitListEntry) {		
        RemoveEntryList(&WaitBlock.WaitListEntry);			
    }
    return WaitStatus;
}

//
// Waits unconditionally on the condition variable until notification.
//

BOOL
WINAPI
StConditionVariable_Wait (
  __inout PST_CONDITION_VARIABLE Condition,
  __inout PVOID Lock
    )
{
    return StConditionVariable_WaitEx(Condition, Lock, INFINITE, NULL);
}

//
// Notifies a thread waiting on the specified condition variable.
//
    
VOID
WINAPI
StConditionVariable_Notify (
    __inout PST_CONDITION_VARIABLE Condition
    )
{
    
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    PWAIT_BLOCK WaitBlock;

    ListHead = &Condition->WaitList;
    if ((Entry = ListHead->Flink) != ListHead) {
        do {	
            //
            // Remove the entry from the condition's wait list and compute
            // the wait block address.
            //
        
            RemoveEntryList(Entry);
            WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
        
            //
            // Try to lock the parker associated to the wait block.
            //
        
            if (TryLockParker(WaitBlock->Parker)) {

                //
                // Insert the wait block in the lock's wait list and return.
                //

                (*Condition->LockVtbl->EnqueueWaiter)(WaitBlock->Channel, WaitBlock);
                break;
            } else {
        
                //
                // The wait was cancelled due to timeout, so mark the
                // wait block as unlinked.
                //
            
                Entry->Flink = Entry;
            }
        } while ((Entry = ListHead->Flink) != ListHead);
    }
}

//
// Notifies all threads waiting on the specified condition variable.
//


VOID
WINAPI
StConditionVariable_NotifyAll (
  __inout PST_CONDITION_VARIABLE Condition
    )
{
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    PLIST_ENTRY_ Next;
    PWAIT_BLOCK WaitBlock;

    //
    // Initialize the local variable.
    //

    ListHead = &Condition->WaitList;
    if ((Entry = ListHead) != ListHead) {

        //
        // Release all threads waiting on the condition queue.
        //
        
        do {

            Next = Entry->Flink;
            WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
                
            //
            // Try to lock the associated parker.
            //
        
            if (TryLockParker(WaitBlock->Parker)) {

                //
                // We locked the waiter, so insert its wait block in
                // the lock's wait list.
                //
        
                (*Condition->LockVtbl->EnqueueWaiter)(WaitBlock->Channel, WaitBlock);
            } else {

                //
                // The wait was cancelled, so mark the wait block as unlinked.
                //
            
                Entry->Flink = Entry;
            }
        } while ((Entry = Next) != ListHead);
        InitializeListHead(ListHead);
    }
}
