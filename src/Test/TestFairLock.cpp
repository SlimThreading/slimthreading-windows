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
// The alerter and the count down event used to synchronize the shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(THREADS);

//
// Counters
//

static ULONG Counts[THREADS];

//
// Acquire/release probability.
//

#define P	50

/*++
 *
 * Test the Fair Lock.
 *
 --*/

//
// The non-reentrant fair lock.
//

static StFairLock FairLock;

//
// Non-reentrant fair lock acquire/releaser thread
//

static
ULONG
WINAPI
AcquirerReleaser (
    __in PVOID Argument
    )
{
    ULONG WaitStatus;
    ULONG Timeouts = 0;
    ULONG Id = (ULONG)Argument;
    ULONG Rnd;
    StWaitable *WaitableLock = &FairLock;

    printf("+++a/r #%d started\n", Id);

    srand(((ULONG)&Id) >> 12);
    Rnd = rand();
    do {
        if ((Rnd % 100) < P) {
        
            do {
                WaitStatus = FairLock.TryEnter(1, &Shutdown);
                //WaitStatus = FairLock.WaitOne(1, &Shutdown);
                //WaitStatus = StWaitable::WaitAny(1, &WaitableLock, 1, &Shutdown);
                //WaitStatus = StWaitable::WaitAll(1, &WaitableLock, 1, &Shutdown);
                if (WaitStatus == WAIT_ALERTED) {
                    goto Exit;
                }
                if (WaitStatus == WAIT_OBJECT_0) {
                    break;
                }
                Timeouts++;
            } while (true);
            Rnd = rand();
            FairLock.Exit();
        } else {
            Rnd = rand();
        }
        Counts[Id]++;
        if ((Counts[Id] % 5000) == 0) {
            printf("-%d", Id);
        }
    } while (!Shutdown.IsSet());
Exit:
    printf("+++a/r #%d exist: [%d/%d]\n", Id, Counts[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The fair lock test.
//

VOID
RunFairLockTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < THREADS; i++) {
        HANDLE ThreadHandle = CreateThread(NULL, 0, AcquirerReleaser, (PVOID)(i), 0, NULL);
        CloseHandle(ThreadHandle);
    }

    int Start = GetTickCount();
    getchar();
    int Elapsed = GetTickCount() - Start;

    //
    // Alert the waiting threads and wait until its termination.
    //

    Shutdown.Set();
    Done.Wait();

    ULONGLONG Total = 0;
    for (int i = 0; i < THREADS; i++) {
        Total += Counts[i];
    }
            
    printf("+++acquires/releases: %I64d, unit cost: %d ns\n",
          Total, (int)((Elapsed * 1000000.0)/ Total));
}

/*++
 *
 * Test the Reentrant Fair Lock.
 *
 --*/

//
// The reentrant fair lock.
//

static StReentrantFairLock RFairLock;

//
// The recursive acquisition counter.
//

#define RECURSE		5

//
// Reentrant fair lock acquirer/releaser thread.
//

static
ULONG
WINAPI
RAcquirerReleaser (
    __in PVOID Argument
    )
{
    ULONG WaitStatus;
    ULONG Timeouts = 0;
    ULONG Id = (ULONG)Argument;
    ULONG Rnd;
    StWaitable *WaitableLocks[] = { &RFairLock };

    printf("+++Ra/r #%d started\n", Id);

    srand(((ULONG)&Id) >> 12);
    Rnd = rand();
    do {
        if ((Rnd % 100) < P) {
            do {		
                WaitStatus = RFairLock.TryEnter(1, &Shutdown);
                //WaitStatus = RFairLock.WaitOne(1, &Shutdown);
                //WaitStatus = StWaitable::WaitAny(1, WaitableLocks, 1, &Shutdown);
                //WaitStatus = StWaitable::WaitAll(1, WaitableLocks, 1, &Shutdown);
                if (WaitStatus == WAIT_ALERTED) {
                    goto Exit;
                }
                if (WaitStatus == WAIT_SUCCESS) {
                    break;
                }
                Timeouts++;
            } while (true);
            for (int i = 1; i < RECURSE; i++) {
                RFairLock.Enter();
            }

            Rnd = rand();
            for (int i = 0; i < RECURSE; i++) {
                RFairLock.Exit();
            }
        } else {
            Rnd = rand();
        }
        Counts[Id]++;
        if ((Counts[Id] % 5000) == 0) {
            printf("-%d", Id);
        }
    } while (!Shutdown.IsSet());
Exit:
    printf("+++Ra/r #%d exists: [%d/%d]\n", Id, Counts[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The test function.
//

VOID
RunReentrantFairLockTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < THREADS; i++) {
        HANDLE ThreadHandle = CreateThread(NULL, 0, RAcquirerReleaser, (PVOID)(i), 0, NULL);
        CloseHandle(ThreadHandle);
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
            
    printf("+++acquires/releases: %I64d, unit cost: %d ns\n",
           Total, (int)((Elapsed * 1000000.0)/ Total));
}
