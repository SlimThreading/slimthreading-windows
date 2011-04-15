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
// Acquire and release the queue's lock.
//

static
FORCEINLINE
VOID
AcquireQueueLock (
    __inout PST_BOUNDED_BLOCKING_QUEUE Queue
    )
{
    AcquireSpinLock(&Queue->Lock);
}

static
FORCEINLINE
VOID
ReleaseQueueLock (
    __inout PST_BOUNDED_BLOCKING_QUEUE Queue
    )
{
    ReleaseSpinLock(&Queue->Lock);
}

//
// Unlinks the specified entry from the queue's wait list.
//

static
FORCEINLINE
VOID
UnlinkEntryList (
    __inout PST_BOUNDED_BLOCKING_QUEUE Queue,
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
// Adds a data item to the bounded blocking queue, activating the
// specified cancellers.
//

ULONG
WINAPI
StBoundedBlockingQueue_AddEx (
    __inout PST_BOUNDED_BLOCKING_QUEUE Queue,
    __in PVOID Item,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    WAIT_BLOCK WaitBlock;
    ST_PARKER Parker;
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    PWAIT_BLOCK WaiterWaitBlock;
    ULONG WaitStatus;

    //
    // Initialize the local variable and acquire the queue's lock.
    //

    ListHead = &Queue->WaitList;
    AcquireQueueLock(Queue);

    //
    // If the queue was already disposed, return failure.
    //

    if (Queue->Items == NULL) {
        ReleaseQueueLock(Queue);
        return WAIT_DISPOSED;
    }

    //
    // When the queue has free slots, we can't have threads blocked
    // by the add operation; so, if there are waiters, we know that
    // they were blocked by the take operation.
    //

    if (Queue->Count < Queue->Capacity) {
        if ((Entry = ListHead->Flink) != ListHead) {
            do {
                RemoveEntryList(Entry);
                WaiterWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
                if (TryLockParker(WaiterWaitBlock->Parker)) {

                    //
                    // Release the queue's lock, pass the data item through
                    // the wait block, unpark the waiter thread and return
                    // success.
                    //

                    ReleaseQueueLock(Queue);
                    WaiterWaitBlock->Channel = Item;
                    UnparkThread(WaiterWaitBlock->Parker, WAIT_OBJECT_0 + WaiterWaitBlock->WaitKey);
                    return WAIT_SUCCESS;
                } else {
                    
                    //
                    // The take operation was cancelled due to timeout or alert,
                    // so mark the wait block as unlinked.
                    // 

                    Entry->Flink = Entry;
                }
            } while ((Entry = ListHead->Flink) != ListHead);
        }

        //
        // Copy the data item to the queue's buffer and return success.
        //

        Queue->Items[Queue->Tail] = Item;
        if (++Queue->Tail >= Queue->Capacity) {
            Queue->Tail = 0;
        }
        Queue->Count++;
        ReleaseQueueLock(Queue);
        return WAIT_SUCCESS;
    }

    //
    // The queue's buffer is full, so return failure if a null
    // timeout was specified.
    //

    if (Timeout == 0) {
        ReleaseQueueLock(Queue);
        return WAIT_TIMEOUT;
    }

    //
    // The current thread must wait. So, initialize a wait block
    // and insert it in the queue's wait list.
    //

    InitializeParkerAndWaitBlockEx(&WaitBlock, &Parker, 0, Item, WaitAny, 0);
    InsertTailList(ListHead, &WaitBlock.WaitListEntry);
    
    //
    // Release the queue lock and park the current thread, activating
    // the specified cancellers.
    //

    ReleaseQueueLock(Queue);
    WaitStatus = ParkThreadEx(&Parker, 0, Timeout, Alerter);

    //
    // If we put the data item or the queue was disposed,
    // return immediately the appropriate wait status.
    // 

    if (WaitStatus == WAIT_OBJECT_0 || WaitStatus == WAIT_DISPOSED_0) {
        return WaitStatus;
    }

    //
    // The put was cancelled due to timeout or alert; so, unlink
    // the wait block from the queue's wait list and return the
    // appropriate failure status.
    //

    UnlinkEntryList(Queue, &WaitBlock.WaitListEntry);
    return WaitStatus;
}
//
// Adds unconditionally a data item to the bounded blocking queue.
//

BOOL
WINAPI
StBoundedBlockingQueue_Add (
    __inout PST_BOUNDED_BLOCKING_QUEUE Queue,
    __in PVOID Item
    )
{
    return StBoundedBlockingQueue_AddEx(Queue, Item, INFINITE, NULL) == WAIT_SUCCESS;
}


//
// Waits until a data item is available on bounded blocking, the
// specified timeout expires or the alerter is set.
//

ULONG
WINAPI
StBoundedBlockingQueue_TakeEx (
    __in PST_BOUNDED_BLOCKING_QUEUE Queue,
    __out PVOID *Result,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    PWAIT_BLOCK WaiterWaitBlock;
    ULONG WaitStatus;

    //
    // ...
    //

    if (Timeout == 0 && Queue->Count == 0) {
        return WAIT_TIMEOUT;
    }

    //
    // Initialize the local variables and acquire the
    // queue's lock.
    //

    ListHead = &Queue->WaitList;
    AcquireQueueLock(Queue);

    //
    // If there is a data item available, ...
    //

    if (Queue->Count != 0) {
        *Result	 = Queue->Items[Queue->Head];
        if (++Queue->Head == Queue->Capacity) {
            Queue->Head = 0;
        }
        Queue->Count--;

        //
        // If there are waiters, use the freed buffer slot to
        // release one of them.
        //

        if ((Entry = ListHead->Flink) != ListHead) {
            do {
                RemoveEntryList(Entry);
                WaiterWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);				
                if (TryLockParker(WaiterWaitBlock->Parker)) {
                    Queue->Items[Queue->Tail] = WaiterWaitBlock->Channel;
                    if (++Queue->Tail == Queue->Capacity) {
                        Queue->Tail = 0;
                    }
                    Queue->Count++;
                    ReleaseQueueLock(Queue);
                    UnparkThread(WaiterWaitBlock->Parker, WAIT_OBJECT_0 + WaiterWaitBlock->WaitKey);
                    return WAIT_SUCCESS;
                } else {		
                    Entry->Flink = Entry;
                }
            } while ((Entry = ListHead->Flink) != ListHead);
        }
        
        //
        // Release the queue's lock and return success.
        //

        ReleaseQueueLock(Queue);
        return WAIT_SUCCESS;
    }

    //
    // If the queue was disposed, release the queue's lock and
    // return the disposed wait status.
    //

    if (Queue->Items == 0) {
        ReleaseQueueLock(Queue);
        return WAIT_DISPOSED;
    }

    //
    // The queue's buffer is empty; so, initialize the parker and the
    // wait block and insert it in the queue's wait list.
    //

    InitializeParkerAndWaitBlock(&WaitBlock, &Parker, 0, WaitAny, 0);
    if ((Queue->Flags & LIFO_WAIT_LIST) != 0) {
        InsertHeadList(ListHead, &WaitBlock.WaitListEntry);
    } else {
        InsertTailList(ListHead, &WaitBlock.WaitListEntry);
    }

    //
    // Release the queue's lock and park the current thread activating
    // the specified cancellers.
    //

    ReleaseQueueLock(Queue);
    WaitStatus = ParkThreadEx(&Parker, 0, Timeout, Alerter);
    
    //
    // If the take succeed, retrieve the data item from the wait block.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        *Result = WaitBlock.Channel;
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
// Tries to take immediately a data item from the queue.
//

static
BOOLEAN
FASTCALL
_TryTake (
    __inout PST_BLOCKING_QUEUE BlockingQueue,
    __out PVOID *Result
    )
{
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    PWAIT_BLOCK WaiterWaitBlock;
    PST_BOUNDED_BLOCKING_QUEUE Queue = (PST_BOUNDED_BLOCKING_QUEUE)BlockingQueue;

    //
    // If the queue seems empty, return failure.
    //

    if (Queue->Count == 0) {
        return FALSE;
    }

    //
    // If the queue seems non-empty, acquire the queue's lock
    // and check again.
    //

    ListHead = &Queue->WaitList;
    AcquireQueueLock(Queue);

    //
    // If the queue is actaully empty, release the queue's lock
    // and return failure.
    //

    if (Queue->Count == 0) {
        ReleaseQueueLock(Queue);
        return FALSE;
    }

    //
    // The queue isn't empty; so, retrieve the next data item
    // from the queue's buffer.
    //

    *Result = Queue->Items[Queue->Head];
    if (++Queue->Head == Queue->Capacity) {
        Queue->Head = 0;
    }
    Queue->Count--;

    //
    // If there are waiters, use the freed buffer slot to release
    // one of them.
    //

    if ((Entry = ListHead->Flink) != ListHead) {
        do {
            RemoveEntryList(Entry);
            WaiterWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);				
            if (TryLockParker(WaiterWaitBlock->Parker)) {
                Queue->Items[Queue->Tail] = WaiterWaitBlock->Channel;
                if (++Queue->Tail == Queue->Capacity) {
                    Queue->Tail = 0;
                }
                Queue->Count++;
                ReleaseQueueLock(Queue);
                UnparkThread(WaiterWaitBlock->Parker, WAIT_OBJECT_0 + WaiterWaitBlock->WaitKey);
                return TRUE;
            } else {

                //
                // The put was cancelled due to timeout or alert; so, mark
                // the wait block as unlinked.
                //

                Entry->Flink = Entry;
            }
        } while ((Entry = ListHead->Flink) != ListHead);
    }

    //
    // Release the queue's lock and return success.
    //

    ReleaseQueueLock(Queue);
    return TRUE;
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
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    PWAIT_BLOCK WaiterWaitBlock;
    PST_BOUNDED_BLOCKING_QUEUE Queue = (PST_BOUNDED_BLOCKING_QUEUE)BlockingQueue;

    //
    // Initialize the local variables and acquire the
    // queue's lock.
    //

    ListHead = &Queue->WaitList;
    AcquireQueueLock(Queue);

    //
    // If there is a data item available, try to lock the thread
    // parker; if succeed, retrive the next data item and self
    // unpark the current thread.
    //

    if (Queue->Count != 0) {
        if (TryLockParker(Parker)) {
            WaitBlock->Channel = Queue->Items[Queue->Head];
            if (++Queue->Head == Queue->Capacity) {
                Queue->Head = 0;
            }
            Queue->Count--;
            UnparkSelf(Parker, WaitKey);

            //
            // If there are waiters, use the freed buffer slot to
            // release one of them.
            //

            if ((Entry = ListHead->Flink) != ListHead) {
                do {
                    RemoveEntryList(Entry);
                    WaiterWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);				
                    if (TryLockParker(WaiterWaitBlock->Parker)) {
                        Queue->Items[Queue->Tail] = WaiterWaitBlock->Channel;
                        if (++Queue->Tail == Queue->Capacity) {
                            Queue->Tail = 0;
                        }
                        Queue->Count++;
                        ReleaseQueueLock(Queue);
                        UnparkThread(WaiterWaitBlock->Parker, WAIT_OBJECT_0 + WaiterWaitBlock->WaitKey);

                        //
                        // Return true to signal that the take-any operation
                        // was satisfied.
                        //

                        return TRUE;
                    } else {		
                        Entry->Flink = Entry;
                    }
                } while ((Entry = ListHead->Flink) != ListHead);
            }
        }
        
        //
        // Release the queue's lock and return true to signal that the
        // take operation was satisfied.
        //

        ReleaseQueueLock(Queue);
        return TRUE;
    }

    //
    // If the queue was disposed, try to self lock the parker
    // and, if succeed, self unpark the current thread.
    //

    if (Queue->Items == 0) {
        if (TryLockParker(Parker)) {
            UnparkSelf(Parker, WAIT_DISPOSED + WaitKey);
        }
        ReleaseQueueLock(Queue);
        return TRUE;
    }

    //
    // The queue's buffer is empty; so, initialize the wait block
    // and insert it in the queue's wait list.
    //

    InitializeWaitBlock(WaitBlock, Parker, 0, WaitAny, WaitKey);
    if ((Queue->Flags & LIFO_WAIT_LIST) != 0) {
        InsertHeadList(ListHead, &WaitBlock->WaitListEntry);
    } else {
        InsertTailList(ListHead, &WaitBlock->WaitListEntry);
    }

    //
    // Release the queue's lock and return false.
    //

    ReleaseQueueLock(Queue);
    return FALSE;
}

//
// Cancels the specified take operation.
//

static
VOID
FASTCALL
_CancelTake (
    __inout PST_BLOCKING_QUEUE BlockingQueue,
    __in PWAIT_BLOCK WaitBlock
    )
{
    UnlinkEntryList((PST_BOUNDED_BLOCKING_QUEUE)BlockingQueue, &WaitBlock->WaitListEntry);
}

//
// The virtual function table that implements the BlockingQueue interface.
//

static
BLOCKING_QUEUE_VTBL BoundedBlockingQueueVtbl = {
    _TryTake,
    _TakePrologue,
    _CancelTake
};

//
//	Initializes the blocking queue.
//

BOOL
WINAPI
StBoundedBlockingQueue_Init (
    __out PST_BOUNDED_BLOCKING_QUEUE Queue,
    __in ULONG Capacity,
    __in BOOL LifoTake
    )
{

    ZeroMemory(Queue, sizeof(*Queue));
    Queue->BlockingQueue.Vtbl = &BoundedBlockingQueueVtbl;
    if (Capacity == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Queue->Items = (PVOID *)HeapAlloc(GetProcessHeap(), 0, Capacity * sizeof(PVOID));
    if (Queue->Items == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    Queue->Capacity = Capacity;
    InitializeSpinLock(&Queue->Lock, LONG_CRITICAL_SECTION_SPINS);
    InitializeListHead(&Queue->WaitList);
    Queue->Flags = LifoTake ? LIFO_WAIT_LIST : 0;
    return TRUE;
}

//
// Disposes the bounded blocking queue.
//

BOOL
WINAPI
StBoundedBlockingQueue_Dispose (
    __inout PST_BOUNDED_BLOCKING_QUEUE Queue,
    __in_opt ST_BOUNDED_BLOCKING_QUEUE_DTOR *Dtor,
    __in_opt PVOID Context
    )
{
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    PWAIT_BLOCK WaiterWaitBlock;
    WAKE_LIST WakeList;
    PVOID *Items;
    ULONG Head;
    ULONG Count;
    
    //
    // Initialize the local variables and acquire the queue's lock.
    //

    ListHead = &Queue->WaitList;
    InitializeWakeList(&WakeList);
    AcquireQueueLock(Queue);

    //
    // if the queue was already disposed, return false.
    //

    if (Queue->Items == NULL) {
        ReleaseQueueLock(Queue);
        return FALSE;
    }

    //
    // Release all waiting thread with a disposed wait status.
    //

    if ((Entry = ListHead->Flink) != ListHead) {
        do {
            RemoveEntryList(Entry);
            WaiterWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            if (TryLockParker(WaiterWaitBlock->Parker)) {
                AddEntryToWakeList(&WakeList, Entry);
            } else {
                Entry->Flink = Entry;
            }
        } while ((Entry = ListHead->Flink) != ListHead);
    }

    //
    // Save the reference to the data items on the queue's buffer.
    //

    Items = Queue->Items;
    Count = Queue->Count;
    Head = Queue->Head;

    //
    // Mark the queue as disposed and release the queue's lock.
    //

    Queue->Items = NULL;
    Queue->Count = 0;
    ReleaseQueueLock(Queue);

    //
    // Unpark all threads released above.
    //

    Entry = WakeList.First;
    while (Entry != NULL) {
        PLIST_ENTRY_ Next = Entry->Blink;
        WaiterWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
        UnparkThread(WaiterWaitBlock->Parker, WAIT_DISPOSED_0 + WaiterWaitBlock->WaitKey);
        Entry = Next;
    }

    //
    // If there were data items on the queue's buffer and a dispose callback
    // was specified, call the callback for one or two arrays of data items.
    //

    if (Count!= 0 && Dtor != NULL) {
        ULONG TillEnd = Queue->Capacity - Head;
        if (Count > TillEnd) {
            Dtor(Items + Head, TillEnd, Context);
            Dtor(Items, Count - TillEnd, Context);
        } else {
            Dtor(Items + Head, Count, Context);
        }
    }

    //
    // Free the queue's buffer and return success.
    //

    HeapFree(GetProcessHeap(), 0, Items);
    return TRUE;
}
