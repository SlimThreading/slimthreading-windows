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
// The asynchronous context structure.
//

typedef struct _ASYNC_CONTEXT {
    ULONG Result;
    StFuture Future;
    StAlerter *Cancel;
} ASYNC_CONTEXT, *PASYNC_CONTEXT;

//
// Simulates an asynchronous computation and runs on a
// thread pool's thread.
//

static
ULONG
WINAPI
AsyncCompute (
    __in PVOID Ctx
    )
{

    PASYNC_CONTEXT Context = (PASYNC_CONTEXT)Ctx;
    BOOL ResOk;

    srand(Context->Result);
    ResOk = StParker::Sleep((rand() % 5), Context->Cancel);
    Context->Future.SetValue(ResOk ? Context->Result : -1);
    assert(ResOk);
    return 0;
}

//
// Fork/join thread that uses wait-all.
//

static
ULONG
WINAPI
WaitAllForkJoinThread (
    __in PVOID Argument
    )
{
    StAlerter *Shutdown = (StAlerter *)Argument;
    ULONG WaitStatus;
    ULONG Timeouts = 0;
    ULONG Computations;
    StWaitable *FutureArray[WORKERS];
    ASYNC_CONTEXT Contexts[WORKERS];

    printf("+++ f/j thread started...\n");

    for (int i = 0; i < WORKERS; i++) {
        Contexts[i].Result = i + 1;
        Contexts[i].Cancel = Shutdown;
        FutureArray[i] = &Contexts[i].Future;
    }
    Computations = 0;
    do {

        for (int i = 0; i < WORKERS; i++) {
            Contexts[i].Future.Reset();
            QueueUserWorkItem(AsyncCompute, &Contexts[i], WT_EXECUTEDEFAULT);
        }

        //
        // Synchronize with all completion.
        //

        do {
            WaitStatus = StWaitable::WaitAll(WORKERS, FutureArray, 1,  NULL);
            if (WaitStatus != WAIT_TIMEOUT) {
                break;
            }
            Timeouts++;
        } while (true);
        if (Shutdown->IsSet()) {
            //printf("***f/j thread ALERTED\n");
            break;
        }

        assert(WaitStatus == WAIT_OBJECT_0);
        ULONG_PTR Result = 0;
        ULONG_PTR Tmp;

        //
        // Get future values and compute result.
        //

        for (int i = 0; i < WORKERS; i++) {
            Contexts[i].Future.TryGetValue(&Tmp);
            Result += Tmp;
        }
            
        Tmp = (WORKERS * (WORKERS + 1)) / 2;

        if (Result == Tmp) {
            Computations++;
            //printf("--computation result: %d", Result);
        } else {
            printf("***computation error: expected %d gotten %d\n", Tmp, Result);
        }
    } while (true);
    printf("+++ f/j thread exiting: [%d/%d]\n", Computations, Timeouts);
    Done.Signal();
    return 0;
}


//
// Teh fork/join thread that uses wait-any.
//

static
ULONG
WINAPI
WaitAnyForkJoinThread (
    __in PVOID Argument
    )
{
    StAlerter *Shutdown = (StAlerter *)Argument;
    ULONG WaitStatus;
    ULONG Timeouts = 0;
    ULONG Computations;
    ULONG Count;
    StWaitable *FutureArray[WORKERS];
    ASYNC_CONTEXT Contexts[WORKERS];

    printf("+++ f/j thread started...\n");

    for (int i = 0; i < WORKERS; i++) {
        Contexts[i].Result = i + 1;
        Contexts[i].Cancel = Shutdown;
    }
    Computations = 0;
    do {

        for (int i = 0; i < WORKERS; i++) {
            FutureArray[i] = &Contexts[i].Future;
            Contexts[i].Future.Reset();
            QueueUserWorkItem(AsyncCompute, &Contexts[i], WT_EXECUTEDEFAULT);
        }

        //
        // Synchronize with all completion.
        //

        Count = WORKERS;
        ULONG_PTR Result = 0;
        do {
            WaitStatus = StWaitable::WaitAny(WORKERS, FutureArray, 1, Shutdown);
            if (WaitStatus >= WAIT_OBJECT_0 && WaitStatus < WAIT_OBJECT_0 + WORKERS) {
                ULONG Index = WaitStatus - WAIT_OBJECT_0;
                ULONG_PTR Tmp;

                Contexts[Index].Future.TryGetValue(&Tmp);
                Result += Tmp;
                FutureArray[Index] = NULL;
                Count--;
            } else  if (WaitStatus == WAIT_TIMEOUT) {
                Timeouts++;
            } else if (WaitStatus == WAIT_ALERTED) {
                for (int i = 0; i < WORKERS; i++) {
                    FutureArray[i] = &Contexts[i].Future;
                }
                StWaitable::WaitAll(WORKERS, FutureArray);
                goto Exit;
            } else {
                assert(!"***we should never get here!");
            }
        } while (Count != 0);
        if (Result == (WORKERS * (WORKERS + 1)) / 2) {
            Computations++;
            //printf("--computation result: %d", Result);
        } else {
            printf("***computation error: expected %d gotten %d\n",
                    (WORKERS * (WORKERS + 1)) / 2, Result);
        }
    } while (true);
Exit:
    printf("+++ f/j thread exiting: [%d/%d]\n", Computations, Timeouts);
    Done.Signal();
    return 0;
}

//
// The wait-all test.
//

VOID
RunWaitAllFutureTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    StAlerter Shutdown;

    CreateThread(NULL, 0, WaitAllForkJoinThread, &Shutdown, 0, NULL);
    printf("+++hit <enter> to terminate...\n");
    getchar();
    Shutdown.Set();
    Done.Wait();
}

VOID
RunWaitAnyFutureTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    StAlerter Shutdown;

    CreateThread(NULL, 0, WaitAnyForkJoinThread, &Shutdown, 0, NULL);
    printf("+++hit <enter> to terminate...\n");
    getchar();
    Shutdown.Set();
    Done.Wait();
}
