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
// The number of initializer threads.
//

#define INITIALIZERS	20

//
// The global target and the init once lock.
//

static PVOID Target;
static ST_INIT_ONCE_LOCK InitLock;

//
// The count down event that syncrhonizes the shutdown.
//

static ST_COUNT_DOWN_EVENT Done;

//
// The target's contructor function.
//

static
PVOID
CALLBACK
TargetCtor (
    __in PVOID Context
    )
{
    HANDLE Semaphore;
    ULONG Id = (ULONG)Context;
    ULONG CreationTime;

    printf("+++ thread %d: called .ctor\n", Id);

    CreationTime = rand() % 100;
    if (CreationTime >= 25) {
        return NULL;
    }
    Semaphore = CreateSemaphore(NULL, INITIALIZERS, INITIALIZERS, NULL);
    if (Semaphore == NULL) {
        return FALSE;
    }
    Sleep(CreationTime + 500);
    return Semaphore;
}

//
// The target's destructor function.
//

static
VOID
CALLBACK
TargetDtor (
    __in PVOID Semaphore,
    __in PVOID Context
    )
{
    printf("*** thread %d: called .DTOR\n", (LONG)Context);
    CloseHandle((HANDLE)Semaphore);
}

//
// The asynchronous initializer thread.
//

static
ULONG
WINAPI
AsyncInitializerThread (
    __in PVOID arg
    )
{

    LONG Id = (LONG)arg;
    HANDLE Semaphore;

    printf("+++ i #%d started...\n", Id);
    srand(GetTickCount() * Id);
    Sleep(rand() % 100);
    do {
        Semaphore = (HANDLE)StEnsureInitialized_Asynchronous(&Target, TargetCtor, TargetDtor, (PVOID)Id);
        if (Semaphore == NULL) {
            printf("*** i #%d failed\n", Id);
            continue;
        }
        WaitForSingleObject(Semaphore, INFINITE);
        break;
    } while (TRUE);
    printf("+++ i #%d exits\n", Id);
    StCountDownEvent_Signal(&Done, 1);
    return 0;
}

//
// Test asynchronous initialization.
//

VOID
RunEnsureInitializedAsynchronousTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);
    do {
        printf("\n\n---> START INITIALIZATION\n\n");
        Target = NULL;
        StCountDownEvent_Init(&Done, INITIALIZERS, 0);
        for (int i = 0; i < INITIALIZERS; i++) {
            CreateThread(NULL, 0, AsyncInitializerThread, (PVOID)(i), 0, NULL);
        }
        StCountDownEvent_Wait(&Done);
        CloseHandle(Target);
    } while (!_kbhit());
    getchar();
}

//
// Test Ensure Initialized synchronous
//

//
// The synchronous initializer thread.
//

static
ULONG
WINAPI
SyncInitializerThread (
    __in PVOID arg
    )
{
    LONG Id = (LONG)arg;
    HANDLE Semaphore;

    printf("+++ i #%d started...\n", Id);
    srand(GetTickCount() * Id);
    Sleep(rand() % 100);
    do {
        Semaphore = (HANDLE)StEnsureInitialized_Synchronous(&Target, &InitLock, TargetCtor,
                                                            (PVOID)Id, 0);
        if (Semaphore == NULL) {
            printf("*** i #%d: FAILED\n", Id);
            continue;
        }
        WaitForSingleObject(Semaphore, INFINITE);
        break;
    } while (TRUE);
    printf("+++ i #%d exits\n", Id);
    StCountDownEvent_Signal(&Done, 1);
    return 0;
}

//
// Test synchronous initialization.
//

VOID
RunEnsureInitializedSynchronousTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);
    do {
        printf("----> START INITIALIZATION\n");

        StInitOnceLock_Init(&InitLock);
        Target = NULL;
        StCountDownEvent_Init(&Done, INITIALIZERS, 0);
        for (int i = 0; i < INITIALIZERS; i++) {
            CreateThread(NULL, 0, SyncInitializerThread, (PVOID)(i), 0, NULL);
        }
        StCountDownEvent_Wait(&Done);
        CloseHandle(Target);
    } while (!_kbhit());
    getchar();
}
