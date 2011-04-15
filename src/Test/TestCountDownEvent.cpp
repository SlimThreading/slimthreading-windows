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
// The number of worker threads.
//

#define WORKERS	20

//
// The event used to synchronize shutdown.
//

static StNotificationEvent Done;

//
// Thread context data.
//

typedef struct _ASYNC_CONTEXT {
    ULONG_PTR Argument;
    ULONG_PTR Result;
    StCountDownEvent *CDEvent;
    StAlerter *Cancel;
} ASYNC_CONTEXT, *PASYNC_CONTEXT;

//
// Simulates an asynchronous computation.
// Runs on a thread pool's thread.
//

static
ULONG
WINAPI
AsyncComputation (
    __in PVOID Ctx
    )
{

    PASYNC_CONTEXT Context = (PASYNC_CONTEXT)Ctx;
    BOOL ResOk;

    srand((LONG)Context->Argument);

    ResOk = StParker::Sleep((rand() % 10), Context->Cancel);
    Context->Result = ResOk ? Context->Argument : -1;
    Context->CDEvent->Signal();
    return 0;
}

//
// Fork/join thread.
//

static
ULONG
WINAPI
ForkJoinThread (
    __in PVOID Argument
    )
{
    StAlerter *Shutdown = (StAlerter *)Argument;
    ULONG WaitStatus;
    ULONG Timeouts = 0;
    ULONG Computations;
    ASYNC_CONTEXT Contexts[WORKERS];

    printf("+++ f/j thread started...\n");

    for (int i = 0; i < WORKERS; i++) {
        Contexts[i].Argument = i + 1;
        Contexts[i].Cancel = Shutdown;
    }
    Computations = 0;
    do {

        StCountDownEvent Event(WORKERS);
        for (int i = 0; i < WORKERS; i++) {
            Contexts[i].CDEvent = &Event;
            Contexts[i].Argument = i + 1;
            Contexts[i].Result = 0;
            QueueUserWorkItem(AsyncComputation, &Contexts[i], WT_EXECUTEDEFAULT);
        }

        //
        // Synchronize with all completion.
        //

        do {
            WaitStatus = Event.Wait(1);
            if (WaitStatus != WAIT_TIMEOUT) {
                break;
            }
            Timeouts++;
        } while (true);
        if (Shutdown->IsSet()) {
            //printf("***fork/join thread ALERTED\n");
            break;
        }

        assert(WaitStatus == WAIT_OBJECT_0);
        ULONG_PTR Result = 0;

        //
        // Compute result.
        //

        for (int i = 0; i < WORKERS; i++) {
            Result += Contexts[i].Result;
        }
            
        ULONG_PTR Tmp = (WORKERS * (WORKERS + 1)) / 2;

        if (Result == Tmp) {
            Computations++;
        } else {
            printf("***computation error: expected %d gotten %d\n", Tmp, Result);
        }
    } while (true);
    printf("+++ f/j thread exiting: [%d/%d]\n", Computations, Timeouts);
    Done.Signal();
    return 0;
}

VOID
RunCountDownEventTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    StAlerter Shutdown;
    CreateThread(NULL, 0, ForkJoinThread, &Shutdown, 0, NULL);
    getchar();
    Shutdown.Set();
    Done.Wait();
}
