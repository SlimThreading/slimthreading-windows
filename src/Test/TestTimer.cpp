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
// Controller threads.
//

#define THREADS	20

//
// Alerter and count down event used to synchronize shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(THREADS);


//
// Number of timers used for each controller thread.
//

#define TIMERS	50

//
// The controller thread.
//

VOID
CALLBACK
TimerCallback (
    __inout_opt PVOID Context,
    __in ULONG Ignored
    )
{
    StTimer *Timer = (StTimer *)Context;
    Timer->Cancel();
}


static
ULONG
WINAPI
Controller (
    __in PVOID Argument
    )
{
    ULONG Id = (ULONG)Argument;
    StTimer Timers[TIMERS];
    StWaitable *Waitables[TIMERS];
    ULONG WaitStatus;
    ULONG Count = 0;
    ULONG Failed = 0;

    printf("+++ ctrl #%d starts...\n", Id);
    srand(((ULONG)&Count) >> 12);
    for (int i = 0; i < TIMERS; i++) {
        Waitables[i] = Timers + i;
    }

    do {

        //
        // Start the timers.
        //

        ULONG MaxTime = 0;
        for (int i = 0; i < TIMERS; i++) {
            ULONG Delay = (rand() % 500) + 100;
            Timers[i].Set(Delay, 100, TimerCallback, Timers + i);
            if (Delay > MaxTime) {
                MaxTime = Delay;
            }
        }

        ULONG Start = GetTickCount();
        WaitStatus = StWaitable::WaitAll(TIMERS, Waitables, 500 + (rand() % 200), &Shutdown);
        if (WaitStatus == WAIT_SUCCESS) {
            ULONG Elapsed = GetTickCount() - Start;
            printf("+++ ctrl #%d, synchronized with its timers[%d/%d]\n", Id, MaxTime, Elapsed);
            Count++;
        } else {
            if (WaitStatus == WAIT_TIMEOUT) {
                printf("-- ctrl #%d, timeout expired\n", Id);
            } else {
                printf("-- ctrl #%d, CANCELLED\n", Id);
            }
            for (int i = 0; i < TIMERS; i++) {
                Timers[i].Cancel();
            }
            Failed++;
        }
    } while (!Shutdown.IsSet());
    printf("+++ ctrl #%d exiting after [%d/%d] synchs...\n", Id, Count, Failed);
    Done.Signal();
    return 0;
}

//
// The test itself.
//

VOID
RunTimerTest (
    )
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < THREADS; i++) {
        CreateThread(NULL, 0, Controller, (PVOID)i, 0, NULL);
    }
    getchar();
    Shutdown.Set();
    Done.Wait();
}
