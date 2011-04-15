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
// Define to use a notification event.
//


#define USE_EVENT

//
// The events or semaphores.
//

#ifdef USE_EVENT
static StSynchronizationEvent Events[THREADS];
static HANDLE WinEvents[THREADS];
#else
static StSemaphore Events[THREADS];
#endif

//
// The alerter and the count down event used to synchronize the shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(THREADS);

//
// The counters.
//

static ULONG Counts[THREADS];


//
// The signaler and waiter thread
//

static
ULONG
WINAPI
SignalerAndWaiter (
    __in PVOID Argument
    )
{
    ULONG Id = (ULONG)Argument;
    ULONG WaitStatus;
    ULONG Timeouts = 0;
    LONG Priority;

    printf("+++s/w #%d started...\n", Id);
    switch (Id % 7) {
    case 0:
        Priority = THREAD_PRIORITY_IDLE;
        break;
    case 1:
        Priority = THREAD_PRIORITY_LOWEST;
        break;
    case 2:
        Priority = THREAD_PRIORITY_BELOW_NORMAL;
        break;
    case 3:
        Priority = THREAD_PRIORITY_NORMAL;
        break;
    case 4:
        Priority = THREAD_PRIORITY_ABOVE_NORMAL;
        break;
    case 5:
        Priority = THREAD_PRIORITY_HIGHEST;
        break;
    default:
        Priority = THREAD_PRIORITY_TIME_CRITICAL;
        break;
    }

    StUmsThread_SetCurrentThreadPriority(Priority);
    Events[Id].WaitOne();

    do {
        WaitStatus = StWaitable::SignalAndWait(&Events[(Id + 1) % THREADS], &Events[Id],
                                               51 /*INFINITE*/ , &Shutdown);
        if (WaitStatus == WAIT_ALERTED) {
            break;
        }
        if (WaitStatus == WAIT_TIMEOUT) {
            Timeouts++;
            if (Events[Id].WaitOne(INFINITE, &Shutdown) == WAIT_ALERTED) {
                break;
            }
        }

        Counts[Id]++;
        /*
        if ((Counts[Id] % 10000) == 0) {
            printf("-%d", Id);
        }
        */
        StThread_Yield();
    } while (true);
    printf("+++si/w #%d exists: [%d/%d]\n", Id, Counts[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// ...
//

typedef struct _VICTIM_CTX {
    ULONG Id;
    HANDLE ExitEvent;
} VICTIM_CTX, *PVICTIM_CTX;

ULONG
WINAPI
VictimUmsThread (
    __in PVOID Argument
    )
{
    PVICTIM_CTX Context = (PVICTIM_CTX)Argument;

    printf("\n+++ victim UMS thread #%d starts...\n", Context->Id);
    StUmsThread_SetCurrentThreadPriority(THREAD_PRIORITY_TIME_CRITICAL);
    do {
        if (WaitForSingleObject(Context->ExitEvent, 15) == WAIT_OBJECT_0) {
            break;
        }
        printf("-%d", Context->Id);
        //StThread_Yield();
        
        StParker::Sleep(35);
    } while (true);
    printf("\n+++ victim UMS thread #%d exits...\n", Context->Id);
    return 0;
}

//
// ...
//

ULONG
WINAPI
AgitatorThread (
    __in PVOID Argument
    )
{
    HANDLE UmsThread;
    ULONG Id = (ULONG)Argument;
    VICTIM_CTX VContext;

    printf("\n+++ agitator thread #%d starts...\n", Id);
    VContext.Id = Id;
    srand((ULONG)(((ULONG_PTR)&Id) >> 12));
    do {
        if (StParker::Sleep((rand() % 500) + 100, &Shutdown) == WAIT_ALERTED) {
            break;
        }
        VContext.ExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        UmsThread = StUmsThread_Create(NULL, 0, VictimUmsThread, &VContext, 0, NULL);
        assert(UmsThread != NULL);
        Sleep((rand() % 100) + 15);
        for (int i = 0; i < 20; i++) {
            //putchar('s');
            SuspendThread(UmsThread);
            Sleep((rand() % 100) + 15);
            ResumeThread(UmsThread);
            //putchar('r');
            Sleep((rand() % 100) + 15);
        }
        SetEvent(VContext.ExitEvent);
        WaitForSingleObject(UmsThread, INFINITE);
        CloseHandle(UmsThread);
        CloseHandle(VContext.ExitEvent);
    } while (true);
    printf("\n+++ agitator thread #%d exits...\n", Id);
    Done.Signal();
    return 0;
}


#define AGITATORS	20

//
// The test function.
//

VOID
RunSignalAndWaitTest(
    )
{

    
    //SetProcessAffinityMask(GetCurrentProcess(), (1 << 0));
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < THREADS; i++) {
        HANDLE Thread = StUmsThread_Create(NULL, 0, SignalerAndWaiter, (PVOID)(i), 0, NULL);
        //HANDLE Thread = CreateThread(NULL, 0, SignalerAndWaiter, (PVOID)(i), 0, NULL);
        CloseHandle(Thread);
    }

    for (int i = 0; i < AGITATORS; i++) {
        HANDLE Agitator = CreateThread(NULL, 0, AgitatorThread, (PVOID)i, 0, NULL);
        Done.TryAdd();
        CloseHandle(Agitator);
    }

    ULONG Start = GetTickCount();
    Events[0].Signal();
    getchar();
    ULONG Elapsed = GetTickCount() - Start;
    Shutdown.Set();
    Done.Wait();

    ULONGLONG Total = 0;
    for (int i = 0; i < THREADS; i++) {
        Total += Counts[i];
    }		
    printf("+++unit cost: %d ns\n", (LONG)((Elapsed * 1000000.0)/Total));
}
