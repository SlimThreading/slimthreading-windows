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

#define CONTROLLERS		4
#define WORKERS			20

//
// The alerter and the count down event used to synhcronize the shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(CONTROLLERS);

//
// The Work item
//

struct WORK_ITEM {
    ULONG Id;
    ULONG Delay;
    StNotificationEvent *Done;

    WORK_ITEM(ULONG id, ULONG delay, StNotificationEvent *d) {
        Id = id;
        Delay = delay;
        Done = d;
    }
};

//
// The worker thread
//

static
ULONG
WINAPI
WorkerThread (
    __in PVOID Arg
    )
{

    WORK_ITEM *wi = (WORK_ITEM *)Arg;
    printf("+++ wrk #%d will take %d ms to process its work item\n",
           wi->Id, wi->Delay);
    StParker::Sleep(wi->Delay, &Shutdown);
    wi->Done->Set();
    delete wi;
    return 0;
}

//
// The controller thread.
//

static
ULONG
WINAPI
Controller (
    __in PVOID Arg
    )
{
    int Id = (int)Arg;
    StNotificationEvent Events[WORKERS];
    StWaitable *Waitables[WORKERS];
    ULONG WaitStatus;
    ULONG Timeouts = 0, Count = 0;

    printf("+++controller #%d started...\n", Id);
    srand(((ULONG)&Id) >> 12);
    do {
        printf("\n+++controller #%d starts its workers...\n", Id);
        for (int i = 0; i < WORKERS; i++) {
            WORK_ITEM *wi = new WORK_ITEM(i, (rand() % 10) + 10, &Events[i]);
            Waitables[i] = &Events[i];
            Events[i].Init(false);
            QueueUserWorkItem(WorkerThread, (PVOID)wi, WT_EXECUTEDEFAULT);
        }
        do {
            WaitStatus = StWaitable::WaitAll(WORKERS, Waitables, 100);
            if (WaitStatus == WAIT_SUCCESS) {
                for (int i = 0; i < WORKERS; i++) {
                    Events[i].Reset();
                }
                Count++;
                break;
            }
            Timeouts++;
        } while (true);
        printf("+++ controller %d, synchronized with all of its workers\n", Id);
        StParker::Sleep(rand()% 500, &Shutdown);
    } while (!Shutdown.IsSet());
    printf("+++ controller #%d exiting: [%d/%d]\n", Id, Count, Timeouts);
    Done.Signal();
    return 0;
}

//
// The test function.
//

VOID
RunWaitForMultipleNotificationEventsTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < CONTROLLERS; i++) {
        HANDLE Thread = CreateThread(NULL, 0, Controller, (PVOID)i, 0, NULL);
        CloseHandle(Thread);
    }
    getchar();
    Shutdown.Set();
    Done.Wait();
}
