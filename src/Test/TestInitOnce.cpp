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
// The init once synchronizer.
//

static StInitOnce InitOnce;

//
// ...
//

typedef struct _THREAD_ARGS {
    ULONG ThreadNumber;
    StCountDownEvent *Done;
} THREAD_ARGS, *PTHREAD_ARGS;

//
// The asynchronous initializer thread
//

static
ULONG
WINAPI
AsyncInitializerThread (
    __in PVOID arg
    )
{
    PTHREAD_ARGS Args = (PTHREAD_ARGS)arg;
    LONG Id = Args->ThreadNumber;
    HANDLE Semaphore;
    BOOL Result;
    BOOL Pending;
    PVOID Context;

    printf("+++ thread #%d started...\n", Id);
    srand(GetTickCount() * Id);
    Sleep(rand() % 100);
    do {

        Result = InitOnce.BeginInit(INIT_ONCE_ASYNC, &Pending, &Context);
        if (!Result) {
            printf("*** thread #%d failed\n", Id);
            continue;
        }
        if (!Pending) {
            Semaphore = (HANDLE)Context;
            WaitForSingleObject(Semaphore, INFINITE);
            break;
        }

        //
        // The current thread must create the semaphore.
        //

        if ((rand() % 2) != 0) {
            
            printf("*** thread %d: FAILED\n", Id);
            Sleep(rand() % 50);
            continue;
        }

        Semaphore = CreateSemaphore(NULL, INITIALIZERS, INITIALIZERS, NULL);
        if (Semaphore == NULL) {
            printf("*** thread %d: FAILED\n", Id);
            Sleep(rand() % 20);
            continue;
        }
        printf("+++ thread %d, creates the semaphore\n", Id);
        if (InitOnce.Complete(INIT_ONCE_ASYNC, (PVOID)Semaphore)) {
            WaitForSingleObject(Semaphore, INFINITE);
            break;
        } else {
            CloseHandle(Semaphore);
        }
    } while (true);
    printf("+++ thread #%d exits\n", Id);
    Args->Done->Signal();
    free(Args);
    return 0;
}

VOID
RunInitOnceAsynchronousTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    do {
        printf("----> START INITIALIZATION CYCLE\n");
        StCountDownEvent Done(INITIALIZERS);
        InitOnce.Init();
        for (int i = 0; i < INITIALIZERS; i++) {
            PTHREAD_ARGS Args = (PTHREAD_ARGS)malloc(sizeof(THREAD_ARGS));
            Args->ThreadNumber = i;
            Args->Done = &Done;
            HANDLE AsyncInit = CreateThread(NULL, 0, AsyncInitializerThread, Args, 0, NULL);
            CloseHandle(AsyncInit);
        }

        //
        // Wait until completion.
        //

        Done.Wait();
        CloseHandle(InitOnce.GetTarget());
    } while (!_kbhit());
    getchar();
}

//
// Test InitOnce synchronous
//

//
// Contructor function.
//

static
BOOL
CALLBACK
SyncCtor (
    __inout PST_INIT_ONCE Unused,
    __in PVOID Parameter,
    __out PVOID *Context
    )
{
    HANDLE Semaphore;
    ULONG Id = (ULONG)Parameter;
    ULONG CreationTime;

    printf("+++ thread %d: called constructor\n", Id);

    CreationTime = rand() % 250;
    if (CreationTime >= 10) {
        return FALSE;
    }
    Semaphore = CreateSemaphore(NULL, INITIALIZERS, INITIALIZERS, NULL);
    if (Semaphore == NULL) {
        return FALSE;
    }
    Sleep(CreationTime);
    *Context = (PVOID)Semaphore;
    return TRUE;
}

//
// Initializer thread
//

static
ULONG
WINAPI
SyncInitializerThread (
    __in PVOID Arg
    )
{

    PTHREAD_ARGS Args = (PTHREAD_ARGS)Arg;
    LONG Id = Args->ThreadNumber;
    HANDLE Semaphore;

    printf("+++ thread #%d started...\n", Id);
    srand(GetTickCount() * Id);
    Sleep(rand() % 100);
    do {
        if (InitOnce.ExecuteOnce(SyncCtor, (PVOID)Id, (PVOID *)&Semaphore)) {
        
            //
            // Wait on the lazy created semaphore.
            //

            WaitForSingleObject(Semaphore, INFINITE);
            break;
        } else {
            printf("****** thread #%d: FAILED\n", Id);
        }
    } while (true);
    printf("+++ thread #%d exits\n", Id);
    Args->Done->Signal();
    return 0;
}

//
// Test init once synchronous.
//

VOID
RunInitOnceSynchronousTest (
    )
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    do {

        printf("---> START INITIALIZATION CYCLE\n");
        StCountDownEvent Done(INITIALIZERS);
        InitOnce.Init();

        for (int i = 0; i < INITIALIZERS; i++) {
            PTHREAD_ARGS Args = (PTHREAD_ARGS)malloc(sizeof(THREAD_ARGS));
            Args->ThreadNumber = i;
            Args->Done = &Done;
            HANDLE SyncInit = CreateThread(NULL, 0, SyncInitializerThread, Args, 0, NULL);
            CloseHandle(SyncInit);
        }
        Done.Wait();
        CloseHandle((HANDLE)InitOnce.GetTarget());
    } while (!_kbhit());
    getchar();
}

//
// Test InitOnce inline Synchronous
//

//
// Initializer thread
//

static
ULONG
WINAPI
InlineSyncInitializerThread (
    __in PVOID Arg
    )
{
    PTHREAD_ARGS Args = (PTHREAD_ARGS)Arg;
    LONG Id = Args->ThreadNumber;
    HANDLE Semaphore;
    BOOL Result;
    BOOL Pending;
    PVOID Context;
    ULONG CreationTime;

    printf("+++ thread #%d started...\n", Id);
    srand(GetTickCount() * Id);
    Sleep(rand() % 100);
    do {

        Result = InitOnce.BeginInit(0, &Pending, &Context);
        if (!Result) {
            printf("*** thread #%d: FAILED\n", Id);
            continue;
        }
        if (!Pending) {
            Semaphore = (HANDLE)Context;
            WaitForSingleObject(Semaphore, INFINITE);
            break;
        }

        //
        // The current thread must create the semaphore.
        //

        CreationTime = rand() % 250;
        if (CreationTime >= 10) {
            
            printf("*** thread %d, fails while creating the semaphore\n", Id);
            InitOnce.Complete(INIT_ONCE_INIT_FAILED, NULL);
            Sleep(250);
            continue;
        }
        Semaphore = CreateSemaphore(NULL, INITIALIZERS, INITIALIZERS, NULL);
        if (Semaphore == NULL) {
            printf("*** Thread %d: FAILED\n", Id);
            InitOnce.Complete(INIT_ONCE_INIT_FAILED, NULL);
            Sleep(250);
            continue;
        }
        Sleep(CreationTime);
        printf("+++ initializer thread %d, creates the semaphore\n", Id);
        InitOnce.Complete(0, (PVOID)Semaphore);
        WaitForSingleObject(Semaphore, INFINITE);
        break;
    } while (true);
    printf("+++ initializer thread #%d exits\n", Id);
    Args->Done->Signal();
    return 0;
}

//
// The test main function.
//

VOID
RunInitOnceInlineSynchronousTest (
    )
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    do {
        printf("---> START INITIALIZATION CYCKE\n");
        InitOnce.Init();
        StCountDownEvent Done(INITIALIZERS);
        for (int i = 0; i < INITIALIZERS; i++) {
            PTHREAD_ARGS Args = (PTHREAD_ARGS)malloc(sizeof(THREAD_ARGS));
            Args->ThreadNumber = i;
            Args->Done = &Done;
            HANDLE InlineInit = CreateThread(NULL, 0, InlineSyncInitializerThread, Args, 0, NULL);
            CloseHandle(InlineInit);
        }
        Done.Wait();
        CloseHandle((HANDLE)InitOnce.GetTarget());
    } while (!_kbhit());
    getchar();
}

