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
// The number of reader and writer threads.
//


#define READERS		10
#define WRITERS		5

//
// The read write lock.
//

static StReadWriteLock  RWLock;

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
    __in PVOID Arg
    )
{
    int Id = (int)Arg;
    int Timeouts = 0;
    int Count = 0;

    printf("+++ rd #%d started...\n", Id);
    srand(((ULONG)&Id) >> 12);
    do {
        //if (RWLock.EnterRead(), TRUE) {
        if (RWLock.TryEnterRead(rand() % 50) == WAIT_SUCCESS) {
            Reads[Id]++;
            if ((++Count % 1000) == 0) {
                printf("-r%d", Id);
            }
            SwitchToThread();
            RWLock.ExitRead();
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
    __in PVOID Arg
    )
{
    int Id = (int)Arg;
    int Timeouts = 0;
    int Count = 0;
    StWaitable *WaitableLocks[] = { &RWLock };

    printf("+++ wr #%d started...\n", Id);
    srand(((ULONG)&Id) >> 12);
    do {
        //if (RWLock.EnterWrite(), TRUE) {
        //if (RWLock.TryEnterWrite(rand() % 50) == WAIT_SUCCESS) {
        //if (RWLock.WaitOne(rand() % 50) == WAIT_SUCCESS) {
        if (StWaitable::WaitAny(1, WaitableLocks, rand() % 50) == WAIT_OBJECT_0) {
        //if (StWaitable::WaitAll(1, WaitableLocks, rand() % 50) == WAIT_SUCCESS) {
            Writes[Id]++;
            if ((++Count % 1000) == 0) {
                printf("-w%d", Id);
            }
            //SwitchToThread();
            RWLock.ExitWrite();
        } else {
            Timeouts++;
        }
    } while (!Shutdown.IsSet());
    printf("+++ wr #%d exiting: [%d/%d]\n", Id, Writes[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The test function.
//

VOID
RunReadWriteLockTest (
    )
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    RWLock.EnterWrite();
    for (int i = 0; i < READERS; i++) {
        HANDLE ThreadHandle = CreateThread(NULL, 0, Reader, (PVOID)i, 0, NULL);
        CloseHandle(ThreadHandle);
    }
    for (int i = 0; i < WRITERS; i++) {
        HANDLE ThreadHandle = CreateThread(NULL, 0, Writer, (PVOID)i, 0, NULL);
        CloseHandle(ThreadHandle);
    }
    //printf("---initialization done!\n");
    RWLock.ExitWrite();
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
    printf("+++reads: %I64d, writes: %I64d\n", TotalReads, TotalWrites);
}
