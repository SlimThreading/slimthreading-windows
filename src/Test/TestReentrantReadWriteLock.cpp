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
// The number of readers and writers.
//

#define READERS		10
#define WRITERS		5

//
// The number of recursive acquisitions.
//

#define RECURSE	5

//
// The reentrant read write lock.
//

static StReentrantReadWriteLock RRWLock;

//
// The alerter and the count down event used to synchronize the shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(READERS + WRITERS);

//
// The counters
//

static int Reads[READERS + 1];
static int Writes[WRITERS + 1];

//
// The reader thread
//

static
ULONG
WINAPI
Reader (
    __in PVOID arg
    )
{
    int Id = (int)arg;
    int Timeouts = 0;
    srand(((ULONG)&Id) >> 12);

    printf("+++ rd #%d started...\n", Id);
    do {
        //if (RRWLock.EnterRead(), TRUE) {
        if (RRWLock.TryEnterRead(rand() % 50) == WAIT_SUCCESS) {
            for (int i = 1; i < RECURSE; i++) {
                RRWLock.EnterRead();
            }
            if ((++Reads[Id] % 10000) == 0) {
                printf("-r%d", Id);
            }
            for (int i = 0; i < RECURSE; i++) {
                RRWLock.ExitRead();
            }
        } else {
            Timeouts++;
        }
    } while (!Shutdown.IsSet());
    printf("+++ rd #%d exiting: [%d/%d]\n", Id, Reads[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The writer thread
//

static
ULONG
WINAPI
Writer (
    __in PVOID arg
    )
{
    int Id = (int)arg;
    int Timeouts = 0;
    StWaitable *WaitableLocks[] = { &RRWLock };

    printf("+++ wr #%d started...\n", Id);
    do {
        //if (RRWLock.EnterWrite(), TRUE) {
        if (RRWLock.TryEnterWrite(rand() % 50) == WAIT_SUCCESS) {
        //if (RRWLock.WaitOne(rand() % 50) == WAIT_SUCCESS) {
        //if (StWaitable::WaitAny(1, WaitableLocks, rand() % 50) == WAIT_OBJECT_0) {
        //if (StWaitable::WaitAll(1, WaitableLocks, rand() % 50, NULL) == WAIT_OBJECT_0) {
            for (int i = 1; i < RECURSE; i++) {
                RRWLock.EnterWrite();
            }
            if ((++Writes[Id] % 10000) == 0) {
                printf("-w%d", Id);
            }
            for (int i = 0; i < RECURSE; i++) {
                RRWLock.ExitWrite();
            }
        } else {
            Timeouts++;
        }
    } while (!Shutdown.IsSet());
    printf("+++ writer #%d exiting, [%d/%d]\n", Id, Writes[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// Test the reentrant read write lock.
//

VOID
RunReentrantReadWriteLockTest (
    )
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    RRWLock.EnterWrite();

    for (int i = 0; i < READERS; i++) {
        HANDLE Thread = CreateThread(NULL, 0, Reader, (PVOID)i, 0, NULL);
        CloseHandle(Thread);
    }

    for (int i = 0; i < WRITERS; i++) {
        HANDLE Thread = CreateThread(NULL, 0, Writer, (PVOID)i, 0, NULL);
        CloseHandle(Thread);
    }
    RRWLock.ExitWrite();
    getchar();
    Shutdown.Set();
    Done.Wait();

    ULONGLONG TotalReads = 0;
    for (int i = 0; i < READERS; i++) {
        TotalReads += Reads[i];
    }
    ULONGLONG TotalWrites = 0;
    for (int i = 0; i < WRITERS; i++) {
        TotalWrites += Writes[i];
    }
    printf("+++Total: reads = %I64d, writes = %I64d\n", TotalReads, TotalWrites);
}

//
// An array of reentrant read write locks.
//

#define LOCK_COUNT	1024

static StReentrantReadWriteLock RRWLocks[LOCK_COUNT];

//
// The multiple reader thread
//

static
ULONG
WINAPI
MultipleReader (
    __in PVOID Arg
    )
{
    ULONG Id = (ULONG)Arg;

    printf("+++ mrd #%d started...\n", Id);

    srand(((ULONG)&Id) >> 12);
    do {
        int first = rand() % (LOCK_COUNT - 1);
        int i;
        for (i = first; i < LOCK_COUNT; i++) {
            for (int j = 0; j < 10; j++) {
                RRWLocks[i].EnterRead();
            }
        }
        if ((++Reads[Id] % 100) == 0) {
            printf("-r%d", Id);
        }
        i = LOCK_COUNT - 1;
        do {
            for (int j = 0; j < 10; j++) {
                RRWLocks[i].ExitRead();
            }
            if (i == first) {
                break;
            }
            i--;
        } while (true);
    } while (!Shutdown.IsSet());
    printf("+++ mrd #%d exiting: [%d]\n", Id, Reads[Id]);
    Done.Signal();
    return 0;
}

//
// The multiple writer thread
//

static
ULONG
WINAPI
MultipleWriter (
    __in PVOID Arg
    )
{
    int Id = (int)Arg;

    printf("+++ mwr #%d started...\n", Id);
    srand(((ULONG)&Id) >> 12);
    do {
        int first = rand() % (LOCK_COUNT - 1);
        int i;
        for (i = first; i < LOCK_COUNT; i++) {
            for (int j = 0; j < 10; j++) {
                RRWLocks[i].EnterWrite();
            }
        }
        if ((++Writes[Id] % 100) == 0) {
            printf("-w%d", Id);
        }
        i = LOCK_COUNT - 1;
        do {
            for (int j = 0; j < 10; j++) {
                RRWLocks[i].ExitWrite();
            }
            if (i == first) {
                break;
            }
            i--;
        } while (true);
    } while (!Shutdown.IsSet());
    printf("+++ mwr #%d exiting: [%d]\n", Id, Writes[Id]);
    Done.Signal();
    return 0;
}

//
// The multiple reader, multiple writer test.
//

VOID
RunMultipleReentrantReadWriteLockTest (
    )
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < READERS; i++) {
        HANDLE Thread = CreateThread(NULL, 0, MultipleReader, (PVOID)i, 0, NULL);
        CloseHandle(Thread);
    }
    for (int i = 0; i < WRITERS; i++) {
        HANDLE Thread = CreateThread(NULL, 0, MultipleWriter, (PVOID)i, 0, NULL);
        CloseHandle(Thread);
    }
    getchar();
    Shutdown.Set();
    Done.Wait();

    ULONGLONG TotalReads = 0;
    for (int i = 0; i < READERS; i++) {
        TotalReads += Reads[i];
    }
    LONGLONG TotalWrites = 0;
    for (int i = 0; i < WRITERS; i++) {
        TotalWrites += Writes[i];
    }
    printf("+++Total: reads = %I64d, writes: %I64d\n", TotalReads, TotalWrites);
}



