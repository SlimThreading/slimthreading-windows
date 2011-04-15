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
#include <intrin.h>


#define UMS_WORKERS	2

//
// The alerter and the count down event used for shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(UMS_WORKERS);

static ULONG Counts[UMS_WORKERS];

//
// UMS worker thread
//

HANDLE KernelEvent;
HANDLE KernelMutex;
CRITICAL_SECTION WinCS;

StNotificationEvent StEvent(true);
StFairLock StMutex;
StLock StCS;

ULONG TlsIndex;

static
ULONG
WINAPI
UmsWorkerThread (
    __in PVOID arg
    )
{

    ULONG Id = (LONG)arg;
    PCHAR ThreadType;

#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

    ThreadType = (GetCurrentUmsThread() != NULL) ? "UMS worker" : "Normal";
#else
    ThreadType = "Normal";
#endif

    printf("+++ %s thread #%d started...\n", ThreadType, Id);

    do {


        StThread_Yield();
        ++Counts[Id];
        /*
        if ((Counts[Id] % 100) == 0) {
            printf("-%d", Id);
        }
        */
        
    } while (!Shutdown.IsSet());
    printf("+++ %s thread #%d exits...\n", ThreadType, Id);
    Done.Signal();
    return 0;
}

VOID
RunUmsSchedulerTest (
    )
{
    HANDLE UmsWorkers[UMS_WORKERS];


    SetProcessAffinityMask(GetCurrentProcess(), (1 << 0));


    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    KernelEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
    KernelMutex = CreateMutex(NULL, FALSE, NULL);
    InitializeCriticalSection(&WinCS);

    for (int i = 0; i < UMS_WORKERS; i++) {
        UmsWorkers[i] = StUmsThread_Create(NULL, 0, UmsWorkerThread, (PVOID)(i), 0, NULL);
        //UmsWorkers[i] = CreateThread(NULL, 0, UmsWorkerThread, (PVOID)(i), 0, NULL);
    }

    ULONG Start = GetTickCount();
    getchar();
    ULONG Elapsed = GetTickCount() - Start;
    Shutdown.Set();

    //
    // Wait until all UMS worker threads exit.
    //

    Done.Wait();

    ULONGLONG Total = 0;
    for (int i = 0; i < UMS_WORKERS; i++) {
        Total += Counts[i];
    }

    ULONG CswTime = (ULONG)((Elapsed * 1000000.0)/Total);

    printf("+++ unit cost: %d ns\n", CswTime);
}
