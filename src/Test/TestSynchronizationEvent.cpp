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
// The used threads.
//

#define WAITERS		10
#define SETTERS		5

//
// The synchronization event.
//

static StSynchronizationEvent Event(false, 200);

//
// The alerter and the count down event used to synchronize the shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(WAITERS + SETTERS);

//
// The wait and set counters.
//

static ULONG Waits[WAITERS];
static ULONG Sets[SETTERS];
static ULONG FailedSets;

//
// The event waiter thread.
//

static
ULONG
WINAPI
Waiter (
    __in PVOID Arg
    )
{
    ULONG WaitStatus;
    ULONG Timeouts = 0;
    ULONG Id = (ULONG)Arg;
    StWaitable *Events[] = { &Event };

    printf("+++w #%d started...\n", Id);
    srand(((ULONG)&Id) >> 12);
    do {
        do {
            WaitStatus = Event.Wait(1, &Shutdown);				
            //WaitStatus = Event.WaitOne(1, &Shutdown);
            //WaitStatus = StWaitable::WaitAny(1, Events, 1, &Shutdown);
            //WaitStatus = StWaitable::WaitAll(1, Events, 1, &Shutdown);
            if (WaitStatus == WAIT_ALERTED) {
                goto Exit;
            }
            if (WaitStatus == WAIT_SUCCESS) {
                break;
            }
            Timeouts++;
        } while (true);
        if ((++Waits[Id] % 10000) == 0) {
            printf("-w%d", Id);
        }
    } while (true);
Exit:
    printf("+++w #%d exits: [%d/%d]\n", Id, Waits[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The setter thread
//

static
ULONG
WINAPI
Setter (
    __in PVOID Arg
    )
{
    ULONG Id = (ULONG)Arg;
    ULONG Failed = 0;

    printf("+++ s #%d started...\n", Id);
    do {
        if (!Event.Set()) {
            Sets[Id]++;
        } else {
            InterlockedIncrement(&FailedSets);
            Failed++;
            Sleep(1);
        }
    } while (!Shutdown.IsSet());
    printf("+++ s #%d: [%d/%d]\n", Id, Sets[Id], Failed);
    Done.Signal();
    return 0;
}

//
// The test's main function.
//

VOID
RunStSynchronizationEventTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < WAITERS; i++) {
        HANDLE WaiterThread = CreateThread(NULL, 0, Waiter, (PVOID)i, 0, NULL);
        CloseHandle(WaiterThread);
    }
    for (int i = 0; i < SETTERS; i++) {
        HANDLE SetterThread = CreateThread(NULL, 0, Setter, (PVOID)i, 0, NULL);
        CloseHandle(SetterThread);
    }
    printf("+++ hit <enter> to terminate the test...\n");
    getchar();
    Shutdown.Set();
    Done.Wait();

    ULONGLONG TotalWaits = 0;
    for (int i = 0; i < WAITERS; i++) {
        TotalWaits += Waits[i];
    }
    ULONGLONG TotalSets = 0;
    for (int i = 0; i < SETTERS; i++) {
        TotalSets += Sets[i];
    }
    printf("+++ Total: sets = %I64d, waits = %I64d, failed = %d\n",
            TotalSets, TotalWaits,  FailedSets);
}

