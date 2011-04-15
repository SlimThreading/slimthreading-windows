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

#include "StInternal.h"

//
// The maximum recursive waits allowed.
//

#define MAX_RECURSIVE_WAITS		16

//
// TLS index used to store the TEB extension.
//

static ULONG TebExtensionTlsIndex = -1;

//
// Allocated a park spot to block the current thread.
//

HANDLE
FASTCALL
AllocParkSpot (
    )
{

    PTEB_EXTENSION Extension;	
    PPARK_SPOT ParkSpot;

    //
    // Get the address of the current thread's TEB extension.
    //

    Extension = GetTebExtension();	

    //
    // Try to get an available park spot from the free list.
    //
    
    if ((ParkSpot = Extension->FreeParkSpotList) != NULL) {
        Extension->FreeParkSpotList = ParkSpot->Next;
        ParkSpot->State = 0;
        return ParkSpot;
    }

    //
    // The free park spot list is empty; so, allocate memory to store
    // a new park spot.
    //
    
    ParkSpot = (PPARK_SPOT)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PARK_SPOT));
    if (ParkSpot == NULL) {
        return NULL;
    }

#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

    if (!IsUmsWorkerThread()) {

        //
        // Create a new synchronization event.
        //
    
        ParkSpot->EventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);

        if (ParkSpot->EventHandle == NULL) {
            HeapFree(GetProcessHeap(), 0, ParkSpot);
            return NULL;
        }
    }
#else

    //
    // Create a new synchronization event.
    //
    
    ParkSpot->EventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (ParkSpot->EventHandle == NULL) {
        HeapFree(GetProcessHeap(), 0, ParkSpot);
        return NULL;
    }

#endif

    //
    // Initialize the park spot structure and return it.
    //

    ParkSpot->Extension = Extension;
    ParkSpot->ThreadId = Extension->ThreadId;
    ParkSpot->Context = Extension->Context;
    return ParkSpot;
}

#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

//
// Allocated a park spot for a UMS worker thread.
//

static
PPARK_SPOT
FASTCALL
UmsWorkerAllocParkSpot (
    __inout PTEB_EXTENSION Extension
    )
{
    PPARK_SPOT ParkSpot;

    //
    // Try to get an available park spot from the free list.
    //
    
    if ((ParkSpot = Extension->FreeParkSpotList) != NULL) {
        Extension->FreeParkSpotList = ParkSpot->Next;
        ParkSpot->State = 0;
        return ParkSpot;
    }
    
    //
    // The free park spot list is empty; so, allocate memory to store
    // a new park spot.
    //
    
    ParkSpot = (PPARK_SPOT)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PARK_SPOT));
    if (ParkSpot == NULL) {
        return NULL;
    }

    //
    // Initialize the park spot structure and return it.
    //

    ParkSpot->Extension = Extension;
    ParkSpot->ThreadId = Extension->ThreadId;
    ParkSpot->Context = Extension->Context;
    return ParkSpot;
}

#endif

//
// Frees the specified park spot.
//

VOID
FASTCALL
FreeParkSpot (
    __in HANDLE ParkSpotHandle
    )
{
    PPARK_SPOT ParkSpot = (PPARK_SPOT)ParkSpotHandle;
    PTEB_EXTENSION Extension = ParkSpot->Extension;
    
    //
    // Insert the park spot in the free list and return.
    //
    
    ParkSpot->Next = Extension->FreeParkSpotList;
    Extension->FreeParkSpotList = ParkSpot;
}

//
// Waits until the park spot is signalled or the specified
// timeout expires.
//

VOID
FASTCALL
WaitForParkSpot (
    __inout PST_PARKER Parker,
    __in HANDLE ParkSpotHandle,
    __in ULONG Timeout
    )
{
    PPARK_SPOT ParkSpot = (PPARK_SPOT)ParkSpotHandle;

    (*ParkSpot->Extension->Vtbl->WaitForParkSpot)(Parker, ParkSpot, Timeout);
}

//
// Waits for park spot for normal non-STA threads.
//

static
VOID
FASTCALL
NonStaWaitForParkSpot (
    __inout PST_PARKER Parker,
    __inout PPARK_SPOT ParkSpot,
    __in ULONG Timeout
    )
{
    ULONG Result;
    ULONG LastTime;
    BOOL Alertable;

    //
    // Wait until the park spot is signalled or the specified
    // timeout expires.
    //

    LastTime = (Timeout != INFINITE) ? GetTickCount() : 0;
    Alertable = (ParkSpot->Extension->WaitsInProgress++ < MAX_RECURSIVE_WAITS);
    do {


        //
        // If the park spot was already set, return success.
        //
    
        if (ParkSpot->State != 0) {
            Result = WAIT_OBJECT_0;
            break;
        }
        
        //
        // If the time timeout expired, break the loop.
        //
        
        if (Timeout == 0) {
            Result = WAIT_TIMEOUT;
            break;
        }
        
        //
        // Waits on the synchronization event that is used to
        // implement the park spot.
        //

        if ((Result = WaitForSingleObjectEx(ParkSpot->EventHandle,
                                            Timeout, Alertable)) != WAIT_IO_COMPLETION) {
            break;
        }

        //
        // The wait was interrupted to deliver a user-mode APC to the
        // current thread. Since that the user-mode APC was already
        // executed, we adjust the timeout value and repeat the wait
        // on the synchronization event.
        //

        if (ParkSpot->State == 0 && Timeout != INFINITE) {
            ULONG Now = GetTickCount();
            ULONG Elapsed = (Now == LastTime) ? 1 : Now - LastTime;
            if (Elapsed < Timeout)
                Timeout -= Elapsed;
            else
                Timeout = 0;
            LastTime = Now;
        }
    } while (TRUE);

    //
    // If the wait on the park spot failed for some reason, try to cancel
    // the parker and pass the wait status through it.
    //

    if (Result != WAIT_OBJECT_0) {
        if (TryCancelParker(Parker)) {
            UnparkSelf(Parker, Result);
        } else {
            WaitForSingleObject(ParkSpot->EventHandle, INFINITE);
        }
    }
    ParkSpot->Extension->WaitsInProgress--;
}

//
// Waits for park spot for normal STA threads.
//

static
VOID
FASTCALL
StaWaitForParkSpot (
    __inout PST_PARKER Parker,
    __inout PPARK_SPOT ParkSpot,
    __in ULONG Timeout
    )
{
    ULONG Result;
    ULONG LastTime;
    ULONG WaitFlags;

    //
    // Wait until the park spot is signalled or the specified
    // timeout expires.
    //

    LastTime = (Timeout != INFINITE) ? GetTickCount() : 0;
    WaitFlags = (ParkSpot->Extension->WaitsInProgress++ < MAX_RECURSIVE_WAITS) ?
                    COWAIT_ALERTABLE : 0;
    do {

        //
        // If the park spot was already set, return success.
        //
    
        if (ParkSpot->State != 0) {
            Result = WAIT_OBJECT_0;
            break;
        }
        
        //
        // If the time timeout expired, break the loop.
        //
        
        if (Timeout == 0) {
            Result = WAIT_TIMEOUT;
            break;
        }
        
        //
        // Waits on the synchronization event pumping COM messages.
        //

        Result = WAIT_FAILED;
        CoWaitForMultipleHandles(WaitFlags, Timeout, 1, &ParkSpot->EventHandle, &Result);
        if (Result != WAIT_OBJECT_0 && Result != WAIT_TIMEOUT &&
            Result != WAIT_IO_COMPLETION) {
            Result = WAIT_FAILED;
        }

        if (Result != WAIT_IO_COMPLETION) {
            break;
        }

        //
        // The wait was interrupted to deliver a user-mode APC to the
        // current thread. Since that the user-mode APC was already
        // executed, we adjust the timeout value and repeat the wait
        // on the synchronization event.
        //

        if (ParkSpot->State == 0 && Timeout != INFINITE) {
            ULONG Now = GetTickCount();
            ULONG Elapsed = (Now == LastTime) ? 1 : Now - LastTime;
            if (Elapsed < Timeout)
                Timeout -= Elapsed;
            else
                Timeout = 0;
            LastTime = Now;
        }
    } while (TRUE);
    if (Result != WAIT_OBJECT_0) {
        if (TryCancelParker(Parker)) {
            UnparkSelf(Parker, Result);
        } else {
            WaitForSingleObject(ParkSpot->EventHandle, INFINITE);
        }
    }
    ParkSpot->Extension->WaitsInProgress--;
}

#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

//
// Special wait for park spot used by a UMS scheduler threads
// when they wait for ready UMS contexts.
//

VOID
FASTCALL
WaitForUmsSchedulerParkSpot (
    __inout PST_PARKER Parker,
    __in HANDLE ParkSpotHandle,
    __in ULONG Timeout,
    __in HANDLE CompletionListEvent
    )
{
    PPARK_SPOT ParkSpot;
    HANDLE Events[2];
    ULONG Result;

    //
    // Get the park spot pointer.
    //

    ParkSpot = (PPARK_SPOT)ParkSpotHandle;
    
    //
    // We will wait for the park spot event and the completion
    // list event.
    //

    Events[0] = ParkSpot->EventHandle;
    Events[1] = CompletionListEvent;

    Result = WaitForMultipleObjects(2, Events, FALSE, Timeout);

    //
    // If the wait on the ...
    //

    if (Result != WAIT_OBJECT_0) {
        _ASSERT(Result == (WAIT_OBJECT_0 + 1) || Result == WAIT_TIMEOUT);
        if (TryCancelParker(Parker)) {
            UnparkSelf(Parker, (Result == WAIT_OBJECT_0 + 1) ? WAIT_ALERTED : WAIT_TIMEOUT);
        } else {

            //
            // We can't, so, ...
            //

            if (Result == WAIT_OBJECT_0 + 1) {
                SetEvent(CompletionListEvent);
            }
            WaitForSingleObject(ParkSpot->EventHandle, INFINITE);
        }
    }
}

#endif


#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

//
// Waits for a UMS worker thread's park spot.
//

static
VOID
FASTCALL
UmsWorkerWaitForParkSpot (
    __inout PST_PARKER Parker,
    __inout PPARK_SPOT ParkSpot,
    __in ULONG Timeout
    )
{
    RAW_TIMER Timer;

    if (Timeout == 0) {
        if (TryCancelParker(Parker)) {
            UnparkSelf(Parker, WAIT_TIMEOUT);
            return;
        }
    } else if (Timeout != INFINITE) {
        InitializeRawTimer(&Timer, Parker);
        SetRawTimer(&Timer, Timeout);
    }

    //
    // Block the UMS worker thread on its park spot.
    //

    WaitForUmsWorkerParkSpot(ParkSpot);

    //
    // If we set a timer and it didn't expired, cancel it.
    //

    if (Timeout != INFINITE && Parker->WaitStatus != WAIT_TIMEOUT) {
        UnlinkRawTimer(&Timer);
    }
}

#endif

//
// Sets the specified park spot to the signalled state.
//

VOID
FASTCALL
SetParkSpot (
    __in HANDLE ParkSpotHandle
    )
{
    PPARK_SPOT ParkSpot = (PPARK_SPOT)ParkSpotHandle;

    (*ParkSpot->Extension->Vtbl->SetParkSpot)(ParkSpot);
}

//
// Sets the park spot for normal threads.
//

static
VOID
FASTCALL
NormalSetParkSpot (
    __in PPARK_SPOT ParkSpot
    )
{

    //
    // When a thread does a self-unpark and the wait-in-progress
    // bit is clear, that means that the wait on the park spot
    // was interrupted to deliver a user-mode APC.
    // In this situation we don't need to propagate the set to kernel,
    // because the thread will see the park spot signalled when it
    // returns from WaitForSingleObjectEx with the WAIT_IO_COMPLETION
    // status.
    //

    if (ParkSpot->ThreadId != GetCurrentThreadId()) {
        SetEvent(ParkSpot->EventHandle);
    } else {
        ParkSpot->State = 1;	
    }
}

#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

//
// Sets the park spot for UMS worker threads.
//

static
VOID
FASTCALL
UmsWorkerSetParkSpot (
    __in PPARK_SPOT ParkSpot
    )
{
    SetUmsWorkerParkSpot(ParkSpot);
}

#endif

//
// Cleanup the TEB extension of the current thread.
//

static
VOID
FASTCALL
CleanupTebExtension (
    )
{
    PTEB_EXTENSION Extension;
    PPARK_SPOT ParkSpot;

    Extension = GetTebExtension();
            
    //
    // Free the allocated park spots.
    //
    
    while ((ParkSpot = Extension->FreeParkSpotList) != NULL) {
        Extension->FreeParkSpotList = ParkSpot->Next;
        if (ParkSpot->EventHandle != NULL) {
            CloseHandle(ParkSpot->EventHandle);
        }
        HeapFree(GetProcessHeap(), 0, ParkSpot);
    }
    
    //
    // If a resource table has allocated entries, free it.
    //

    if (Extension->ResourceTable.Entries != NULL) {
        HeapFree(GetProcessHeap(), 0, Extension->ResourceTable.Entries); 
    }

    //
    // Free the TEB extension structure and return.
    //
    
    HeapFree(GetProcessHeap(), 0, Extension);
    return;
}

//
// TEB extension vtable for normal non-STA threads.
//

static
TEB_EXTENSION_VTBL NonStaTebExtensionVtbl = {
    NonStaWaitForParkSpot,
    NormalSetParkSpot,
};


//
// TEB extension vtable for normal STA threads.
//

static
TEB_EXTENSION_VTBL StaTebExtensionVtbl = {
    StaWaitForParkSpot,
    NormalSetParkSpot,
};

#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

//
// TEB extension vtable for UMS worker threads.
//

static
TEB_EXTENSION_VTBL UmsWorkerTebExtensionVtbl = {
    UmsWorkerWaitForParkSpot,
    UmsWorkerSetParkSpot,
};

#endif

//
// Returns the TEB extension for the current thread.
//
// NOTE: If the thread doesn't have a TEB extension it will get
// 		 a non-STA thread's extension. 
//

PTEB_EXTENSION
FASTCALL
GetTebExtension (
    )
{
    PTEB_EXTENSION Extension;

    //
    // Get the pointer to the current thread's extension.
    //
    
    if ((Extension = (PTEB_EXTENSION)TlsGetValue(TebExtensionTlsIndex)) == NULL) {
    
        //
        // The current thread doesn't have a TEB extension; so, allocate one.
        //

        Extension = (PTEB_EXTENSION)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                              sizeof(TEB_EXTENSION));

        if (Extension == NULL) {
            return NULL;
        }
    
        //
        // Initialize the thread ID field.
        //
        
        Extension->ThreadId = GetCurrentThreadId();

        //
        // Initialize the virtual function table.
        //
        Extension->Vtbl = &NonStaTebExtensionVtbl;
        TlsSetValue(TebExtensionTlsIndex, Extension);
    }
    return Extension;
}

#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

//
// Ensures that the current thread gets a UMS worker thread's TEB extension.
//

PTEB_EXTENSION
FASTCALL
EnsureUmsThreadTebExtension (
    )
{
    PTEB_EXTENSION Extension;

    //
    // Get a non-sta thread's TEB extension and change the virtual
    // function table to a UMS worker thread's extension.
    //
    
    Extension = GetTebExtension();
    Extension->Vtbl = &UmsWorkerTebExtensionVtbl;
    return Extension;
}

#endif

//
// Sets or clears the COM STA affinity for the current thread.
//

BOOLEAN
FASTCALL
SetStaAffinity (
    __in BOOLEAN StaAffinity
    )
{
    PTEB_EXTENSION Extension;	
    BOOL OldState;

    //
    // Get the address of the current thread's TEB extension.
    //
    
    Extension = GetTebExtension();
    OldState = (Extension->Vtbl == &StaTebExtensionVtbl);
    Extension->Vtbl = (StaAffinity) ? &StaTebExtensionVtbl : &NonStaTebExtensionVtbl;
    return OldState;
}


//
// Returns the address of the resource table for the
// current thread.
//

PVOID
FASTCALL
GetRawResourceTable (
    )
{
    return &GetTebExtension()->ResourceTable;
}

//
// Returns true if the current thread is a STA thread.
//

BOOL
FASTCALL
IsStaThread (
    )
{
    return GetTebExtension()->Vtbl == &StaTebExtensionVtbl;
}

//
// Returns true if the current thread is a Non-STA thread.
//

BOOL
FASTCALL
IsNonStaThread (
    )
{
    return GetTebExtension()->Vtbl == &NonStaTebExtensionVtbl;
}

#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

//
// Returns true if the current thread is a UMS worker thread.
//

BOOL
FASTCALL
IsUmsWorkerThread (
    )
{
    return GetTebExtension()->Vtbl == &UmsWorkerTebExtensionVtbl;
}

#else

BOOL
FASTCALL
IsUmsWorkerThread (
    )
{
    return FALSE;
}

#endif

//
// Allocates and releases the resources that each thread
// needs to use the slim threading library.
// 

BOOL
WINAPI
DllMain (
    __in HANDLE hinstDLL, 
    __in ULONG Reason, 
    __in LPVOID Ignored
    )
{	
    switch (Reason) {
        case DLL_PROCESS_ATTACH:
        
            //
            // When this DLL is loaded into a process, we allocate a TLS index
            // where each thread will store its TEB extension.
            //
            
            if ((TebExtensionTlsIndex = TlsAlloc()) == -1) {
        
                //
                // We can't allocate a TLS index, so return FALSE to fail LoadLibrary.
                //
        
                return FALSE;
            }
            break;
            
        case DLL_THREAD_DETACH:
        
            //
            // Cleanup the TEB extension and free the allocated resources.
            //
            
            CleanupTebExtension();
            break;
            
        case DLL_PROCESS_DETACH:

            //
            // When this DLL is unloaded from a process, we need
            // to cleanup the thread extension used by the current
            // thread, if any, and free the TLS index allocated when
            // the DLL was loaded.
            //
            
            CleanupTebExtension();
            TlsFree(TebExtensionTlsIndex);
            break;
    }
    return TRUE;
}
