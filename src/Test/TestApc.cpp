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

#define APC_THREADS	100

//
// Alerter and count down event to control shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(APC_THREADS);

//
// ...
//

struct APC_ARGS {
    HANDLE ThreadHandle;
    StSemaphore *Semaphore;

    APC_ARGS(HANDLE t, StSemaphore *s) {
        ThreadHandle = t;
        Semaphore = s;
    }
};

//
// The APC routine
//

static
VOID
CALLBACK
UserApcHandler (
    __inout ULONG_PTR context
    )
{
    StSemaphore *s = (StSemaphore *)context;
    printf("***APC handler: sleep for 2 seconds\n");
    Sleep(2000);
    printf("***APC handler: sends a permit to the semaphore\n");
    s->Release();
    printf("***APC handler: exit\n");
    return;
}

//
// Runs on a thread pool thread and queues an user APC to
// the specified thread.
//

static
ULONG
WINAPI
QueueUserApcThread (
    __in PVOID arg
    )
{
    APC_ARGS *args = (APC_ARGS *)arg;

    printf("+++ Queue user APC thread: started, queues a user APC for %d\n", args->ThreadHandle);
    if (QueueUserAPC(UserApcHandler, args->ThreadHandle, (ULONG_PTR)args->Semaphore) == 0) {		
        printf("***QueueUserApcFailed, error: %d\n", GetLastError());
    }
    delete args;
    printf("+++ Queue user APC thread: exits\n");
    return 0;
}

//
// The thread that processes the APC.
//

static
ULONG
WINAPI
ApcThread (
    __in PVOID arg
    )
{

    int n = (int)arg;
    HANDLE thisHandle;
    StSemaphore s(0, 1);

    printf("+++ APC thread %d started...\n", n);
    
    BOOL r = DuplicateHandle( GetCurrentProcess(),
                              GetCurrentThread(),
                              GetCurrentProcess(),
                              &thisHandle,
                              THREAD_ALL_ACCESS,
                              FALSE,
                              0 );

    if (r) {
        printf("+++APC thread handle is: %d\n", thisHandle);
    } else {
        printf("***DuplicateHandle failed, error: %d\n", GetLastError());
        return 0;
    }

    do {
        APC_ARGS *args = new APC_ARGS(thisHandle, &s);
        QueueUserWorkItem(QueueUserApcThread, (PVOID)args, 0);
        printf("+++ thread created, wait on the semaphore\n");
        s.Acquire();
        printf("+++ awakened on private semaphore\n");
    } while (!Shutdown.IsSet());
    printf("--- APC thread #%d exiting\n", n);
    Done.Signal();
    return 0;
}

VOID
RunApcTest (
    )
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < APC_THREADS; i++) {
        CreateThread(NULL, 0, ApcThread, (PVOID)(i), 0, NULL);
    }

    getchar();
    Shutdown.Set();
    Done.Wait();
}
