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
// The number of threads, synchronizers and acquire count.
//

#define THREADS				10
#define SYNCHRONIZERS		20
#define ACQUIRE_COUNT		10

//
// The alerter and the count down event used to synchronize the shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(THREADS);

//
// The counters.
//

static ULONG Counts[THREADS];

/*++
 *
 * Test reentrant fair lock.
 *
 --*/

//
// The reentrant fair locks.
//

static StReentrantFairLock FairLocks[SYNCHRONIZERS];

//
// The acquirer/releaser thread.
//

static
ULONG
WINAPI
AcquirerReleaserForFairLock (
    __in PVOID Arg
    )
{
    ULONG Id = (ULONG)Arg;
    StWaitable *WaitableLocks[ACQUIRE_COUNT];
    ULONG WaitStatus;
    ULONG Timeouts = 0;

    printf("+++a/r #%d starts...\n", Id);

    srand(((ULONG)&Id) >> 12);
    do {
        LONG Start = rand() % SYNCHRONIZERS;
        for (int i = 0; i < ACQUIRE_COUNT; i++) {
            WaitableLocks[i] = &FairLocks[Start];
            if (++Start >= SYNCHRONIZERS) {
                Start = 0;
            }
        }
        do {
            WaitStatus = StWaitable::WaitAll(ACQUIRE_COUNT, WaitableLocks, 20, NULL);
            if (WaitStatus == WAIT_SUCCESS) {
                break;
            }
            Timeouts++;
        } while (true);

        if ((++Counts[Id] % 1000) == 0) {
            printf("-%d", Id);
        }

        //
        // Release the fair locks.
        //

        for (int i = 0; i < ACQUIRE_COUNT; i++) {
            WaitableLocks[i]->Signal();
        }
    } while (!Shutdown.IsSet());
    printf("+++a/r #%d exiting: [%d/%d]\n", Id, Counts[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The test function for reentrant fair locks.
//

VOID
RunWaitAllForFairLockTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < THREADS; i++) {
        HANDLE AcqRel = CreateThread(NULL, 0, AcquirerReleaserForFairLock, (PVOID)i, 0, NULL);
        CloseHandle(AcqRel);
    }
    getchar();
    Shutdown.Set();
    Done.Wait();
}

/*++
 *
 * Read Write Lock.
 *
 --*/

//
// The read write locks.
//

static StReadWriteLock RWLocks[SYNCHRONIZERS];

//
// The acquirer/releaser thread for read write lock.
//

static
ULONG
WINAPI
AcquirerReleaserForReadWriteLock (
    __in PVOID Arg
    )
{
    ULONG Id = (ULONG)Arg;
    StWaitable *WaitableLocks[ACQUIRE_COUNT];
    ULONG WaitStatus;
    ULONG Timeouts = 0;

    printf("+++a/r #%d starts...\n", Id);
    srand(((ULONG)&Id) >> 12);
    do {
        LONG Start = rand() % SYNCHRONIZERS;
        for (int i = 0; i < ACQUIRE_COUNT; i++) {
            WaitableLocks[i] = &RWLocks[Start];
            if (++Start >= SYNCHRONIZERS) {
                Start = 0;
            }
        }
        do {
            WaitStatus = StWaitable::WaitAll(ACQUIRE_COUNT, WaitableLocks, 20, NULL);
            if (WaitStatus == WAIT_SUCCESS) {
                break;
            }
            Timeouts++;
        } while (true);
        if ((++Counts[Id] % 1000) == 0) {
            printf("-%d", Id);
        }

        //
        // Release the read write locks.
        //

        for (int i = 0; i < ACQUIRE_COUNT; i++) {
            WaitableLocks[i]->Signal();
        }
    } while (!Shutdown.IsSet());
    printf("+++a/r #%d exiting: [%d/%d]...\n", Id, Counts[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The test function for read write lock.
//

VOID
RunWaitAllForReadWriteLockTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < THREADS; i++) {
        HANDLE AcqRel = CreateThread(NULL, 0, AcquirerReleaserForReadWriteLock, (PVOID)i, 0, NULL);
        CloseHandle(AcqRel);
    }
    getchar();
    Shutdown.Set();
    Done.Wait();
}

/*++
 *
 * Synchronization Event.
 *
 --*/

//
// The synchronization events.
//

static StSynchronizationEvent Events[SYNCHRONIZERS];

//
// The acquirer/releaser thread for synchronization event.
//

static
ULONG
WINAPI
AcquirerReleaserForSynchronizationEvent (
    __in PVOID Arg
    )
{
    ULONG Id = (ULONG)Arg;
    StWaitable *Waitables[ACQUIRE_COUNT];
    ULONG WaitStatus;
    ULONG Timeouts = 0;

    printf("+++a/r #%d starts...\n", Id);

    srand(((ULONG)&Id) >> 12);
    do {
        LONG Start = rand() % SYNCHRONIZERS;
        for (int i = 0; i < ACQUIRE_COUNT; i++) {
            Waitables[i] = &Events[Start];
            if (++Start >= SYNCHRONIZERS) {
                Start = 0;
            }
        }
        do {
            WaitStatus = StWaitable::WaitAll(ACQUIRE_COUNT, Waitables, 20, NULL);
            if (WaitStatus == WAIT_OBJECT_0) {
                break;
            }
            Timeouts++;
        } while (true);

        if ((++Counts[Id] % 1000) == 0) {
            printf("-%d", Id);
        }

        //
        // Set the events.
        //

        for (int i = 0; i < ACQUIRE_COUNT; i++) {
            Waitables[i]->Signal();
        }
    } while (!Shutdown.IsSet());
    printf("+++a/r #%d exiting: [%d/%d]\n", Id, Counts[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The test function for synchronization events.
//

VOID
RunWaitAllForSynchronizationEventTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < SYNCHRONIZERS; i++) {
        Events[i].Init(true);
    }
    for (int i = 0; i < THREADS; i++) {
        HANDLE AcqRel = CreateThread(NULL, 0, AcquirerReleaserForSynchronizationEvent,
                                     (PVOID)i, 0, NULL);
        CloseHandle(AcqRel);
    }
    getchar();
    Shutdown.Set();
    Done.Wait();
}

/*++
 *
 * Semaphore.
 *
 --*/

//
// The semaphores.
//

static StSemaphore Semaphores[SYNCHRONIZERS];

//
// The acquirer/releaser thread for semaphore.
//

static
ULONG
WINAPI
AcquirerReleaserForSemaphore (
    __in PVOID Arg
    )
{
    ULONG Id = (ULONG)Arg;
    StWaitable *Waitables[ACQUIRE_COUNT];
    ULONG WaitStatus;
    ULONG Timeouts = 0;

    printf("+++ a/r #%d starts...\n", Id);
    srand(((ULONG)&Id) >> 12);
    do {
        LONG Start = rand() % SYNCHRONIZERS;
        for (int i = 0; i < ACQUIRE_COUNT; i++) {
            Waitables[i] = &Semaphores[Start];
            if (++Start >= SYNCHRONIZERS) {
                Start = 0;
            }
        }
        do {
            WaitStatus = StWaitable::WaitAll(ACQUIRE_COUNT, Waitables, 20, NULL);
            if (WaitStatus == WAIT_SUCCESS) {
                break;
            }
            Timeouts++;
        } while (true);
        if ((++Counts[Id] % 1000) == 0) {
            printf("-%d", Id);
        }

        //
        // Release the semaphore permit acquire above.
        //

        for (int i = 0; i < ACQUIRE_COUNT; i++) {
            Waitables[i]->Signal();
        }
    } while (!Shutdown.IsSet());
    printf("+++ a/r #%d exiting: [%d/%d]\n", Id, Counts[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The test function for semaphore.
//

VOID
RunWaitAllForSemaphoreTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < SYNCHRONIZERS; i++) {
        Semaphores[i].Init(1, 1);
    }
    for (int i = 0; i < THREADS; i++) {
        HANDLE AcqRel = CreateThread(NULL, 0, AcquirerReleaserForSemaphore, (PVOID)i, 0, NULL);
        CloseHandle(AcqRel);
    }
    getchar();
    Shutdown.Set();
    Done.Wait();
}

/*++
 *
 * Notification Event.
 *
 --*/

//
// The notification events.
//

static StNotificationEvent NEvents[SYNCHRONIZERS];

#define SETTERS		1

//
// The event setter thread.
//

static
ULONG
WINAPI
EventSetter (
    __in PVOID Arg
    )
{

    ULONG Id = (ULONG)Arg;

    printf("+++ setter #%d starts...\n", Id);
    srand(((ULONG)&Id) >> 12);
    do {
        LONG Start = rand() % SYNCHRONIZERS;
        for (int i = 0; i < ACQUIRE_COUNT; i++) {
            NEvents[Start].Set();
            if (++Start >= SYNCHRONIZERS) {
                Start = 0;
            }
        }
        SwitchToThread();
    } while (!Shutdown.IsSet());
    printf("+++setter #%d exiting ...\n", Id);
    Done.Signal();
    return 0;
}

//
// The wait/reset thread for notification event.
//

static
ULONG
WINAPI
EventWaitReset (
    __in PVOID Arg
    )
{
    ULONG Id = (ULONG)Arg;
    StWaitable *Waitables[ACQUIRE_COUNT];
    ULONG WaitStatus;
    ULONG Timeouts = 0;

    printf("+++w/r #%d starts...\n", Id);
    srand(((ULONG)&Id) >> 12);
    do {
        LONG Start = rand() % SYNCHRONIZERS;
        for (int i = 0; i < ACQUIRE_COUNT; i++) {
            Waitables[i] = &NEvents[Start];
            if (++Start >= SYNCHRONIZERS) {
                Start = 0;
            }
        }
        do {
            WaitStatus = StWaitable::WaitAll(ACQUIRE_COUNT, Waitables, 20, &Shutdown);
            if (WaitStatus == WAIT_SUCCESS) {
                break;
            }
            if (WaitStatus == WAIT_ALERTED) {
                goto Exit;
            }
            Timeouts++;
        } while (true);
        if ((++Counts[Id] % 100) == 0) {
            printf("-%d", Id);
        }

        //
        // Reset the events.
        //

        for (int i = 0; i < ACQUIRE_COUNT; i++) {
            ((StNotificationEvent *)Waitables[i])->Reset();
        }
    } while (!Shutdown.IsSet());
Exit:
    printf("+++w/r #%d exiting: [%d/%d]\n", Id, Counts[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The test function for notification events.
//

VOID
RunWaitAllForNotificationEventTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);
    Done.TryAdd(SETTERS);

    for (int i = 0; i < THREADS; i++) {
        HANDLE Thread = CreateThread(NULL, 0, EventWaitReset, (PVOID)i, 0, NULL);
        CloseHandle(Thread);
    }
    for (int i = 0; i < SETTERS; i++) {
        HANDLE Thread = CreateThread(NULL, 0, EventSetter, (PVOID)i, 0, NULL);
        CloseHandle(Thread);
    }
    getchar();
    Shutdown.Set();
    Done.Wait();
}
