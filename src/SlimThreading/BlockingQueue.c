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
// Waits until a data item is available on blocking queue,the specified
// timeout expires or the alerter is set.
//

ULONG
WINAPI
StBlockingQueue_TakeEx (
    __in PVOID Queue,
    __out PVOID *Result,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    WAIT_BLOCK WaitBlock;
    ST_PARKER Parker;
    PST_BLOCKING_QUEUE Queuex;
    ULONG WaitStatus;

    Queuex = (PST_BLOCKING_QUEUE)Queue;
    //
    // Try to take a data item immediately from the queue.
    //
    
    if (TRY_TAKE(Queuex, Result)) {
        return WAIT_SUCCESS;
    }
    
    //
    // The queue is empty; so, return failure, if a null timeout was specified.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }

    //
    // Initialize the parker and execute the take prologue.
    //
    
    InitializeParker(&Parker, 1);
    TAKE_PROLOGUE(Queuex, &Parker, &WaitBlock, 0);

    //
    // Park the current thread, activating the specified cancellers.
    //

    WaitStatus = ParkThreadEx(&Parker, 0, Timeout, Alerter);
    
    //
    // If the take succeed, retrieve the data item from the wait block
    // and return success.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        *Result = WaitBlock.Channel;
        return WAIT_SUCCESS;
    }

    //
    // If the queue was disposed, return the appropriate failure status.
    //

    if (WaitStatus == WAIT_DISPOSED_0) {
        return WAIT_DISPOSED_0;
    }

    //
    // The take operation was cancelled; so, cancel the take attempt
    // on the queue and report the failure appropriately.
    //

    CANCEL_TAKE(Queuex, &WaitBlock);
    return WaitStatus;
}


//
// Waits unconditionally until a data item is available on the blocking queue.
//

BOOL
WINAPI
StBlockingQueue_Take (
    __in PVOID Queue,
    __out PVOID *Result
    )
{
    return StBlockingQueue_TakeEx(Queue, Result, INFINITE, NULL) == WAIT_SUCCESS;
}

//
// Takes a data item from one of the specified queues, activating
// the specified cancellers.
//

ULONG
WINAPI
StBlockingQueue_TakeAnyEx (
    __in ULONG Offset,
    __in ULONG Count,
    __in ULONG Length,
    __in PVOID Queues[],
    __out PVOID *Result,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    WAIT_BLOCK WaitBlockArray[MAXIMUM_WAIT_OBJECTS];
    ST_PARKER Parker;
    PST_BLOCKING_QUEUE Queue;
    ULONG Defined;
    ULONG Index;
    ULONG Iter;
    LONG LastVisited;
    ULONG WaitStatus;
    ULONG TakenQueue;

    //
    // Validate the parameters.
    //

    if (Offset >= Length || Count > Length || Length > MAXIMUM_WAIT_OBJECTS) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }

    //
    // Try to take a data item from any of the specified queues.
    //
    
    Defined = 0;
    for (Index = Offset, Iter = 0; Iter < Count; Iter++) {
        Queue = (PST_BLOCKING_QUEUE)Queues[Index];
        if (Queue != NULL) {
            if (TRY_TAKE(Queue, Result)) {
                return (WAIT_OBJECT_0 + Index);
            }
            Defined++;
        }
        if (++Index >= Length) {
            Index = 0;
        }
    }

    //
    // Return failure, If the queue array doesn't contain any
    // non-null pointer,
    //

    if (Defined == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }
    
    //
    // We can't take immediately from none of the specified queues;
    // so, return failure, if a null timeout was specified.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }

    //
    // Initialize the parker and execute the take-any prologue on
    // the specified queues.
    //
    
    InitializeParker(&Parker, 1);
    LastVisited = -1;
    for (Index = Offset, Iter = 0; !IsParkerLocked(&Parker) && Iter < Count; Iter++) {
        Queue = (PST_BLOCKING_QUEUE)Queues[Index];
        if (Queue != NULL) {

            //
            // Execute the take-any prologue for the current queue and,
            // if the take operation is satisfied, break the loop.
            //

            if (TAKE_PROLOGUE(Queue, &Parker, &WaitBlockArray[Index], Index)) {
                break;
            }
            LastVisited = (LONG)Index;
        }		
        if (++Index >= Length) {
            Index = 0;
        }
    }

    //
    // Park the current thread, activating the specified cancellers.
    //

    WaitStatus = ParkThreadEx(&Parker, 0, Timeout, Alerter);
    
    //
    // If the take succeed, retrieve the data item from the
    // corresponding wait block.
    //

    TakenQueue = ~0;
    if (WaitStatus >= WAIT_OBJECT_0 && WaitStatus < (WAIT_OBJECT_0 + Count)) {
        TakenQueue = WaitStatus - WAIT_OBJECT_0;
        *Result = WaitBlockArray[TakenQueue].Channel;
    } else if (WaitStatus >= WAIT_DISPOSED_0 && WaitStatus < (WAIT_DISPOSED_0 + Count)) {
        TakenQueue = WaitStatus - WAIT_DISPOSED_0;
        *Result = NULL;
    }

    //
    // Cancel the take attempts that can be still pending.
    //

    if (LastVisited >= 0) {
        Index = Offset;
        do {
            Queue = (PST_BLOCKING_QUEUE)Queues[Index];
            if (Queue != NULL && Index != TakenQueue) {
                CANCEL_TAKE(Queue, &WaitBlockArray[Index]);
            }
            if ((LONG)Index == LastVisited) {
                break;
            }
            if (++Index >= Length) {
                Index = 0;
            }
        } while (TRUE);
    }

    //
    // Return the appropriate wait status.
    //

    return WaitStatus;
}
//
// Waits unconditionally until take a data item from one of the
// specified queues.
//

ULONG
WINAPI
StBlockingQueue_TakeAny (
    __in ULONG Offset,
    __in ULONG Count,
    __in ULONG Length,
    __in PVOID Queues[],
    __out PVOID *Result
    )
{
    return StBlockingQueue_TakeAnyEx(Offset, Count, Length, Queues, Result, INFINITE, NULL);
}
