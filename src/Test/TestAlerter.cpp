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

#define THREADS	10

//
// The alerter.
//

static StAlerter Alerter;

//
// The count down event that synchronizes termination.
//

static StCountDownEvent Done(THREADS);

//
// The thread to be alerted.
//

static
ULONG
WINAPI
AlertedThread (
    __in_opt PVOID Argument
    )
{
    ULONG Id = (ULONG)Argument;
    printf("+++ alerted #%d started...\n", Id);
    ULONG Count = 0;
    do {
        ULONG Start = GetTickCount();
        //if (Sleep(45), WAIT_TIMEOUT) {
        if (StParker::Sleep(45, &Alerter) == WAIT_TIMEOUT) {
            ULONG Elapsed = (GetTickCount() - Start);
            if (!(Elapsed >= 45 && Elapsed <= 48)) {
                printf("[%d]", Elapsed);
            }
            Count++;
            
            if ((Count % 100) == 0) {
                printf("-%d", Id);
            }
            
        } else {
            printf("***alerted #%d alerted\n", Id);
            break;
        }
    } while (Count < (5000 + 500 * Id));
    printf("+++ alerted #%d exiting, after %d timeouts\n", Id, Count);
    Done.Signal();
    return 0;
}

VOID
RunAlerterTest (
    )
{

    HANDLE als[THREADS];

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);	
    //SetProcessAffinityMask(GetCurrentProcess(), (1 << 0));

    for (int i = 0; i < THREADS; i++) {
        //als[i] = CreateThread(NULL, 0, AlertedThread, (PVOID)i, 0, NULL);
        als[i] = StUmsThread_Create(NULL, 0, AlertedThread, (PVOID)i, 0, NULL);
    }
    getchar();
    Alerter.Set();
    Done.Wait();
}
