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
// The structure that holds the transfer state during
// reads and writes.
//

typedef struct _TRANSFER_STATE {
    PUCHAR DataPtr;
    ULONG Remaining;
} TRANSFER_STATE, *PTRANSFER_STATE;

//
// Acquire and release the queue's lock.
//

static
FORCEINLINE
VOID
AcquireQueueLock (
    __inout PST_STREAM_BLOCKING_QUEUE Queue
    )
{
    AcquireSpinLock(&Queue->Lock);
}

static
FORCEINLINE
VOID
ReleaseQueueLock (
    __inout PST_STREAM_BLOCKING_QUEUE Queue
    )
{
    ReleaseSpinLock(&Queue->Lock);
}

//
// Waits until to write the specified number of bytes to the
// queue, activating the specified cancellers.
//

ULONG
WINAPI
StStreamBlockingQueue_WriteEx (
    __inout PST_STREAM_BLOCKING_QUEUE Queue,
    __in PVOID Buffer,
    __in ULONG Count,
    __out PULONG Transferred,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    TRANSFER_STATE Transfer;
    PWAIT_BLOCK ReaderWaitBlock;
    PTRANSFER_STATE ReaderTransfer;
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    WAKE_LIST WakeList;
    ULONG ToCopy;
    UCHAR *Tail;
    UCHAR *Limit;
    ULONG_PTR TillEnd;
    BOOLEAN MustWait;
    ULONG WaitStatus;

    //
    // If this is a zero length write, return success.
    //

    if (Count == 0) {
        *Transferred = 0;
        return WAIT_SUCCESS;
    }

    //
    // Initialize the local variables and acquire the queue's lock.
    //

    Transfer.DataPtr = (PUCHAR)Buffer;
    Transfer.Remaining = Count;
    ListHead = &Queue->WaitList;
    InitializeWakeList(&WakeList);
    AcquireQueueLock(Queue);

    //
    // If the queue was disposed, return failure.
    //

    if (Queue->Items == NULL) {
        ReleaseQueueLock(Queue);
        *Transferred = 0;
        return WAIT_DISPOSED;
    }

    //
    // If the queue's buffer is full, the current thread must wait
    // if a null timeout wasn't specified.
    //

    if (Queue->Count == Queue->Capacity) {
        goto CheckForWait;
    }

    //
    // If there are waiting readers - which means that the queue's buffer
    // is empty -, we start transferring directly to the waiting
    // readers' buffers.
    //

    if ((Entry = ListHead->Flink) != ListHead) {
        do {
            ReaderWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);

            //
            // If the waiting reader cancelled its read request, remove its
            // wait block from the wait list and process the next waiting waiter.
            //

            if (IsParkerLocked(ReaderWaitBlock->Parker)) {				
                RemoveEntryList(Entry);
                Entry->Flink = Entry;
            } else {
                ReaderTransfer = (PTRANSFER_STATE)ReaderWaitBlock->Channel;

                //
                // Compute the number of bytes to transfer to the waiting
                // reader's buffer.
                //

                if ((ToCopy = ReaderTransfer->Remaining) > Transfer.Remaining) {
                    ToCopy = Transfer.Remaining;
                }

                //
                // Copy the bytes from the current thread's buffer to the
                // waiting reader's buffer and update counters and pointers.
                //

                CopyMemory(ReaderTransfer->DataPtr, Transfer.DataPtr, ToCopy);
                ReaderTransfer->Remaining -= ToCopy;
                ReaderTransfer->DataPtr += ToCopy;
                Transfer.Remaining -= ToCopy;
                Transfer.DataPtr += ToCopy;

                //
                // If the waiting reader completes its operation, remove its
                // wait block from the queue's wait list and try to lock the
                // associated parker. 
                //

                if (ReaderTransfer->Remaining == 0) {
                    RemoveEntryList(Entry);
                    if (TryLockParker(ReaderWaitBlock->Parker)) {
                        if (!UnparkInProgressThread(ReaderWaitBlock->Parker, WAIT_SUCCESS)) {
                            AddEntryToWakeList(&WakeList, Entry);
                        }
                    } else {

                        //
                        // A cancellation occured in race with the read completion.
                        // In this case, the cancellation will be ignored but we
                        // need to mark the wait block as non-inserted.
                        //

                        Entry->Flink = Entry;
                    }
                } else {

                    //
                    // The available data is not enough to satisfy the waiting
                    // reader that is at front of wait list, so break the loop.
                    //

                    break;
                }
            }
        } while (Transfer.Remaining != 0 && (Entry = ListHead->Flink) != ListHead);
    }
    
    //
    // If we have still bytes to write and there is free space in
    // the queue's buffer, transfer the appropriate number of bytes
    // to the buffer.
    //

    if (Transfer.Remaining != 0 && Queue->Count < Queue->Capacity) {

        //
        // Compute the number of bytes that can be copied to the
        // queue's buffer.
        //

        if ((ToCopy = Transfer.Remaining) > (Queue->Capacity - Queue->Count)) {
            ToCopy = Queue->Capacity - Queue->Count;
        }
        
        Tail = Queue->Tail;
        Limit = Queue->Limit;
        TillEnd = Limit - Tail;
        if (TillEnd >= ToCopy) {
            CopyMemory(Tail, Transfer.DataPtr, ToCopy);
            Tail += ToCopy;
            if (Tail >= Limit) {
                Tail = Queue->Items;
            }
        } else {
            ULONG FromBegin = (ULONG)(ToCopy - TillEnd);
            CopyMemory(Tail, Transfer.DataPtr, TillEnd);
            CopyMemory(Queue->Items, Transfer.DataPtr + TillEnd, FromBegin);
            Tail = Queue->Items + FromBegin;
        }
        Queue->Count += ToCopy;
        Queue->Tail = Tail;
        Transfer.DataPtr += ToCopy;
        Transfer.Remaining -= ToCopy;
    }

CheckForWait:

    //
    // If we have still bytes to write we must wait if a null
    // timeout wasn't specified.
    //
    
    WaitStatus = WAIT_SUCCESS;
    MustWait = FALSE;
    if (Transfer.Remaining != 0) {
        if (Timeout == 0) {
            WaitStatus = WAIT_TIMEOUT;
        } else {
            MustWait = TRUE;
        }
    }

    if (MustWait) {
        InitializeParkerAndWaitBlockEx(&WaitBlock, &Parker, 0, &Transfer, WaitAny, WAIT_SUCCESS);
        InsertTailList(ListHead, &WaitBlock.WaitListEntry);
    }

    //
    // Release the queue lock.
    //

    ReleaseQueueLock(Queue);

    //
    // If we locked waiting reader threads above, unpark them now.
    //

    UnparkWakeList(&WakeList);

    //
    // If the write operation was completed or a null timeout was
    // specified, return immediately with the appropriate wait status.
    //

    if (!MustWait) {
        *Transferred = Count - Transfer.Remaining;
        return WaitStatus;
    }

    //
    // Park the current thread, activating the specified cancellers.
    //

    WaitStatus = ParkThreadEx(&Parker, 0, Timeout, Alerter);

    //
    // If the wait was satisfied due to write completion or queue shutdown,
    // return the number of bytes transferred and the appropriate status.
    //

    if (WaitStatus == WAIT_SUCCESS || WaitStatus == WAIT_DISPOSED) {
        if ((*Transferred = Count - Transfer.Remaining) != 0) {
            return WAIT_SUCCESS;
        }
        return WAIT_DISPOSED;
    }

    //
    // The write operation was cancelled due to timeout or alert;
    // so, lock the queue, remove the wait block, if it is still linked
    // in the wait list, and return the number of bytes that were
    // actually written.
    //

    AcquireQueueLock(Queue);
    if (WaitBlock.WaitListEntry.Flink != &WaitBlock.WaitListEntry) {
        RemoveEntryList(&WaitBlock.WaitListEntry);
    }
    ReleaseQueueLock(Queue);
    *Transferred = Count - Transfer.Remaining;
    return (Transfer.Remaining == 0) ? WAIT_SUCCESS : WaitStatus;
}

//
// Waits unconditionally until to write the specified number of bytes
// to the queue.
//

BOOL
WINAPI
StStreamBlockingQueue_Write (
    __inout PST_STREAM_BLOCKING_QUEUE Queue,
    __in PVOID Buffer,
    __in ULONG Count,
    __out PULONG Transferred
    )
{
    return StStreamBlockingQueue_WriteEx(Queue, Buffer, Count, Transferred,
                                         INFINITE, NULL) == WAIT_SUCCESS;
}

//
// Waits until to read the specified number of bytes from the queue,
// activating the specified cancellers.
//

ULONG
WINAPI
StStreamBlockingQueue_ReadEx (
    __inout PST_STREAM_BLOCKING_QUEUE Queue,
    __in PVOID Buffer,
    __in ULONG Count,
    __out PULONG Transferred,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    TRANSFER_STATE Transfer;
    PWAIT_BLOCK WriterWaitBlock;
    PTRANSFER_STATE WriterTransfer;
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    WAKE_LIST WakeList;
    ULONG ToCopy;
    UCHAR *Head;
    UCHAR *Tail;
    UCHAR *Limit;
    ULONG TillEnd;
    BOOLEAN MustWait;
    ULONG WaitStatus;

    //
    // If this is a zero length read, return success.
    //

    if (Count == 0) {
        *Transferred = 0;	
        return WAIT_OBJECT_0;
    }

    //
    // Initialize the local variables and acquire the queue's lock.
    //

    Transfer.DataPtr = (PUCHAR)Buffer;
    Transfer.Remaining = Count;
    ListHead = &Queue->WaitList;
    InitializeWakeList(&WakeList);
    AcquireQueueLock(Queue);

    //
    // If the queue was disposed, return failure.
    //

    if (Queue->Items == NULL) {
        ReleaseQueueLock(Queue);
        *Transferred = 0;
        return WAIT_DISPOSED;
    }

    //
    // If the queue's buffer is empty, the current thread must wait.
    //

    if (Queue->Count == 0) {
        goto CheckForWait;
    }

    //
    // The queue's buffer is not empty; so, transfer the appropriate
    // number of bytes from the buffer.
    //
    // Compute the number of bytes to tranfer and copy them to
    // the current thread's buffer.
    //

    if ((ToCopy = Transfer.Remaining) > Queue->Count) {
        ToCopy = Queue->Count;
    }
    Head = Queue->Head;
    Limit = Queue->Limit;
    TillEnd = (ULONG)(Limit - Head);
    if (TillEnd >= ToCopy) {
        CopyMemory(Transfer.DataPtr, Head, ToCopy);
        if ((Head += ToCopy) >= Limit) {
            Head = Queue->Items;
        }
    } else {
        ULONG FromBegin = (ULONG)(ToCopy - TillEnd);
        CopyMemory(Transfer.DataPtr, Head, TillEnd);
        CopyMemory(Transfer.DataPtr + TillEnd, Queue->Items, FromBegin);
        Head = Queue->Items + FromBegin;
    }

    //
    // Update pointers and counters.
    //

    Transfer.DataPtr += ToCopy;
    Transfer.Remaining -= ToCopy;
    Queue->Head = Head;
    Queue->Count -= ToCopy;

    //
    // If the read isn't completed and there are waiting writers,
    // transfer the data directly from the writers' buffers.
    //

    if (Transfer.Remaining != 0 && (Entry = ListHead->Flink) != ListHead) {
        do {
            WriterWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);

            //
            // If the write operation was cancelled, remove the wait block
            // from the wait list and mark it as unlinked.
            //

            if (IsParkerLocked(WriterWaitBlock->Parker)) {
                RemoveEntryList(Entry);
                Entry->Flink = Entry;
            } else {
                WriterTransfer = (PTRANSFER_STATE)WriterWaitBlock->Channel;

                //
                // Compute the number of bytes to transfer from the waiting
                // writer's buffer and copy them.
                //

                if ((ToCopy = Transfer.Remaining) > WriterTransfer->Remaining) {
                    ToCopy = WriterTransfer->Remaining;	
                }
                CopyMemory(Transfer.DataPtr, WriterTransfer->DataPtr, ToCopy);

                //
                // Update pointers and counters.
                //

                WriterTransfer->DataPtr += ToCopy;
                WriterTransfer->Remaining -= ToCopy;
                Transfer.DataPtr += ToCopy;
                Transfer.Remaining -= ToCopy;

                //
                // If the writer completed its request, release it.
                //

                if (WriterTransfer->Remaining == 0) {
                    RemoveEntryList(Entry);
                    if (TryLockParker(WriterWaitBlock->Parker)) {		
                        if (!UnparkInProgressThread(WriterWaitBlock->Parker, WAIT_SUCCESS)) {
                            AddEntryToWakeList(&WakeList, Entry);
                        }
                    } else {

                        //
                        // The write completion raced with cancellation, so mark
                        // the wait block as unlinked.
                        //

                        Entry->Flink = Entry;
                    }
                } else {

                    //
                    // We read the requested number of bytes, so break the loop.
                    //

                    break;
                }
            }
        } while (Transfer.Remaining != 0 && (Entry = ListHead->Flink) != ListHead);
    }

    //
    // If there is available space on the queue's buffer and waiting
    // writers, we must try to fill the queue's buffer.
    //

    if (Queue->Count < Queue->Capacity && (Entry = ListHead->Flink) != ListHead) {
        do {
            WriterWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);

            if (IsParkerLocked(WriterWaitBlock->Parker)) {
                RemoveEntryList(Entry);
                Entry->Flink = Entry;
            } else {
                WriterTransfer = (PTRANSFER_STATE)WriterWaitBlock->Channel;

                //
                // Compute the number of bytes to copy and copy them.
                //

                if ((ToCopy = WriterTransfer->Remaining) > (Queue->Capacity - Queue->Count)) {
                    ToCopy = Queue->Capacity - Queue->Count;	
                }
                Tail = Queue->Tail;
                Limit = Queue->Limit;
                TillEnd = (ULONG)(Limit - Tail);
                if (TillEnd >= ToCopy) {
                    CopyMemory(Tail, WriterTransfer->DataPtr, ToCopy);
                    if ((Tail += ToCopy) >= Limit) {
                        Tail = Queue->Items;
                    }
                } else {
                    ULONG FromBegin = (ULONG)(ToCopy - TillEnd);
                    CopyMemory(Tail, WriterTransfer->DataPtr, TillEnd);
                    CopyMemory(Queue->Items, WriterTransfer->DataPtr + TillEnd, FromBegin);
                    Tail = Queue->Items + FromBegin;
                }

                //
                // Update counters and pointers.
                //

                WriterTransfer->Remaining -= ToCopy;
                WriterTransfer->DataPtr += ToCopy;
                Queue->Tail = Tail;
                Queue->Count += ToCopy;

                //
                // If the writer completed, release it.
                //

                if (WriterTransfer->Remaining == 0) {
                    RemoveEntryList(Entry);
                    if (TryLockParker(WriterWaitBlock->Parker)) {
                        if (!UnparkInProgressThread(WriterWaitBlock->Parker, WAIT_SUCCESS)) {
                            AddEntryToWakeList(&WakeList, Entry);
                        }
                    } else {
                        Entry->Flink = Entry;
                    }
                } else {
                    break;
                }
            }
        } while (Queue->Count < Queue->Capacity && (Entry = ListHead->Flink) != ListHead);
    }

CheckForWait:

    //
    // If the read operation wasn't completed, we must wait if
    // a null timeout wasn't specified.
    //

    WaitStatus = WAIT_SUCCESS;
    MustWait = FALSE;
    if (Transfer.Remaining != 0) {
        if (Timeout == 0) {
            WaitStatus = WAIT_TIMEOUT;
        } else {
            MustWait = TRUE;
        }
    }

    //
    // If we must wait, initialize a wait block and insert it in
    // the queue's wait list and release the queue lock.
    //

    if (MustWait) {
        InitializeParkerAndWaitBlockEx(&WaitBlock, &Parker, 0, &Transfer, WaitAny, WAIT_SUCCESS);
        InsertTailList(ListHead, &WaitBlock.WaitListEntry);
    }
    ReleaseQueueLock(Queue);

    //
    // If we released writer threads above, unpark them now.
    //

    UnparkWakeList(&WakeList);

    //
    // If we must not wait, return appropriately.
    //

    if (!MustWait) {
        *Transferred = Count - Transfer.Remaining;
        return WaitStatus;
    }

    //
    // Park the current thread, activating the specified cancellers.
    //

    WaitStatus = ParkThreadEx(&Parker, 0, Timeout, Alerter);

    //
    // If we completed the read or the queue was disposed, return
    // the transferred number of bytes and the appropriate wait status.

    if (WaitStatus == WAIT_SUCCESS || WaitStatus == WAIT_DISPOSED) {
        if ((*Transferred = Count - Transfer.Remaining) != 0) {
            return WAIT_SUCCESS;
        }
        return WAIT_DISPOSED;
    }

    //
    // The read operation was cancelled due to timeout or alert;
    // so, lock the queue, remove the wait block, if it is still linked
    // on the wait list and return the number of bytes that were read
    // from the queue and the appropriate wait status.
    //

    AcquireQueueLock(Queue);
    if (WaitBlock.WaitListEntry.Flink != &WaitBlock.WaitListEntry) {
        RemoveEntryList(&WaitBlock.WaitListEntry);
    }
    ReleaseQueueLock(Queue);
    *Transferred = Count - Transfer.Remaining;
    return (Transfer.Remaining == 0) ? WAIT_SUCCESS : WaitStatus;
}

//
// Waits unconditionally until to read the specified number of bytes
// from the queue.
//

BOOL
WINAPI
StStreamBlockingQueue_Read (
    __inout PST_STREAM_BLOCKING_QUEUE Queue,
    __in PVOID Buffer,
    __in ULONG Count,
    __out PULONG Transferred
    )
{
    return StStreamBlockingQueue_ReadEx(Queue, Buffer, Count, Transferred, INFINITE, NULL);
}

//
// Initializes the stream blocking queue.
//

BOOL
WINAPI
StStreamBlockingQueue_Init (
    __out PST_STREAM_BLOCKING_QUEUE Queue,
    __in ULONG Capacity
    )
{
    ZeroMemory(Queue, sizeof(*Queue));
    if (Capacity == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Queue->Items = (PUCHAR)HeapAlloc(GetProcessHeap(), 0, Capacity * sizeof(UCHAR));
    if (Queue->Items == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    Queue->Capacity = Capacity;
    Queue->Limit = Queue->Items + Capacity;
    Queue->Head = Queue->Tail = Queue->Items;
    InitializeSpinLock(&Queue->Lock, LONG_CRITICAL_SECTION_SPINS);
    InitializeListHead(&Queue->WaitList);
    return TRUE;
}

//
// Dispose the stream blocking queue.
//

BOOL
WINAPI
StStreamBlockingQueue_Dispose (
    __inout PST_STREAM_BLOCKING_QUEUE Queue,
    __in ST_STREAM_BLOCKING_QUEUE_DTOR *Dtor,
    __in PVOID Context
    )
{
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    PWAIT_BLOCK WaitBlock;
    PUCHAR Items;
    ULONG Count;
    PUCHAR Head;
    WAKE_LIST WakeList;

    //
    // Initialize the local variables and acquire the queue's lock.
    //

    ListHead = &Queue->WaitList;
    InitializeWakeList(&WakeList);
    AcquireQueueLock(Queue);

    //
    // if the queue was already disposed, release the queue's lock
    // and return false.
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
            WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            if (TryLockParker(WaitBlock->Parker)) {
                AddEntryToWakeList(&WakeList, Entry);
            } else {
                Entry->Flink = Entry;
            }
        } while ((Entry = ListHead->Flink) != ListHead);
    }

    //
    // Get the queue buffer, the available data count and the data head.
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
        UnparkThread(CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry)->Parker, WAIT_DISPOSED);
        Entry = Next;
    }

    //
    // If the queue buffer isn't empty and a dispose callback was
    // specified, call the callback with the available data.
    //

    if (Count != 0 && Dtor != NULL) {
        ULONG TillEnd = (ULONG)(Queue->Limit - Head);
        if (Count <= TillEnd) {
            (*Dtor)(Head, Count, Context);
        } else {
            (*Dtor)(Head, TillEnd, Context);
            (*Dtor)(Items, Count - TillEnd, Context);
        }
    }

    //
    // Free the queue's buffer and return success.
    //

    HeapFree(GetProcessHeap(), 0, Items);
    return TRUE;
}
