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
// Registered wait callback
//

VOID
CALLBACK
RegisteredWaitCallback (
    __inout PVOID Context,
    __in ULONG WaitStatus
    )
{
    static ULONG TickCount = GetTickCount();
    static ULONG Count = 0;

    PST_REGISTERED_WAIT RegWait = (PST_REGISTERED_WAIT)Context; 
    ULONG Now = GetTickCount();
    ULONG Elapsed = Now - TickCount;
    TickCount = Now;

    if (WaitStatus == WAIT_TIMEOUT) {
        printf("+++Wait callback called due to timeout[%d]\n", Elapsed);
    } else {
        printf("+++Wait callback called due to success[%d]\n", ++Count);
    }
}

//
// Unregister thread.
//

struct UCONTEXT {
    StNotificationEvent UnregisterEvent;
    StRegisteredWait *RegisteredWait;
    
    UCONTEXT(StRegisteredWait *RegWait) : UnregisterEvent(false) {
        RegisteredWait = RegWait;
    }
};


ULONG
WINAPI
UnregisterThread (
    __in PVOID Argument
    )
{
    UCONTEXT *Context = (UCONTEXT *)Argument;

    printf("+++unregister thread starts...\n");

    Context->UnregisterEvent.Wait();
    if (Context->RegisteredWait->Unregister()) {
        printf("+++ WAIT unregistered\n");
    }
    return 0;
}

//
// The test function.
//

VOID
RunRegisteredWaitTest(
    )
{

    //SetProcessAffinityMask(GetCurrentProcess(), (1 << 0));
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    StSemaphore Semaphore(0);
    StRegisteredWait RegWait;

    UCONTEXT UContext(&RegWait);
    // HANDLE Thread = StUmsThread_Create(NULL, 0, UnregisterThread, &UContext, 0, NULL);
    HANDLE Thread = CreateThread(NULL, 0, UnregisterThread, &UContext, 0, NULL);
    CloseHandle(Thread);

    StWaitable::RegisterWait(&Semaphore, &RegWait, RegisteredWaitCallback, &RegWait, 250, FALSE);
    getchar();
    Semaphore.Release(10);
    UContext.UnregisterEvent.Set();
    for (int i = 0; i < 100; i++) {
        Semaphore.Release();
    }
    getchar();
}
