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
// The number of acquirer/releaser threads.
//

#define THREADS		5

//
// The Lock
//

static StLock Lock(50);

//
// The alerter and the count down event used to synchronize shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(THREADS);

//
// The counters
//

static ULONG Loops[THREADS];

//
// Shared random
//

static LONG SharedSeed;

//
// Probability used to lock acquire/release.
//

#define S	100

//
// Random number generator
//

static
LONG
NextRandom (
    LONG Seed
    )
{
    int t = (Seed % 1277773) * 16807 - (Seed / 127773) * 2836;
    return (t > 0) ? t : t + 0x7fffffff;
}

//
// The acquire/releaser thread.
//

static
ULONG
WINAPI
AcquirerReleaser (
    __in PVOID Argument
    )
{
    ULONG Id = (ULONG)Argument;
    LONG LocalSeed, s;
    LocalSeed = s = GetCurrentThreadId();
    ULONG Timeouts = 0;

    printf("+++ a/r #%d started...\n", Id);
    do {
        if ((s % 100) < S) {
            //Lock.Enter();
            
            while (!Lock.TryEnter(1)) {
                Timeouts++;
            }
            
            s = SharedSeed = NextRandom(SharedSeed);
            Lock.Exit();
        } else {
            s = LocalSeed = NextRandom(LocalSeed);
        }
        Loops[Id]++;
    } while (!Shutdown.IsSet());
    printf("+++a/r #%d exits: [%d/%d]\n", Id, Loops[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The test function.
//

VOID
RunLockTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    SharedSeed = GetTickCount();
    for (int i = 0; i < THREADS; i++) {
        //HANDLE ThreadHandle = CreateThread(NULL, 0, AcquirerReleaser, (PVOID)(i), 0, NULL);
        HANDLE ThreadHandle = StUmsThread_Create(NULL, 0, AcquirerReleaser, (PVOID)(i), 0, NULL);
        CloseHandle(ThreadHandle);
    }

    int start = GetTickCount();
    getchar();
    int elapsed = GetTickCount() - start;

    //
    // Alert all threads
    //

    Shutdown.Set();
    Done.Wait();
    ULONGLONG total = 0;
    for (int i = 0; i < THREADS; i++) {
        total += Loops[i];
    }
            
    printf("+++ Loops: %I64d, unit cost: %d ns\n", total,
           (LONG)((elapsed * 1000000.0)/ total) - 20);
}
