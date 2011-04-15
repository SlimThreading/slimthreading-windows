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

#include "stdafx.h"

//
// The number of threads.
//

#define LINKED_PRODUCERS	5
#define BOUNDED_PRODUCERS	10
#define CONSUMERS			10

//
// The blocking queues.
//

static StLinkedBlockingQueue LinkedQueues[LINKED_PRODUCERS];
static StBoundedBlockingQueue BoundedQueues[BOUNDED_PRODUCERS];

//
// The counters.
//

static ULONG LinkedProductions[LINKED_PRODUCERS];
static ULONG BoundedProductions[BOUNDED_PRODUCERS];
static ULONG Consumptions[CONSUMERS];

//
// The alerter and the count down event used to synchronize the shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(CONSUMERS + LINKED_PRODUCERS + BOUNDED_PRODUCERS);

//
// Message used with linked queues.
//

typedef struct _MESSAGE {
    SLIST_ENTRY Link;
    ULONG Origin;
} MESSAGE, *PMESSAGE;

//
// Linked producer thread
//

static
ULONG
WINAPI
LinkedProducerThread (
    __in PVOID Arg
    )
{

    ULONG Id = (ULONG)Arg;
    PMESSAGE Message;

    printf("+++ up #%d started...\n", Id);
    do {

        Message = (PMESSAGE)malloc(sizeof(MESSAGE));
        if (Message == NULL) {
            Sleep(10);
            continue;
        }
        Message->Origin = Id;
        if (LinkedQueues[Id].Add(&Message->Link)) {
            LinkedProductions[Id]++;
        } else {
            break;
        }
        Sleep(1);
    } while (!Shutdown.IsSet());
    printf("+++ up #%d exits: [%d]\n", Id, LinkedProductions[Id]);
    Done.Signal();
    return 0;
}

//
// Bounded producer thread
//

static
ULONG
WINAPI
BoundedProducerThread (
    __in PVOID arg
    )
{

    LONG Id = (LONG)arg;
    printf("+++ bp #%d started...\n", Id);
    ULONG Timeout = 0;
    do {
        ULONG WaitStatus = BoundedQueues[Id].Add((PVOID)Id, 250);
        if (WaitStatus == WAIT_SUCCESS) {
            BoundedProductions[Id]++;
        } else if (WaitStatus == WAIT_DISPOSED) {
            break;
        } else if (WaitStatus == WAIT_TIMEOUT) {
            Timeout++;
        } else {
            assert(!"***this could never appear!");
            break;
        }
    } while (!Shutdown.IsSet());
    printf("+++ bp #%d exits: [%d/%d]\n", Id, BoundedProductions[Id], Timeout);
    Done.Signal();
    return 0;
}

//
// The callback called when an linked queue is disposed.
//

static
VOID
CALLBACK
LinkedDispose (
    __in PSLIST_ENTRY List,
    __in PVOID Context
    )
{
    PSLIST_ENTRY Next;
    PULONG Count = (PULONG)Context;

    do {
        Next = List->Next;
        free(CONTAINING_RECORD(List, MESSAGE, Link));
        *Count += 1;
    } while ((List = Next) != NULL);
}

//
// The callback called when a bounded queue is disposed.
//

VOID
CALLBACK
BoundedDispose (
    __in PVOID *Item,
    __in ULONG Count,
    __in PVOID Context
    )
{
    PULONG CountPtr = (PULONG)Context;
    *CountPtr += Count;
}

//
// The consumer thread.
//

static
ULONG
WINAPI
ConsumerThread (
    __in PVOID arg
    )
{

    LONG Id = (LONG)arg;
    PMESSAGE RecvMessage;
    ULONG Timeout = 0;
    StBlockingQueue* QueueArray[LINKED_PRODUCERS + BOUNDED_PRODUCERS];
    ULONG Index;
    ULONG QueueCount;
    ULONG ActiveCount;

    printf("+++ c #%d started...\n", Id);

    QueueCount = ActiveCount = LINKED_PRODUCERS + BOUNDED_PRODUCERS;
    for (ULONG i = 0; i < QueueCount; i++) {
        if (i < LINKED_PRODUCERS) {
            QueueArray[i] = LinkedQueues + i;
        } else {
            QueueArray[i] = BoundedQueues + (i - LINKED_PRODUCERS);
        }
    }

    do {

        ULONG WaitStatus = StBlockingQueue::TakeAny(0, QueueCount, QueueCount,
                                                    QueueArray, (PVOID *)&RecvMessage,
                                                    1, &Shutdown);
        if (WaitStatus >= WAIT_OBJECT_0 && WaitStatus < (WAIT_OBJECT_0 + QueueCount)) {
            Index = WaitStatus - WAIT_OBJECT_0;

            //
            // Process the received message.
            //

            if (Index < LINKED_PRODUCERS) { 
                if (RecvMessage->Origin == Index) {
                    Consumptions[Id]++;
                } else {
                    printf("***error: \"%d\" is a wrong origin\n", RecvMessage->Origin); 
                }
                free(RecvMessage);
            } else {
                ULONG Origin = Index - LINKED_PRODUCERS;
                if ((ULONG)RecvMessage == Origin) {
                    Consumptions[Id]++;
                } else {
                    printf("***error: \"%d\" is a wrong origin\n", (ULONG)RecvMessage); 
                }
            }
        } else if (WaitStatus >= WAIT_DISPOSED_0 && WaitStatus < (WAIT_DISPOSED_0 + QueueCount)) {
            Index = WaitStatus - WAIT_DISPOSED_0;
            QueueArray[Index] = NULL;
            if (--ActiveCount == 0) {
                break;
            }
        } else if (WaitStatus == WAIT_ALERTED) {

            //
            // Get all pending messages...
            //

            for (ULONG i = 0; i < LINKED_PRODUCERS; i++) {
                LinkedQueues[i].Dispose(LinkedDispose, &Consumptions[Id]);
            }
            for (ULONG i = 0; i < BOUNDED_PRODUCERS; i++) {
                BoundedQueues[i].Dispose(BoundedDispose, &Consumptions[Id]);
            }
            break;
        } else if (WaitStatus == WAIT_TIMEOUT) {
            Timeout++;
        } else {
            assert(!"***this could never appear!");
            break;
        }
    } while (true);
    printf("+++ c #%d exits: [%d/%d]\n", Id, Consumptions[Id], Timeout);
    Done.Signal();
    return 0;
}

//
// The main.
//

VOID
RunBlockingQueueTakeAnyTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    //
    // Create threads.
    //

    for (int i = 0; i < CONSUMERS; i++) {
        CreateThread(NULL, 0, ConsumerThread, (PVOID)(i), 0, NULL);
    }
    for (int i = 0; i < LINKED_PRODUCERS; i++) {
        CreateThread(NULL, 0, LinkedProducerThread, (PVOID)(i), 0, NULL);
    }
    for (int i = 0; i < BOUNDED_PRODUCERS; i++) {
        CreateThread(NULL, 0, BoundedProducerThread, (PVOID)(i), 0, NULL);
    }

    printf("+++ hit return to terminate the test...\n");
    getchar();
    Shutdown.Set();
    Done.Wait();

    //
    // Compute results.
    //

    ULONGLONG totalProds = 0;
    for (int i = 0; i < LINKED_PRODUCERS; i++) {
        totalProds += LinkedProductions[i];
    }
    for (int i = 0; i < BOUNDED_PRODUCERS; i++) {
        totalProds += BoundedProductions[i];
    }
    ULONGLONG totalCons = 0;
    for (int i = 0; i < CONSUMERS; i++) {
        totalCons += Consumptions[i];
    }
    printf("+++total: prods = %I64d, cons = %I64d\n", totalProds, totalCons);
}
