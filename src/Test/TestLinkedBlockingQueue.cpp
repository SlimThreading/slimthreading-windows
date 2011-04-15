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

#define PRODUCERS	10
#define CONSUMERS	10 

//
// The linked blocking queue.
//

static StLinkedBlockingQueue Queue;

//
// The alerter and the count down event used to synchronize shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(PRODUCERS + CONSUMERS);

//
// The counters.
//

static ULONG Productions[PRODUCERS];
static ULONG Consumptions[CONSUMERS];

//
// The producer thread.
//

static
ULONG
WINAPI
ProducerThread (
    __in PVOID arg
    )
{

    LONG Id = (LONG)arg;
    PSLIST_ENTRY Message;

    printf("+++ producer #%d started...\n", Id);
    do {
        Message = (PSLIST_ENTRY)malloc(sizeof(*Message));
        if (Message == NULL) {
            putchar('-');
            Sleep(10);
            continue;
        }
        if (Queue.Add(Message)) {;
            Productions[Id]++;
        } else {
            break;
        }
        SwitchToThread();
    } while (!Shutdown.IsSet());
    printf("+++ producer #%d exits: [%d]\n", Id, Productions[Id]);
    Done.Signal();
    return 0;
}

//
// The callback function called when the queue is disposed.
//

static
VOID
CALLBACK
DisposeCallback (
    __in PSLIST_ENTRY List,
    __in PVOID Context
    )
{
    PSLIST_ENTRY Next;
    PULONG Count = (PULONG)Context;
    do {
        Next = List->Next;
        free(List);
        *Count += 1;
    } while ((List = Next) != NULL);
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
    ULONG Timeouts = 0;
    PSLIST_ENTRY Message;

    printf("+++ consumer #%d started...\n", Id);
    do {
        ULONG WaitStatus = Queue.Take(&Message, 1, &Shutdown);
        if (WaitStatus == WAIT_SUCCESS) {
            free(Message);
            Consumptions[Id]++;
        } else if (WaitStatus == WAIT_ALERTED) {
            Sleep(5);
            Queue.Dispose(DisposeCallback, &Consumptions[Id]);
            break;
        } else if (WaitStatus == WAIT_DISPOSED) {
            break;
        } else {
            Timeouts++;
        }
    } while (true);
    printf("+++ consumer #%d exits: [%d/%d]\n", Id, Consumptions[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The test itself.
//

VOID
RunLinkedBlockingQueueTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < CONSUMERS; i++) {
        HANDLE Consumer = CreateThread(NULL, 0, ConsumerThread, (PVOID)i, 0, NULL);
        CloseHandle(Consumer);
    }
    for (int i = 0; i < PRODUCERS; i++) {
        HANDLE Producer = CreateThread(NULL, 0, ProducerThread, (PVOID)i, 0, NULL);
        CloseHandle(Producer);
    }

    printf("+++ hit <enter> to terminate the test...\n");
    getchar();
    Shutdown.Set();
    Done.Wait();

    ULONGLONG TotalProds = 0;
    for (int i = 0; i < PRODUCERS; i++) {
        TotalProds += Productions[i];
    }

    ULONGLONG TotalCons = 0;
    for (int i = 0; i < CONSUMERS; i++) {
        TotalCons += Consumptions[i];
    }
    printf("+++ Total: productions = %I64d, consumptions = %I64d\n", TotalProds, TotalCons); 
}
