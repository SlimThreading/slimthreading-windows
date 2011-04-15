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
// The number of waiter threads.
//

#define WAITERS		10

//
// The notification event.
//

static StNotificationEvent Event;

//
// The alerter and the count down event used to synchronize the shutdown.
//

static StAlerter Shutdown;
static StNotificationEvent Done;

//
// The count down event that synchronizes completion.
//

static StCountDownEvent Completed(WAITERS);

//
// The waiter thread
//

ULONG
WINAPI
WaiterThread (
    __in PVOID Argument
    )
{

    ULONG Id = (ULONG)Argument;
    ULONG Timeouts = 0;

    printf("--- w %d started...\n", Id);
    while (Event.Wait(Id + 1) == WAIT_TIMEOUT) {
        Timeouts++;
    }
    printf("--- w #%d exiting: [%d]\n", Id, Timeouts);
    Completed.Signal();
    return 0;
}

//
// The controller thread.
//

ULONG
WINAPI
Controller (
    __in PVOID Argument
    )
{
    ULONG Id = (ULONG)Argument;
    ULONG Count = 0;	
    printf("+++ c #%d started...\n", Id);
    srand(((ULONG)&Id) >> 12);
    do {
        Completed.Init(WAITERS);
        Event.Init();
        for (int i = 0; i < WAITERS; i++) {
            QueueUserWorkItem(WaiterThread, (PVOID)i, WT_EXECUTEDEFAULT);
        }

        //
        // Sleep for a while and set the event.
        //

        StParker::Sleep((rand() % 100) + 10);
        Event.Set();

        //
        // Wait until all worker threads finish its work.
        //

        Completed.Wait();
        Count++;
    } while (!Shutdown.IsSet());
    printf("+++ c #%d exiting: [%d]\n", Id, Count);
    Done.Set();
    return 0;
}

//
// The test function.
//

VOID
RunNotificationEventTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    CreateThread(NULL, 0, Controller, (PVOID)0, 0, NULL);
    getchar();
    Shutdown.Set();
    Done.Wait();
}
