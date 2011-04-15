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
// The number of recursive acquisitions.
//

#define RECURSE		5

//
// The reentrant lock
//

static StReentrantLock RLock(100);

//
// The Win32 critical section
//

static CRITICAL_SECTION Win32Crst;

//
// The alerter and count down event used to synchronize shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(THREADS);

//
// Counters
//

static ULONG Loops[THREADS];

//
// Shared random
//

LONG SharedSeed;

//
// Probability
//

LONG S = 100;
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
// Acquire/Releaser thread
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
    ULONG Timeouts = 0;
    LocalSeed = s = GetCurrentThreadId();
    ULONG Count = 0;

    printf("+++a/r #%d started\n", Id);
    do {
        if ((s % 100) < S) {
            for (int i = 0; i < RECURSE; i++) {
                //RLock.Enter();
            
                
                while (!RLock.TryEnter(1)) {
                    Timeouts++;
                }
            }
            s = SharedSeed = NextRandom(SharedSeed);
            for (int i = 0; i < RECURSE; i++) {
                RLock.Exit();
            }
        } else {
            s = LocalSeed = NextRandom(LocalSeed);
        }
        Loops[Id]++;
        if ((++Count % 25000) == 0) {
            printf("-%d", Id);
            StParker_Sleep(250);
        }
    } while (!Shutdown.IsSet());

    printf("+++a/r #%d exists: [%d/%d]\n", Id, Loops[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The test itself.
//

VOID
RunReentrantLockTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    SharedSeed = GetTickCount();

    //
    // Create the threads.
    //

    for (int i = 0; i < THREADS; i++) {
        //HANDLE ThreadHandle = CreateThread(NULL, 0, AcquirerReleaser, (PVOID)(i), 0, NULL);
        HANDLE ThreadHandle = StUmsThread_Create(NULL, 0, AcquirerReleaser, (PVOID)(i), 0, NULL);
        CloseHandle(ThreadHandle);
    }

    int start = GetTickCount();
    getchar();
    int elapsed = GetTickCount() - start;

    //
    // Alert all threads and synchronize with its termination.
    //

    Shutdown.Set();
    Done.Wait();

    ULONGLONG total = 0;
    for (int i = 0; i < THREADS; i++) {
        total += Loops[i];
    }
            
    printf("Loops: %I64d, unit cost: %d ns\n", total,
            (LONG)((elapsed * 1000000.0)/ total) - 20);
}

//
// Acquire/Releaser thread using a Windows Critical Section
//

static
ULONG
WINAPI
NtAcquirerReleaser (
    __in PVOID Argument
    )
{
    ULONG Id = (ULONG)Argument;
    LONG LocalSeed, s;

    printf("+++Nt a/r #%d started\n", Id);
    LocalSeed = s = GetCurrentThreadId();
    do {
        if ((s % 100) < S) {
            for (int i = 0; i < RECURSE; i++) {
                EnterCriticalSection(&Win32Crst);
            }
            s = SharedSeed = NextRandom(SharedSeed);
            for (int i = 0; i < RECURSE; i++) {
                LeaveCriticalSection(&Win32Crst);
            }
        } else {
            s = LocalSeed = NextRandom(LocalSeed);
        }
        Loops[Id]++;
    } while (!Shutdown.IsSet());

    printf("+++Nt a/r #%d exists: [%d]\n", Id, Loops[Id]);
    Done.Signal();
    return 0;
}

VOID
RunWindowsCriticalSectionTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);
    InitializeCriticalSectionEx(&Win32Crst, 100, CRITICAL_SECTION_NO_DEBUG_INFO);
    SharedSeed = GetTickCount();

    //
    // Create the threads.
    //

    for (int i = 0; i < THREADS; i++) {
        HANDLE ThreadHandle = CreateThread(NULL, 0, NtAcquirerReleaser, (PVOID)(i), 0, NULL);
        CloseHandle(ThreadHandle);
    }
    int start = GetTickCount();
    getchar();
    int elapsed = GetTickCount() - start;

    //
    // Alert the waiting threads and synchronize with its termination.
    //

    Shutdown.Set();
    Done.Wait();

    ULONGLONG total = 0;
    for (int i = 0; i < THREADS; i++) {
        total += Loops[i];
    }
            
    printf("Nt loops: %I64d, unit cost: %d ns\n", total,
            (LONG)((elapsed * 1000000.0)/ total) - 20);
}
