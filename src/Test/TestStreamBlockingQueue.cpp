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


#define PRODUCERS	10
#define CONSUMERS	10

//
// The stream blocking queue.
//

static StStreamBlockingQueue Queue(4 * 1024);

//
// The alerter and the count down event used to synchronize shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(CONSUMERS + PRODUCERS);

//
// The counters
//

static ULONG Productions[PRODUCERS];
static ULONG Consumptions[CONSUMERS];

//
// The producer thread.
//

static
ULONG
WINAPI
ProducerThread (
    __in PVOID arg
    )
{

    LONG Id = (LONG)arg;
    CHAR Message[256];
    ULONG Written;
    ULONG MessageLength;
    ULONG Timeouts = 0;
    ULONG Count = 0;

    printf("+++ producer #%d started...\n", Id);
    do {
        sprintf_s(Message, sizeof(Message), "-message %d from producer #%d thread\n", ++Count, Id);
        MessageLength = (ULONG)strlen(Message);
        ULONG WaitStatus = Queue.Write(Message, MessageLength, &Written, 1, &Shutdown );
        Productions[Id] += Written;
        if (WaitStatus == WAIT_ALERTED || WaitStatus == WAIT_DISPOSED) {
            break;
        } else if (WaitStatus == WAIT_TIMEOUT) {
            Timeouts++;
        }
        //Sleep(1);
    } while (true);
    printf("+++ producer #%d exits: [%d/%d]\n", Id, Productions[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The dispose callback.
//

static
VOID
CALLBACK
DisposeCallback (
    __in PUCHAR Buffer,
    __in ULONG Count,
    __in PVOID Context
    )
{
    *((PULONG)Context) += Count;
}

//
// The consumer thread.
//

static
ULONG
WINAPI
ConsumerThread (
    __in PVOID Arg
    )
{
    LONG Id = (LONG)Arg;
    ULONG Timeouts = 0;
    CHAR Buffer[10];
    ULONG Read;
    ULONG WaitStatus;

    printf("+++ consumer #%d started...\n", Id);
    do {
        ZeroMemory(Buffer, sizeof(Buffer));
        WaitStatus = Queue.Read(&Buffer, sizeof(Buffer) - 1, &Read, 1, &Shutdown);
        Consumptions[Id] += Read;
        /*
        if (Read != 0) {
            Buffer[Read] = '\0';
            printf("%s", Buffer);
        }
        */
        if (WaitStatus == WAIT_ALERTED) {
            Sleep(10);
            Queue.Dispose(DisposeCallback, &Consumptions[Id]);
            break;
        } else if (WaitStatus == WAIT_TIMEOUT) {
            Timeouts++;
        } else if (WaitStatus == WAIT_DISPOSED) {
            break;
        }
    } while (true);
    printf("+++ Consumer #%d exits: [%d/%d]\n", Id, Consumptions[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The test function.
//

VOID
RunStreamBlockingQueueTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);
    for (int i = 0; i < CONSUMERS; i++) {
        HANDLE Consumer = CreateThread(NULL, 0, ConsumerThread, (PVOID)(i), 0, NULL);
        CloseHandle(Consumer);
    }
    for (int i = 0; i < PRODUCERS; i++) {
        HANDLE Producer = CreateThread(NULL, 0, ProducerThread, (PVOID)(i), 0, NULL);
        CloseHandle(Producer);
    }

    printf("+++ hit <enter> to terminate the test...\n");
    getchar();
    Shutdown.Set();
    Done.Wait();
    
    ULONGLONG TotalProds = 0;
    for (int i = 0; i < PRODUCERS; i++) {
        TotalProds += Productions[i];
    }
    ULONGLONG TotalCons = 0;
    for (int i = 0; i < CONSUMERS; i++) {
        TotalCons += Consumptions[i];
    }
    printf("+++Total: productions = %I64d, consumptions = %I64d\n", TotalProds, TotalCons);
}
