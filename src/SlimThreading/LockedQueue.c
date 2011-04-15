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
// Special request type used with lock acquire wait blocks.
//

#define	LOCK_ONLY_REQUEST	(LOCKED_REQUEST | SPECIAL_REQUEST)

//
// Processes the wait blocks present in the lock's queue.
//

static
FORCEINLINE
BOOLEAN
ProcessLockQueue (
    __in PLIST_ENTRY_ First,
    __in PLIST_ENTRY_ Last,
    __inout PLIST_ENTRY_ *LockQueue
    )
{
    PLIST_ENTRY_ Next;
    PWAIT_BLOCK WaitBlock;
    PST_PARKER Parker;
    BOOLEAN QueueChanges = FALSE;

    do {
        Next = First->Flink;
        WaitBlock = CONTAINING_RECORD(First, WAIT_BLOCK, WaitListEntry);
        Parker = WaitBlock->Parker;

        if (WaitBlock->Request == LOCK_ONLY_REQUEST) {
            First->Flink = *LockQueue;
            *LockQueue = First;
        } else  if (!IsParkerLocked(Parker) || WaitBlock->Request < 0) {

            //
            // Valid request, so, move the wait block from the lock's
            // queue to the locked queue.
            //

            InsertHeadList(Last, First);
            QueueChanges = TRUE;

        } else {

            //
            // This request was cancelled; so, mark the wait block
            // as unlinked.
            //

            First->Flink = First;
        }
    } while ((First = Next) != NULL);
    return QueueChanges;
}

//
// Enqueues a wait block in the specified locked queue.
//
// NOTE: This is the slow path that is called when the queue's
//		 lock is busy.
//

BOOLEAN
FASTCALL
SlowEnqueueInLockedQueue (
    __inout PLOCKED_QUEUE Queue,
    __inout PWAIT_BLOCK WaitBlock,
    __out BOOLEAN *FrontHint
    )
{
    PLIST_ENTRY_ ListHead = &Queue->Head;

    do {
        PLIST_ENTRY_ State;

        //
        // If the lock is free, try to acquire it and, if succeed, insert
        // the wait block in the locked queue.
        //

        if ((State = Queue->LockState) == LOCK_FREE) {
            if (CasPointer(&Queue->LockState, LOCK_FREE, LOCK_BUSY)) {
                InsertTailList(ListHead, &WaitBlock->WaitListEntry);
                *FrontHint = (ListHead->Flink == &WaitBlock->WaitListEntry);
                Queue->FrontRequest = 0;
                Queue->LockPrivateQueue = NULL;
                return TRUE;
            }
            continue;
        }

        //
        // The lock is busy; so, try to insert the wait block in the
        // lock's queue.
        //
            
        WaitBlock->WaitListEntry.Flink = State;
        if (CasPointer(&Queue->LockState, State, &WaitBlock->WaitListEntry)) {
            *FrontHint = (State == NULL && IsListEmpty(ListHead));
            return FALSE;
        }
    } while (TRUE);
}

//
// Tries to unlock the locked queue.
//
// NOTE: This is the slow path that is called when the lock's
//		 wait list is not empty.
//

BOOLEAN
FASTCALL
SlowTryUnlockLockedQueue (
    __inout PLOCKED_QUEUE Queue,
    __in BOOLEAN ForceUnlock
    )
{
    PLIST_ENTRY_ Entry;
    PLIST_ENTRY_ ListHead = &Queue->Head;

    //
    // If the force unlock flag wasn't specified, but the queue
    // isn't empty, set the force unlock flag. In this situation,
    // if a new waiter is transferred to the queue, it will not
    // inserted at the front of the queue. 
    //

    if (!ForceUnlock && !IsListEmpty(ListHead)) {
        ForceUnlock = TRUE;
    }

    do {
        PLIST_ENTRY_ State;		
        if ((State = Queue->LockState) == LOCK_BUSY) {
            Queue->FrontRequest = ((Entry = ListHead->Flink) == ListHead) ? 0 :
                (CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry)->Request & MAX_REQUEST);

            Entry = Queue->LockPrivateQueue;
            if (CasPointer(&Queue->LockState, LOCK_BUSY, LOCK_FREE)) {

                //
                // If there are threads on the lock private queue, unpark them.
                //

                while (Entry != NULL) {
                    PLIST_ENTRY_ Next = Entry->Flink;
                    UnparkThread(CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry)->Parker,
                                 WAIT_SUCCESS);
                    Entry = Next;
                }
                return TRUE;
            }
            Queue->FrontRequest = 0;
            continue;
        }
        
        //
        // The lock's queue isn't empty; so, empty the lock's queue
        // and process the wait blocks that were inserted in it.
        //
            
        if (CasPointer(&Queue->LockState, State, NULL)) {
            if (ProcessLockQueue(State, ListHead->Blink, &Queue->LockPrivateQueue) && !ForceUnlock) {
                return FALSE;
            }
        }
    } while (TRUE);
}

//
// Locks the queue.
// ...
//

BOOLEAN
FASTCALL
LockLockedQueue (
    __inout PLOCKED_QUEUE Queue,
    __in_opt PLIST_ENTRY_ Entry
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    ULONG SpinCount;
    PLIST_ENTRY_ State;

    //
    // Mark the wait block as uninitialized.
    //
    
    WaitBlock.Parker = NULL;

    do {

        //
        // Try to acquire spinning for the specified number of cycles.
        //

        SpinCount = Queue->SpinCount;
        do {
            if (Entry != NULL && Entry->Flink == Entry) {
                return FALSE;
            }
            if ((State = Queue->LockState) == LOCK_FREE) {
                if (CasPointer(&Queue->LockState, LOCK_FREE, LOCK_BUSY)) {
                    Queue->FrontRequest = 0;
                    Queue->LockPrivateQueue = NULL;
                    return TRUE;
                }
                continue;
            }
            if (State != LOCK_BUSY || SpinCount-- <= 0) {
                break;
            }
            SpinWait(1);
        } while (TRUE); 
    
        //
        // It seems that the lock is busy; so, initialize the wait
        // block if it isn't initialized.
        //

        if (WaitBlock.Parker == NULL) {
            InitializeWaitBlock(&WaitBlock, &Parker, LOCK_ONLY_REQUEST, WaitAny, WAIT_SUCCESS);
        }

        //
        // Initializes the parker as locked.
        //

        InitializeParker(&Parker, 0);

        //
        // Try to insert our wait block in the lock's queue
        // or acquire the lock if it seems free.
        //

        do {
            if (Entry != NULL && Entry->Flink == Entry) {
                return FALSE;
            }
            if ((State = Queue->LockState) == LOCK_FREE) {
                if (CasPointer(&Queue->LockState, LOCK_FREE, NULL)) {
                    Queue->FrontRequest = 0;
                    Queue->LockPrivateQueue = NULL;
                    return TRUE;
                }
                continue;
            }

            //
            // The lock seems busy, so try to insert our wait block
            // in the lock's wait list.
            //

            WaitBlock.WaitListEntry.Flink = State;
            if (CasPointer(&Queue->LockState, State, &WaitBlock.WaitListEntry)) {
                break;
            }
        } while (TRUE);
        ParkThread(&Parker);
    } while (TRUE);
}
