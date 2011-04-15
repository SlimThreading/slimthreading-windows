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
// The value of data queue *Last* field set when the queue is disposed.
//

#define DISPOSED_LAST	((PSLIST_ENTRY)~0)

//
// Acquire and release the queue's lock.
//

static
FORCEINLINE
VOID
AcquireQueueLock (
    __inout PST_LINKED_BLOCKING_QUEUE Queue
    )
{
    AcquireSpinLock(&Queue->Lock);
}

static
FORCEINLINE
VOID
ReleaseQueueLock (
    __inout PST_LINKED_BLOCKING_QUEUE Queue
    )
{
    ReleaseSpinLock(&Queue->Lock);
}

//
// Returns true if the data queue is empty.
//

static
FORCEINLINE
BOOLEAN
IsDataQueueEmpty (
    __in PST_LINKED_BLOCKING_QUEUE Queue
    )
{
    return Queue->First == NULL;
}

//
// Enqueues a item on the data queue.
//

static
FORCEINLINE
VOID
EnqueueDataItem (
    __inout PST_LINKED_BLOCKING_QUEUE Queue,
    __in PSLIST_ENTRY Item
    )
{
    Item->Next = NULL;
    if (Queue->First == NULL) {
        Queue->First = Item;
    } else {
        Queue->Last->Next = Item;
    }
    Queue->Last = Item;
}

//
// Dequeues a item from the data queue.
//

static
FORCEINLINE
PSLIST_ENTRY
DequeueDataItem (
    __inout PST_LINKED_BLOCKING_QUEUE Queue
    )
{
    PSLIST_ENTRY Item;
    if ((Item = Queue->First) != NULL && (Queue->First = Item->Next) == NULL) {
        Queue->Last = NULL;
    }
    return Item;
}

//
// Unlinks the specified entry from the queue's wait list.
//

static
FORCEINLINE
VOID
UnlinkEntryList (
    __inout PST_LINKED_BLOCKING_QUEUE Queue,
    __inout PLIST_ENTRY_ Entry
    )
{
    if (Entry->Flink != Entry) {
        AcquireQueueLock(Queue);
        if (Entry->Flink != Entry) {
            RemoveEntryList(Entry);
        }
        ReleaseQueueLock(Queue);
    }
}

//
// Adds a data item to the linked blocking queue.
//

BOOL
WINAPI
StLinkedBlockingQueue_Add (
    __inout PST_LINKED_BLOCKING_QUEUE Queue,
    __in PSLIST_ENTRY Item
    )
{
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    PWAIT_BLOCK WaiterWaitBlock;

    //
    // Initialize the local variable and acquire the queue's lock.
    //

    ListHead = &Queue->WaitList;
    AcquireQueueLock(Queue);

    //
    // If the queue was already disposed, return failure.
    //

    if (Queue->Last == DISPOSED_LAST) {
        ReleaseQueueLock(Queue);
        return FALSE;
    }

    //
    // If there are waiting threads, try to deliver the data item directly
    // to one of them.
    //

    if ((Entry = ListHead->Flink) != ListHead) {
        do {
            RemoveEntryList(Entry);
            WaiterWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            if (TryLockParker(WaiterWaitBlock->Parker)) {
                ReleaseQueueLock(Queue);
                Item->Next = NULL;
                WaiterWaitBlock->Channel = Item;
                UnparkThread(WaiterWaitBlock->Parker, WAIT_OBJECT_0 + WaiterWaitBlock->WaitKey);
                return TRUE;
            } else {

                //
                // Mark the wait block as unlinked.
                //

                Entry->Flink = Entry;
            }
        } while ((Entry =ListHead->Flink) != ListHead);
    }

    //
    // We can't deliver the data item to an waiter thread, so insert
    // the data item in the data queue, release the queue lock and
    // return success.
    //

    EnqueueDataItem(Queue, Item);
    ReleaseQueueLock(Queue);
    return TRUE;
}

//
// Adds a lsit of data item to the linked blocking queue.
//

BOOL
WINAPI
StLinkedBlockingQueue_AddList (
    __inout PST_LINKED_BLOCKING_QUEUE Queue,
    __in PSLIST_ENTRY List
    )
{
    PLIST_ENTRY_ ListHead;
    PSLIST_ENTRY Last;
    PLIST_ENTRY_ Entry;
    PWAIT_BLOCK WaiterWaitBlock;
    WAKE_LIST WakeList;

    //
    // Initialize the local variable and compute the last list
    // entry.
    
    InitializeWakeList(&WakeList);
    ListHead = &Queue->WaitList;
    Last = List;
    while (Last->Next != NULL) {
        Last = Last->Next;
    }

    //
    // Acquire the queue's lock.
    //

    AcquireQueueLock(Queue);

    //
    // If the queue was already disposed, return failure.
    //

    if (Queue->Last == DISPOSED_LAST) {
        ReleaseQueueLock(Queue);
        return FALSE;
    }

    //
    // If there are waiting threads, try to deliver the available data items
    // directly to the waiter threads.
    //

    if ((Entry = ListHead->Flink) != ListHead) {
        do {
            RemoveEntryList(Entry);
            WaiterWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            if (TryLockParker(WaiterWaitBlock->Parker)) {
                PSLIST_ENTRY Item = List;
                List = List->Next;
                Item->Next = NULL;
                WaiterWaitBlock->Channel = Item;
                AddEntryToWakeList(&WakeList, &WaiterWaitBlock->WaitListEntry);
            } else {

                //
                // Mark the wait block as unlinked.
                //

                Entry->Flink = Entry;
            }
        } while (List != NULL && (Entry =ListHead->Flink) != ListHead);
    }

    //
    // Add the remaining data items do the data queue.
    //

    if (List != NULL) {
        if (Queue->First == NULL) {
            Queue->First = List;
        } else {
            Queue->Last->Next = List;
        }
        Queue->Last = Last;
    }

    //
    // Release the queue's lock, unpark the wake list and return success.
    //

    ReleaseQueueLock(Queue);
    UnparkWakeList(&WakeList);
    return TRUE;
}

//
// Tries to take a data item immediately from the queue.
//

static
BOOLEAN
FASTCALL
_TryTake (
    __inout PST_BLOCKING_QUEUE BlockingQueue,
    __out PVOID *Result
    )
{
    PST_LINKED_BLOCKING_QUEUE Queue = (PST_LINKED_BLOCKING_QUEUE)BlockingQueue;

    //
    // If the queue seems empty, return failure.
    //

    if (IsDataQueueEmpty(Queue)) {
        return FALSE;
    }

    //
    // The queue seems non-empty; so, acquire the queue's
    // lock and check again.
    //

    AcquireQueueLock(Queue);

    //
    // If there are data items available, retrieve the next data
    // item from the data queue, release the lock and return
    // success.
    //

    if (!IsDataQueueEmpty(Queue)) {
        *Result = DequeueDataItem(Queue);
        ReleaseQueueLock(Queue);
        return TRUE;
    }

    //
    // The data queue is actually empty; so, release the queue's
    // lock and return failure.
    //

    ReleaseQueueLock(Queue);
    return FALSE;
}


//
// Takes a a data item from the queue, activating the specified cancellers.
//

static
FORCEINLINE
ULONG
TakeExTail (
    __in PST_LINKED_BLOCKING_QUEUE Queue,
    __out PSLIST_ENTRY *Result,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    ULONG WaitStatus;

    //
    // If the queue was already disposed, return failure.
    //

    if (Queue->Last == DISPOSED_LAST) {
        ReleaseQueueLock(Queue);
        return WAIT_DISPOSED;
    }

    //
    // The current thread must wait; so, check if a null timeout was
    // specified and, if so, return failure.
    // 

    if (Timeout == 0) {
        ReleaseQueueLock(Queue);
        return WAIT_TIMEOUT;
    }

    //
    // The data queue is empty; so, initialize the parker and the wait
    // block, and insert it in the queue's wait list.
    //

    InitializeParkerAndWaitBlock(&WaitBlock, &Parker, 0, WaitAny, WAIT_SUCCESS);
    if ((Queue->Flags & LIFO_WAIT_LIST) != 0) {
        InsertHeadList(&Queue->WaitList, &WaitBlock.WaitListEntry);
    } else {
        InsertTailList(&Queue->WaitList, &WaitBlock.WaitListEntry);
    }
    ReleaseQueueLock(Queue);

    //
    // Park the current thread, activating the specified cancellers.
    //

    WaitStatus = ParkThreadEx(&Parker, 0, Timeout, Alerter);
    
    //
    // If the take succeed, retrieve the data item from the wait block.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        *Result = (PSLIST_ENTRY)WaitBlock.Channel;
        return WAIT_SUCCESS;
    }

    //
    // If the queue was disposed, return the appropiate status.
    //

    if (WaitStatus == WAIT_DISPOSED_0) {
        return WAIT_DISPOSED_0;
    }

    //
    // The take operation was cancelled; so, cancel the take attempt
    // and return the appropriate failure status.
    //

    UnlinkEntryList(Queue, &WaitBlock.WaitListEntry);
    return WaitStatus;
}

//
// Takes a data item from the queue, activating the specified cancellers.
//

ULONG
WINAPI
StLinkedBlockingQueue_TakeEx (
    __in PST_LINKED_BLOCKING_QUEUE Queue,
    __out PSLIST_ENTRY *Result,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{

    //
    // If a null timeout was specified, return failure immediately if
    // the data queue is empty.
    //

    if (Timeout == 0 && IsDataQueueEmpty(Queue)) {
        return WAIT_TIMEOUT;
    }

    //
    // Acquire the queue's lock.
    //

    AcquireQueueLock(Queue);

    //
    // If a data item is available, dequeue it and return success.
    // 

    if (!IsDataQueueEmpty(Queue)) {
        *Result = DequeueDataItem(Queue);
        ReleaseQueueLock(Queue);
        return WAIT_SUCCESS;
    }

    //
    // Excute the tail tail processing...
    //

    return TakeExTail(Queue, Result, Timeout, Alerter);
}

//
// Takes unconditionally a data item from the queue.
//

ULONG
WINAPI
StLinkedBlockingQueue_Take (
    __in PST_LINKED_BLOCKING_QUEUE Queue,
    __out PSLIST_ENTRY *Result
    )
{
    return StLinkedBlockingQueue_TakeEx(Queue, Result, INFINITE, NULL);
}

//
// Takes all data items from the queue, activating the specified
// cancellers.
//

ULONG
WINAPI
StLinkedBlockingQueue_TakeAllEx (
    __in PST_LINKED_BLOCKING_QUEUE Queue,
    __out PSLIST_ENTRY *ListResult,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{

    //
    // If a null timeout was specified, return failure immediately if
    // the data queue is empty.
    //

    if (Timeout == 0 && IsDataQueueEmpty(Queue)) {
        *ListResult = NULL;
        return WAIT_TIMEOUT;
    }

    //
    // Acquire the queue's lock.
    //

    AcquireQueueLock(Queue);

    //
    // If the data queue is not empty, return all queue items and
    // empty the data queue.
    // 

    if (!IsDataQueueEmpty(Queue)) {
        *ListResult = Queue->First;
        Queue->First = Queue->Last = NULL;
        ReleaseQueueLock(Queue);
        return WAIT_SUCCESS;
    }

    //
    // The data queue is empty, so execute the take tail tail processing.
    //

    return TakeExTail(Queue, ListResult, Timeout, Alerter);
}

//
// Takes unconditionally all data items from the queue.
//

BOOL
WINAPI
StLinkedBlockingQueue_TakeAll (
    __in PST_LINKED_BLOCKING_QUEUE Queue,
    __out PSLIST_ENTRY *ListResult
    )
{
    return StLinkedBlockingQueue_TakeAllEx(Queue, ListResult, INFINITE, NULL) == WAIT_SUCCESS;
}

//
// Executes the Take prologue.
//

static
BOOLEAN
FASTCALL
_TakePrologue (
    __inout PST_BLOCKING_QUEUE BlockingQueue,
    __inout PST_PARKER Parker,
    __out PWAIT_BLOCK WaitBlock,
    __in ULONG WaitKey
    )
{
    PST_LINKED_BLOCKING_QUEUE Queue = (PST_LINKED_BLOCKING_QUEUE)BlockingQueue;

    //
    // Acquire the queue's lock.
    //

    AcquireQueueLock(Queue);

    //
    // If a data item is available, try to lock the  parker,
    // return the data item through the Channel field of the wait block
    // and unpark the current thread.
    // 

    if (!IsDataQueueEmpty(Queue)) {
        if (TryLockParker(Parker)) {
            WaitBlock->Channel = DequeueDataItem(Queue);
            UnparkSelf(Parker, WAIT_OBJECT_0 + WaitKey);
        }
        ReleaseQueueLock(Queue);

        //
        // Return true to signal that the take operation was satisfied.
        //

        return TRUE;
    }

    //
    // If the queue was already disposed, try to self lock the parker
    // and, if succeed, self unparks the current thread.
    //

    if (Queue->Last == DISPOSED_LAST) {
        if (TryLockParker(Parker)) {
            WaitBlock->Channel = NULL;
            UnparkSelf(Parker, WAIT_DISPOSED_0 + WaitKey);
        }
        ReleaseQueueLock(Queue);
        return TRUE;
    }

    //
    // There data item's queue is empty; so, initialize the wait block,
    // insert the wait blcok in the queue's wait list, release the
    // queue's lock and return false.
    //

    InitializeWaitBlock(WaitBlock, Parker, 0, WaitAny, WaitKey);
    if ((Queue->Flags & LIFO_WAIT_LIST) != 0) {
        InsertHeadList(&Queue->WaitList, &WaitBlock->WaitListEntry);
    } else {
        InsertTailList(&Queue->WaitList, &WaitBlock->WaitListEntry);
    }
    ReleaseQueueLock(Queue);
    return FALSE;
}

//
// Cancels the specified take attempt.
//

static
VOID
FASTCALL
_CancelTake (
    __inout PST_BLOCKING_QUEUE BlockingQueue,
    __inout PWAIT_BLOCK WaitBlock
    )
{
    UnlinkEntryList((PST_LINKED_BLOCKING_QUEUE)BlockingQueue, &WaitBlock->WaitListEntry);
}

//
// The virtual function table used by the linked blocking queue instances.
//

static
BLOCKING_QUEUE_VTBL LinkedBlockingQueueVtbl = {
    _TryTake,
    _TakePrologue,
    _CancelTake
};

//
//	Initializes the linked blocking queue.
//

VOID
WINAPI
StLinkedBlockingQueue_Init (
    __out PST_LINKED_BLOCKING_QUEUE Queue,
    __in BOOL LifoTake
    )
{
    Queue->BlockingQueue.Vtbl = &LinkedBlockingQueueVtbl;
    InitializeSpinLock(&Queue->Lock, SHORT_CRITICAL_SECTION_SPINS);
    InitializeListHead(&Queue->WaitList);
    Queue->First = Queue->Last = NULL;
    Queue->Flags = LifoTake ? LIFO_WAIT_LIST : 0;
}

//
// Disposes the linked blocking queue.
//

BOOL
WINAPI
StLinkedBlockingQueue_Dispose (
    __inout PST_LINKED_BLOCKING_QUEUE Queue,
    __in_opt ST_LINKED_BLOCKING_QUEUE_DTOR *Dtor,
    __in_opt PVOID Context
    )
{
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    PWAIT_BLOCK WaitBlock;
    PSLIST_ENTRY Item;
    WAKE_LIST WakeList;

    //
    // Initialize the local variables and acquire the queue's lock.
    //

    ListHead = &Queue->WaitList;
    InitializeWakeList(&WakeList);
    AcquireQueueLock(Queue);

    //
    // If the queue was already disposed, release the queue's
    // lock and return false.
    //

    if (Queue->Last == DISPOSED_LAST) {
        ReleaseQueueLock(Queue);
        return FALSE;
    }

    //
    // Release all waiting threads with a disposed wait status.
    //

    if ((Entry = ListHead->Flink) != ListHead) {
        do {
            RemoveEntryList(Entry);
            WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            if (TryLockParker(WaitBlock->Parker)) {
                AddEntryToWakeList(&WakeList, Entry);
            } else {
                Entry->Flink = Entry;
            }
        } while ((Entry = ListHead->Flink) != ListHead);
    }

    //
    // Get the first data item, mark the queue as disposed
    // and release the queue's lock.
    //

    Item = Queue->First;
    Queue->First = NULL;
    Queue->Last = DISPOSED_LAST;
    ReleaseQueueLock(Queue);

    //
    // Unpark all threads released above.
    //

    Entry = WakeList.First;
    while (Entry != NULL) {
        PLIST_ENTRY_ Next = Entry->Blink;
        WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
        UnparkThread(WaitBlock->Parker, WAIT_DISPOSED_0 + WaitBlock->WaitKey);
        Entry = Next;
    }

    //
    // If the data queue wasn't empty and a dispose callback was
    // specified, call the callback with the list of data items
    // present on the queue.
    //

    if (Item != NULL && Dtor != NULL) {
        (*Dtor)(Item, Context);
    }
    return TRUE;
}
