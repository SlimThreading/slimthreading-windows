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
#define CONSUMERS	5

//
// The bounded blocking queue.
//

static StBoundedBlockingQueue Queue(16);

//
// The alerter and the count down event used to synchronize shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(CONSUMERS + PRODUCERS);

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
    printf("+++ p #%d started...\n", Id);
    ULONG Timeout = 0;
    do {
        ULONG WaitStatus = Queue.Add((PVOID)0x12345678, 1);
        if (WaitStatus == WAIT_OBJECT_0) {
            Productions[Id]++;
        } else if (WaitStatus == WAIT_TIMEOUT) {
            Timeout++;
        } else if (WaitStatus == WAIT_DISPOSED) {
            break;
        } else if (WaitStatus == WAIT_ALERTED) {
            ;
        } else {
            assert(!"this could never happen!");
            break;
        }
    } while (!Shutdown.IsSet());
    printf("+++ p #%d exits, after [%d/%d]\n", Id, Productions[Id], Timeout);
    Done.Signal();
    return 0;
}

//
// Consumer thread
//

//
// The callback called for each data item when the queue is
// disposed.
//

static
VOID
CALLBACK
DisposeCallback(
    __in PVOID *Item,
    __in ULONG Count,
    __in PVOID Context
    )
{
    PULONG CountPtr = (PULONG)Context;
    *CountPtr += Count;
}

//
// The consumer thread
//

static
ULONG
WINAPI
ConsumerThread (
    __in PVOID arg
    )
{

    LONG Id = (LONG)arg;
    PVOID Result;
    ULONG Timeouts = 0;

    printf("+++ c #%d started...\n", Id);
    do {
        ULONG WaitStatus = Queue.Take(&Result, 1, &Shutdown);
        if (WaitStatus == WAIT_SUCCESS) {
            if (Result == (PVOID)0x12345678) {
                Consumptions[Id]++;
            } else {
                printf("***error: \"0x%p\" is a wrong request\n", Result); 
            }
        } else if (WaitStatus == WAIT_ALERTED) {
            Queue.Dispose(DisposeCallback, &Consumptions[Id]);
            break;
        } else if (WaitStatus == WAIT_TIMEOUT) {
            Timeouts++;
        } else if (WaitStatus == WAIT_DISPOSED) {
            break;
        } else {
            assert(!"this could never happen!");
            break;
        }
    } while (true);
    printf("+++ c #%d exits: [%d/%d]\n", Id, Consumptions[Id], Timeouts);
    Done.Signal();
    return 0;
}

VOID
RunBoundedBlockingQueueTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    //
    // Create the threads.
    //

    for (int i = 0; i < CONSUMERS; i++) {
        HANDLE Consumer = CreateThread(NULL, 0, ConsumerThread, (PVOID)(i), 0, NULL);
        CloseHandle(Consumer);
    }
    for (int i = 0; i < PRODUCERS; i++) {
        HANDLE Producer = CreateThread(NULL, 0, ProducerThread, (PVOID)(i), 0, NULL);
        CloseHandle(Producer);
    }

    printf("+++ hit return to terminate the test...\n");
    getchar();
    Shutdown.Set();
    Done.Wait();

    ULONGLONG totalProds = 0;
    for (int i = 0; i < PRODUCERS; i++) {
        totalProds += Productions[i];
    }
    ULONGLONG totalCons = 0;
    for (int i = 0; i < CONSUMERS; i++) {
        totalCons += Consumptions[i];
    }

    printf("+++total: prods = %I64d, cons = %I64d\n", totalProds, totalCons); 
}
