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
// The number of producer threads.
//

#define PRODUCERS	10

//
// The bounded blocking queue.
//

static StBoundedBlockingQueue Queue(16);

//
// The alerter and the count down event used to synchronize shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(PRODUCERS);

//
// The counters.
//

static ULONG Productions[PRODUCERS];
static ULONGLONG Consumptions;

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
        SwitchToThread();
    } while (!Shutdown.IsSet());
    printf("+++ p #%d exits, after [%d/%d]\n", Id, Productions[Id], Timeout);
    Done.Signal();
    return 0;
}

//
// The take consumer callback
//

static
VOID
CALLBACK
TakeCallback (
    __in PVOID Ignored,
    __in PVOID DataItem,
    __in ULONG WaitStatus
    )
{
    if (WaitStatus == WAIT_SUCCESS) {
        if (DataItem != (PVOID)0x12345678) {
            printf("***wrong data item: %p\n", DataItem);
        } else {
            Consumptions++;
        }
    } else if (WaitStatus == WAIT_TIMEOUT) {
        printf("+++TIMEOUT\n");
    }
}

VOID
RunRegisteredTakeTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    //
    // Register...
    //

    StRegisteredTake RegTake;

    StBlockingQueue::RegisterTake(&Queue, &RegTake, TakeCallback, NULL, 250, FALSE);

    //
    // Create the threads.
    //

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

    printf("+++total: prods = %I64d, cons = %I64d\n", totalProds, Consumptions); 
    getchar();
    RegTake.Unregister();
}
