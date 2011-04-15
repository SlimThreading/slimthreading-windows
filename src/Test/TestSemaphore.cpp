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

#define THREADS		20

//
// The semaphore
//

static StSemaphore Semaphore(1, 1, 200);

//
// The alerter and the count down event used to synchronize shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(THREADS);

//
// The counters.
//

static ULONG Counts[THREADS];

//
// Acquire/release probability.
//

#define P	100

//
// The  acquirer/Releaser thread
//

static
ULONG
WINAPI
StAcquirerReleaser (
    __in PVOID Argument
    )
{
    ULONG WaitStatus;
    ULONG Timeouts = 0;
    ULONG Id = (ULONG)Argument;
    LONG Rnd;
    StWaitable *Semaphores[] = { &Semaphore };

    printf("+++a/r #%d started...\n", Id);

    srand(((ULONG)&Id) >> 12);
    Rnd = rand();
    do {
        if ((Rnd % 100) < P) {
            do {
                //WaitStatus = Semaphore.TryAcquire(1, 1, &Shutdown);
                //WaitStatus = Semaphore.WaitOne(1, &Shutdown);
                //WaitStatus = StWaitable::WaitAny(1, Semaphores, 1, &Shutdown);
                WaitStatus = StWaitable::WaitAll(1, Semaphores, 10, &Shutdown);
                if (WaitStatus == WAIT_ALERTED) {
                    goto Exit;
                }
                if (WaitStatus == WAIT_SUCCESS) {
                    break;
                }
                Timeouts++;
            } while (true);
            Rnd = rand();
            Semaphore.Release();
        } else {
            Rnd = rand();
        }
        Counts[Id]++;
    } while (!Shutdown.IsSet());
Exit:
    printf("+++a/r #%d exists: [%d/%d]\n", Id, Counts[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// Semaphore test.
//

VOID
RunSemaphoreTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < THREADS; i++) {
        HANDLE Thread = CreateThread(NULL, 0, StAcquirerReleaser, (PVOID)(i), 0, NULL);
        CloseHandle(Thread);
    }

    int Start = GetTickCount();
    getchar();
    int Elapsed = GetTickCount() - Start;
    Shutdown.Set();
    Done.Wait();

    ULONGLONG Total = 0;
    for (int i = 0; i < THREADS; i++) {
        Total += Counts[i];
    }
    printf("Total a/r: %I64d, unit cost: %d ns\n", Total,
            (LONG)((Elapsed * 1000000.0)/ Total));
}

