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
// The number of partner threads.
//

#define INITIAL_PARTNERS	5
#define TOTAL_PARTNERS		10

//
// The barrier.
//

static
VOID
CALLBACK
BarrierAction (
    __in PVOID Context
    );

static StBarrier Barrier(1, BarrierAction);

//
// Alert source used for test's shutdown.
//

static StAlerter Shutdown;

//
// The partners threads and the thread is seed.
//

static LONG PartnersCount;
static LONG ThreadId;

//
// The count down event that synchronizes termination.
//

static StCountDownEvent Done(1);

//
// Adds a new partner if the maximum wasn't reached.
//

ULONG
WINAPI
PartnerThread (
    __in PVOID arg
    );


static
VOID
NewPartner(
    )
{
    do {
        LONG Count = PartnersCount;
        if (Count == TOTAL_PARTNERS) {
            return;
        }
        if (InterlockedCompareExchange(&PartnersCount, Count + 1, Count) == Count) {
            if (!Barrier.AddPartner()) {
                printf("***Error: AddPartners failed\n");
            }
            Count = InterlockedIncrement(&ThreadId) - 1;
            Done.TryAdd();
            CreateThread(NULL, 0, PartnerThread, (PVOID)Count, 0, NULL);
            return;
        }
    } while (true);
}

//
// Removes a new partner if the minimum wasn't reached.
//

static
bool
RemovePartner (
    )
{
    do {
        LONG Count = PartnersCount;
        if (Count == INITIAL_PARTNERS) {
            return false;
        }
        if (InterlockedCompareExchange(&PartnersCount, Count - 1, Count) == Count) {
            Barrier.RemovePartner();
            return true;
        }
    } while (true);
}

//
// Partner thread
//

static
ULONG
WINAPI
PartnerThread (
    __in PVOID arg
    )
{

    ULONG Id = (LONG)arg;
    ULONG Count = 0;
    ULONG TimedOut = 0;
    ULONG WaitStatus;


    printf("+++ partner #%d started...\n", Id);
    do {

        do {
            WaitStatus = Barrier.SignalAndWait(1, &Shutdown);
            if (WaitStatus == WAIT_OBJECT_0) {
                break;
            } else if (WaitStatus == WAIT_ALERTED) {
                goto Exit;
            } else if (WaitStatus == WAIT_TIMEOUT) {
                TimedOut++;
            } else {
                printf("***partner #%d: WAIT_FAILED\n");
                goto Exit;
            }
        } while (true);
        Count++;
        if (INITIAL_PARTNERS != TOTAL_PARTNERS) {
            if (Id == 0 && (Count % 10000) == 0) {
                NewPartner();
            } else if (Id != 0 && Count >= Id * 10000) {
                if (RemovePartner()) {
                    break;
                }
            }
        }
        //SwitchToThread();
    } while (!Shutdown.IsSet());
Exit:
    printf("+++ partner #%d exits: [%d/%d]\n", Id, Count, TimedOut);
    Done.Signal();
    return 0;
}

//
// Barrier action
//

static ULONG ActionCount = 0;

static
VOID
CALLBACK
BarrierAction (
    __in PVOID Context
    )
{
    if (Context == (PVOID)&Barrier) {
        ActionCount++;
        if ((ActionCount % 500) == 0) {
            putchar('.');
        }
    }
}

VOID
RunBarrierTest (
    )
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    PartnersCount = ThreadId = 1;
    CreateThread(NULL, 0, PartnerThread, (PVOID)0, 0, NULL);
    for (int i = 1; i < INITIAL_PARTNERS; i++) {
        NewPartner();
    }

    getchar();
    Shutdown.Set();
    Done.Wait();
    printf("+++ post phase action executions: %d\n", ActionCount);
}
