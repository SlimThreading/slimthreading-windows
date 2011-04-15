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
// The number of controller and worker threads.
//

#define CONTROLLERS		2
#define WORKERS			10

//
// The alerter and the count down event used to synchronize the shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(CONTROLLERS);

//
// The work item
//

struct WORK_ITEM {
    ULONG Id;
    ULONG Delay;
    StSemaphore *Completed;

    WORK_ITEM(ULONG id, ULONG delay, StSemaphore *c) {
        Id = id;
        Delay = delay;
        Completed = c;
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
    printf("+++ wrk #%d will take %d ms to process its work item\n", wi->Id, wi->Delay);
    StParker::Sleep(wi->Delay, &Shutdown);
    wi->Completed->Release();
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
    ULONG Id = (ULONG)Arg;
    StSemaphore Sems[WORKERS];
    StWaitable *Waitables[WORKERS];
    ULONG WaitStatus;
    ULONG Timeouts = 0, Count = 0;

    printf("+++controller #%d started...\n", Id);
    srand(((ULONG)&Id) >> 12);
    
    do {
        printf("\n+++controller #%d starts its workers...\n", Id);
        for (int i = 0; i < WORKERS; i++) {
            WORK_ITEM *wi = new WORK_ITEM(i, (rand() % 10) + 10, &Sems[i]);
            Waitables[i] = &Sems[i];
            Sems[i].Init(0, 1);
            QueueUserWorkItem(WorkerThread, (PVOID)wi, WT_EXECUTEDEFAULT);
        }
        do {
            WaitStatus = StWaitable::WaitAll(WORKERS, Waitables, rand() % 50, NULL);
            if (WaitStatus == WAIT_SUCCESS) {
                break;
            }
            Timeouts++;
        } while (true);
        printf("+++ controller %d, synchronized with all of its workers\n", Id);
        Count++;
        Sleep(rand() % 100);
    } while (!Shutdown.IsSet());
    printf("+++ controller #%d exiting: [%d/%d]\n", Id, Count, Timeouts);
    Done.Signal();
    return 0;
}

//
// The test function.
//

VOID
RunWaitForMultipleSemaphoresTest(
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
