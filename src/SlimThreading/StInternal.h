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

#pragma once

#include "StBase.h"
#include "St.h"
#include <intrin.h>
#pragma intrinsic(_ReadBarrier)

/*++
 *
 * Parker.
 *
 --*/

//
// Definitions releated to parker's *State* field.
// 

#define WAIT_IN_PROGRESS_BIT	31
#define WAIT_IN_PROGRESS		(1 << WAIT_IN_PROGRESS_BIT)
#define LOCK_COUNT_MASK			((1 << 16) - 1)

//
// Initializes the parker.
//

FORCEINLINE
VOID
InitializeParker (
    __out PST_PARKER Parker,
    __in USHORT Count
    )
{	
    Parker->State = (Count | WAIT_IN_PROGRESS);
}

//
// Returns true if the parker is locked.
//

FORCEINLINE
BOOLEAN
IsParkerLocked (
    __in PST_PARKER Parker
    )
{
    return (Parker->State & LOCK_COUNT_MASK) == 0;
}

//
// Tries to lock the parker.
//

FORCEINLINE
BOOLEAN
TryLockParker (
    __inout PST_PARKER Parker
    )
{
    do {
        LONG State;
    
        //
        // If the parker is already locked, return false.
        //
        
        if (((State = Parker->State) & LOCK_COUNT_MASK) == 0) {
            return FALSE;
        }

        //
        // Try to decrement the lock counter.
        //

        if (CasLong(&Parker->State, State, State - 1)) {

            //
            // If the lock counter reaches zero, return true;
            // otherwise, return false.
            //

            return (State & LOCK_COUNT_MASK) == 1;
        }
    } while (TRUE);
}

//
// Tries to cancel the parker.
//

FORCEINLINE
BOOLEAN
TryCancelParker (
    __inout PST_PARKER Parker
    )
{
    do {
        LONG State;
    
        //
        // If the parker is already locked, return false.
        //
        
        if (((State = Parker->State) & LOCK_COUNT_MASK) == 0) {
            return FALSE;
        }
        
        //
        // Try to clear the lock counter, preserving the wait-in-progress bit.
        //

        if (CasLong(&Parker->State, State, State & WAIT_IN_PROGRESS)) {
            return TRUE;
        }
    } while (TRUE);
}


//
// Unparks the parker's owner thread if it isn't still blcoked
// on the underlying park spot.
//

FORCEINLINE
BOOLEAN
UnparkInProgressThread (
    __inout PST_PARKER Parker,
    __in ULONG WaitStatus
    )
{

    //
    // Set the park wait status.
    //

    Parker->WaitStatus = WaitStatus;
    
    //
    // Unpark the thread if the wait-in-progress bit is still set.
    //
        
    return (Parker->State & WAIT_IN_PROGRESS) != 0 && 
           (InterlockedExchange(&Parker->State, 0) & WAIT_IN_PROGRESS) != 0;
}

//
// Unparks the parker's owner thread.
//

VOID
FASTCALL
UnparkThread (
    __inout PST_PARKER Parker,
    __in ULONG WaitStatus
    );

//
// Unparks the current thread.
//

FORCEINLINE
VOID
UnparkSelf (
    __inout PST_PARKER Parker,
    __in ULONG WaitStatus
    )
{
    
    //
    // Since that the parker was locked by the current thread, no
    // other thread will access the *State* field; so, an ordinary write
    // is sufficient to clear the wait-in-progress bit.
    //
    
    Parker->WaitStatus = WaitStatus;
    Parker->State = 0;
}

/*++
 *
 * Alerter.
 *
 --*/

#define ALERTED		((PST_PARKER)~0)

//
// Sets the alerter.
//

BOOLEAN
FASTCALL
SetAlerter (
    __inout PST_ALERTER Alerter
    );

//
// Unparks with alerted status all threads whose non-locked
// parkers are inserted in the specified list.
//

static
FORCEINLINE
VOID
AlertParkerList (
    __in PST_PARKER First
    )
{

    while (First != NULL) {
        PST_PARKER Next = First->Next;
        if (TryCancelParker(First)) {
            UnparkThread(First, WAIT_ALERTED);
        }

        //
        // Mark the parker as unlink and get the next one.
        //

        First->Next = First;
        First = Next;
    }
}

//
// Deregisters the parker from the alerter - slow path.
//

VOID
FASTCALL
SlowDeregisterParker (
     __inout PST_ALERTER Alerter,
     __inout PST_PARKER Parker
     );

//
// Deregisters the parker from the alerter.
//

static
FORCEINLINE
VOID
DeregisterParker (
     __inout PST_ALERTER Alerter,
     __inout PST_PARKER Parker
     )
{

    //
    // Very often, an alerter has only a parker registered with it.
    // So, we inline that scenario.
    //

    if (Parker->Next == Parker ||
        (Parker->Next == NULL && CasPointer(&Alerter->State, Parker, NULL))) {
        return;
    }
    SlowDeregisterParker(Alerter, Parker);
}

//
// Registers the parker with the alerter.
//

static
FORCEINLINE
BOOLEAN
RegisterParker (
    __inout PST_ALERTER Alerter,
    __in PST_PARKER Parker
    )
{
    PST_PARKER State;

    //
    // Try to insert the parker in alerter's list, but only if the
    // alerter remains non-set.
    //

    do {

        if ((State = Alerter->State) == ALERTED) {
            return FALSE;
        }	
        Parker->Next = State;
        if (CasPointer(&Alerter->State, State, Parker)) {			
            return TRUE;
        }
    } while (TRUE);
}

/*++
 *
 * Parker.
 *
 --*/

//
// Parks the current thread until it is unparked, the specified timeout
// expires or the alerter is alerted. Before the thread blocks on a park
// spot, it executes the specified number of spin cycles.
//

ULONG
FASTCALL
ParkThreadEx (
    __inout PST_PARKER Parker,
    __in ULONG SpinCycles,
    __in ULONG Timeout,
    __inout PST_ALERTER Alerter
    );


ULONG
FASTCALL
ParkThread (
    __inout PST_PARKER Parker
    );


/*++
 *
 * Convenience functions related to wait block and parker.
 *
 --*/

//
// Initializes the parker for exclusive release along with the wait block.
//

FORCEINLINE
VOID
InitializeParkerAndWaitBlock (
    __out PWAIT_BLOCK WaitBlock,
    __in struct _ST_PARKER *Parker,
    __in LONG Request,
    __in WAIT_TYPE WaitType,
    __in ULONG WaitKey
    )
{
    InitializeParker(Parker, 1);
    WaitBlock->Parker = Parker;
    WaitBlock->Request = Request;
    WaitBlock->WaitType = (SHORT)WaitType;
    WaitBlock->WaitKey = (SHORT)WaitKey;
}

FORCEINLINE
VOID
InitializeParkerAndWaitBlockEx (
    __out PWAIT_BLOCK WaitBlock,
    __in struct _ST_PARKER *Parker,
    __in LONG Request,
    __in PVOID Channel,
    __in WAIT_TYPE WaitType,
    __in ULONG WaitKey
    )
{
    InitializeParker(Parker, 1);
    WaitBlock->Parker = Parker;
    WaitBlock->Channel = Channel;
    WaitBlock->Request = Request;
    WaitBlock->WaitType = (SHORT)WaitType;
    WaitBlock->WaitKey = (SHORT)WaitKey;
}


/*++
 *
 * Non-reentrant Lock.
 *
 --*/

#define LOCK_FREE	((PLIST_ENTRY_)~0)
#define LOCK_BUSY	((PLIST_ENTRY_)0)

//
// Ties to acquire the lock (for internal use).
//

FORCEINLINE
BOOLEAN
TryAcquireLockInternal (
    __inout PST_LOCK Lock
    )
{
    return (Lock->State == LOCK_FREE && CasPointer(&Lock->State, LOCK_FREE, LOCK_BUSY));
}

/*++
 *
 *
 --*/

BOOLEAN
FASTCALL
Lock_TryEnterTail (
    __inout PST_LOCK Lock,
    __in ULONG Timeout
    );

ULONG
FASTCALL
FairLock_TryEnterTail (
    __inout PST_FAIR_LOCK Lock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Unparks a wake up list of locked threads.
//

FORCEINLINE
VOID
UnparkWakeList (
    __in PWAKE_LIST WakeList
    )
{
    PLIST_ENTRY_ Entry;
    
    Entry = WakeList->First;
    while (Entry != NULL) {
        PLIST_ENTRY_ Next = Entry->Blink;
        PWAIT_BLOCK WaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
        UnparkThread(WaitBlock->Parker, WaitBlock->WaitKey);
        Entry = Next;
    }
}

/*++
 *
 * The Locked Queue API.
 *
 --*/

//
// Initializes the locked queue.
//

#define LOCKED_QUEUE_SPINS	200

FORCEINLINE
VOID
InitializeLockedQueue (
    __out PLOCKED_QUEUE Queue
    )
{
    Queue->LockState = LOCK_FREE;
    InitializeListHead(&Queue->Head);
    Queue->FrontRequest = 0;
    Queue->SpinCount = IsMultiProcessor() ? LOCKED_QUEUE_SPINS : 0;
}

//
// Returns true if the locked queue is empty.
//

FORCEINLINE
BOOLEAN
IsLockedQueueEmpty (
    __out PLOCKED_QUEUE Queue
    )
{
    PLIST_ENTRY_ LockState = Queue->LockState;
    return ((LockState == LOCK_BUSY || LockState == LOCK_FREE) && IsListEmpty(&Queue->Head));
}

//
// Tries to unlock the locked queue.
//

BOOLEAN
FASTCALL
SlowTryUnlockLockedQueue (
    __inout PLOCKED_QUEUE Queue,
    __in BOOLEAN ForceUnlock
    );

FORCEINLINE
BOOLEAN
TryUnlockLockedQueue (
    __inout PLOCKED_QUEUE Queue,
    __in BOOLEAN ForceUnlock
    )
{
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;

    if (Queue->LockState == LOCK_BUSY && Queue->LockPrivateQueue == NULL) {
        ListHead = &Queue->Head;
        Queue->FrontRequest = ((Entry = ListHead->Flink) == ListHead) ?  0 :
                (CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry)->Request & MAX_REQUEST);
        if (CasPointer(&Queue->LockState, LOCK_BUSY, LOCK_FREE)) {
            return TRUE;
        }
        Queue->FrontRequest = 0;
    }
    return SlowTryUnlockLockedQueue(Queue, ForceUnlock);
}

//
// Enqueues a wait block in the locked queue.
//

BOOLEAN
FASTCALL
SlowEnqueueInLockedQueue (
    __inout PLOCKED_QUEUE Queue,
    __inout PWAIT_BLOCK WaitBlock,
    __out BOOLEAN *SpinHint
    );

FORCEINLINE
BOOLEAN
EnqueueInLockedQueue (
    __inout PLOCKED_QUEUE Queue,
    __inout PWAIT_BLOCK WaitBlock,
    __out BOOLEAN *FrontHint
    )
{

    if (Queue->LockState == LOCK_FREE && CasPointer(&Queue->LockState, LOCK_FREE, LOCK_BUSY)) {
        PLIST_ENTRY_ ListHead = &Queue->Head;
        InsertTailList(ListHead, &WaitBlock->WaitListEntry);
        *FrontHint = (ListHead->Flink == &WaitBlock->WaitListEntry);
        Queue->FrontRequest = 0;
        Queue->LockPrivateQueue = NULL;
        return TRUE;
    }
    return SlowEnqueueInLockedQueue(Queue, WaitBlock, FrontHint);
}

//
// Tries to lock the locked queue (called by releasers).
//

FORCEINLINE
BOOLEAN
TryLockLockedQueue (
    __inout PLOCKED_QUEUE Queue
    )
{
    if (Queue->LockState == LOCK_FREE && CasPointer(&Queue->LockState, LOCK_FREE, LOCK_BUSY)) {
        Queue->FrontRequest = 0;
        Queue->LockPrivateQueue = NULL;
        return TRUE;
    }
    return FALSE;
}

//
// Locks the locked queue.
//

BOOLEAN
FASTCALL
LockLockedQueue (
    __inout PLOCKED_QUEUE Queue,
    __inout PLIST_ENTRY_ Entry
    );

/*++
 *
 * Fair Lock.
 *
 --*/

//
// Values used for fair lock state.
//

#define FLOCK_BUSY		0
#define FLOCK_FREE		1

//
// Tries to acquire immediately the fair lock.
//

FORCEINLINE
BOOLEAN
TryAcquireFairLockInternal (
    __inout PST_FAIR_LOCK Lock
    )
{
    
    do {

        //
        // If the lock is busy or is free but the wait queue is
        // non-empty, the lock can't be immediatelly acquired.
        //

        if (Lock->State == FLOCK_BUSY || !IsLockedQueueEmpty(&Lock->Queue)) {
            return FALSE;
        }

        //
        // The lock seems free; so, try to acquire it.
        //

        if (CasLong(&Lock->State, FLOCK_FREE, FLOCK_BUSY)) {
            return TRUE;
        }
    } while (TRUE);
}

/*++
 *
 * Resorce Table.
 *
 --*/

//
// The structure that holds an entry of the resource table.
//

typedef struct _RESOURCE_TABLE_ENTRY {
    PVOID Resource;
    ULONG RefCount;
} RESOURCE_TABLE_ENTRY, *PRESOURCE_TABLE_ENTRY;

//
// The resource table.
//

#define RESOURCE_TABLE_INITIAL_SIZE		4

typedef struct _RESOURCE_TABLE {
    PRESOURCE_TABLE_ENTRY Entries;
    ULONG Size;
    ULONG NextFree;
} RESOURCE_TABLE, *PRESOURCE_TABLE;

//
// Returns the address of the the raw resource table for the
// current thread that is store on the TLS.
//

PVOID
FASTCALL
GetRawResourceTable (
    );

//
// Returns the address of the resource table for the
// current thread.
//

FORCEINLINE
PRESOURCE_TABLE
GetResourceTable (
    )
{
    return (PRESOURCE_TABLE)GetRawResourceTable();
}

//
// The Resource table API.
//

//
//	Looks up an entry for the specified resource.
//

FORCEINLINE
PRESOURCE_TABLE_ENTRY
LookupEntryOnResourceTable (
    __in PVOID Resource,
    __out_opt PRESOURCE_TABLE *Table
    )
{

    PRESOURCE_TABLE LocalTable;
    PRESOURCE_TABLE_ENTRY Entry;
    LONG Index;
    
    //
    // Get the address of resource table for the current thread.
    //

    LocalTable = GetResourceTable();

    if (ARGUMENT_PRESENT(Table)) {
        *Table = LocalTable;
    }
    
    //
    // The table search is done from from bottom to top, because the
    // lock aquisition/release is typically nested.
    //

    Index = LocalTable->NextFree - 1;
    Entry = LocalTable->Entries + Index;
    while (Index >= 0) {
        if (Resource == Entry->Resource) { 

            //
            // The current entry is associated to the specified resource,
            // so, return its address which means success.
            //

            return Entry;
        }
        Entry--;
        Index--;
    }

    //
    // The current thread is not owner of the specified resource,
    // so, return failure.
    //
    
    return NULL;
}

//
// Adds a new entry to the resource table.
//

PRESOURCE_TABLE_ENTRY
FASTCALL
AddEntryToResourceTable (
    __inout PRESOURCE_TABLE Table,
    __in PVOID Resource
    );

//
// Free the resource table entry.
//

//
// Free the resource table entry.
//

FORCEINLINE
VOID
FreeResourceTableEntry (
    __inout PRESOURCE_TABLE Table,
    __in PRESOURCE_TABLE_ENTRY Entry
    )
{
    LONG Index = (LONG)(Table->Entries - Entry);			
    LONG LastIndex = Table->NextFree - 1;

    if (Index != LastIndex) {

        //
        // Copy the last entry of the table to the now freed one.
        // By doing so, we never have free entries in the middle of
        // the resource tables.
        //

        *Entry = Table->Entries[LastIndex];
    }
    Table->NextFree = LastIndex;
}

/*++
 *
 * Spin Lock internal API.
 *
 --*/

//
// Distinguished values used for spin lock's state.
//

#define SL_FREE		((PST_PARKER)~0)
#define SL_BUSY		((PST_PARKER)0)

//
// Initializes the spin lock.
//

FORCEINLINE
VOID
InitializeSpinLock (
    __out PSPINLOCK Lock,
    __in ULONG SpinCount
    )
{
    Lock->State = SL_FREE;
    Lock->SpinCount = IsMultiProcessor() ? SpinCount : 0;
}

//
// Slow path to acquire the spin lock.
//

VOID
FASTCALL
SlowAcquireSpinLock (
    __inout PSPINLOCK Lock
    );

//
// Acquires spin lock if it is free.
//

FORCEINLINE
BOOLEAN
TryAcquireSpinLock (
    __inout PSPINLOCK Lock
    )
{
    return (Lock->State == SL_FREE && CasPointer(&Lock->State, SL_FREE, SL_BUSY));
}

//
// Acquires spin lock.
//

FORCEINLINE
VOID
AcquireSpinLock (
    __inout PSPINLOCK Lock
    )
{
    if (Lock->State == SL_FREE && CasPointer(&Lock->State, SL_FREE, SL_BUSY)) {
        return;
    }
    SlowAcquireSpinLock(Lock);
}

//
// Releases the spin lock waiters.
//

VOID
FASTCALL
ReleaseSpinLockWaiters (
    __inout PST_PARKER WaitList
    );

//
// Releases the spin lock.
//

FORCEINLINE
VOID
ReleaseSpinLock (
    __inout PSPINLOCK Lock
    )
{
    PST_PARKER WaitList;

    if ((WaitList = InterlockedExchangePointer(&Lock->State, SL_FREE)) == SL_BUSY) {
        return;
    }
    ReleaseSpinLockWaiters(WaitList);
}

/*++
 *
 * Init Once Lock.
 *
 --*/

//
// Distinguished values of the *State* field of the lock. 
//

#define IOL_FREE		(PST_PARKER)NULL
#define IOL_BUSY		((PST_PARKER)~0)
#define IOL_AVAILABLE	((PST_PARKER)~1)

//
// Wait status.
//

#define IOL_STATUS_INIT			WAIT_OBJECT_0
#define IOL_STATUS_AVAILABLE	(WAIT_OBJECT_0 + 1)

//
// Initializes the init once lock.
//

FORCEINLINE
VOID
InitializeInitOnceLock (
    __out PST_INIT_ONCE_LOCK Lock
    )
{
    Lock->State = IOL_FREE;
}

//
// Slow path for try init target.
//

BOOLEAN
FASTCALL
SlowTryInitTarget (
    __inout PST_INIT_ONCE_LOCK Lock,
    __in ULONG SpinCount
    );

//
// Acquires the lock and returns true; or, returns false if the
// initialization already succeed and the associated target
// value is available.
//

FORCEINLINE
BOOLEAN
TryInitOnce (
    __inout PST_INIT_ONCE_LOCK Lock,
    __in ULONG SpinCount
    )
{
    //
    // If the initialization was already done, return false.
    //

    if (Lock->State == IOL_AVAILABLE) {
        _ReadBarrier();
        return FALSE;
    }
    return SlowTryInitTarget(Lock, SpinCount);
}

FORCEINLINE
BOOLEAN
TryInitTarget (
    __inout PST_INIT_ONCE_LOCK Lock
    )
{
    //
    // If the initialization was already done, return false.
    //

    if (Lock->State == IOL_AVAILABLE) {
        _ReadBarrier();
        return FALSE;
    }
    return SlowTryInitTarget(Lock, 0);
}

//
// Signals that the current thread completed the initialization.
//

FORCEINLINE
VOID
InitTargetCompleted (
    __inout PST_INIT_ONCE_LOCK Lock
    )
{
    PST_PARKER Entry;
    PST_PARKER Next;

    if ((Entry = InterlockedExchangePointer(&Lock->State, IOL_AVAILABLE)) == IOL_BUSY) {
        return;
    }

    //
    // The are threads waiting on the lock; so, release them with a
    // status that signals that the initialization was completed.
    //

    do {
        Next = Entry->Next;
        UnparkThread(Entry, IOL_STATUS_AVAILABLE);
    } while ((Entry = Next) != IOL_BUSY);
}

//
// Signals that the current thread failed the initialization.
//

FORCEINLINE
VOID
InitTargetFailed (
    __inout PST_INIT_ONCE_LOCK Lock
    )
{
    PST_PARKER State;

    do {

        //
        // if the lock's wait list is empty, frees the lock.
        //

        if ((State = Lock->State) == IOL_BUSY && CasPointer(&Lock->State, IOL_BUSY, IOL_FREE)) {
            return;
        }

        //
        // There are waiter threads. So, try to release one of them
        // with a wait status that tells it to peform the initialization
        // and return.
        //

        if (CasPointer(&Lock->State, State, State->Next)) {
            UnparkThread(State, IOL_STATUS_INIT);
            return;
        }
    } while (TRUE);
}

/*++
 *
 * Raw Timer
 *
 --*/

//+
// Raw timer API
//-

FORCEINLINE
VOID
InitializeRawTimer (
    __out PRAW_TIMER Timer,
    __in PST_PARKER Parker
    )
{
    Timer->Parker = Parker;
}

//
// Adds a raw timer to the timer list.
//

BOOLEAN
FASTCALL
SetRawTimer (
    __inout PRAW_TIMER Timer,
    __in ULONG Delay
    );

//
// Adds a raw timer to the timer list.
//

BOOLEAN
FASTCALL
SetPeriodicRawTimer (
    __inout PRAW_TIMER Timer,
    __in ULONG Period
    );

//
// Unlinks a cancelled raw timer.
//

VOID
FASTCALL
UnlinkRawTimer (
    __inout PRAW_TIMER Timer
    );

//
// Tries to cancel a raw timer if it is still active.
//

BOOLEAN
FASTCALL
TryCancelRawTimer (
    __inout PRAW_TIMER Timer
    );

/*++
 *
 * The callback parker API.
 *
 --*/

//
// Initialized the callback parker.
//

FORCEINLINE
VOID
InitializeCallbackParker (
    __out PCB_PARKER CbParker,
    __in PARKER_CALLBACK *Callback,
    __in PVOID CallbackContext
    )
{
    InitializeParker(&CbParker->Parker, 1);
    CbParker->Callback = Callback;
    CbParker->CallbackContext = CallbackContext;
}

//
// ...
//

ULONG
FASTCALL
EnableUnparkCallback (
    __inout PCB_PARKER Parker,
    __in ULONG Timeout,
    __in_opt PRAW_TIMER TimeoutTimer
    );

/*++
 *
 * Return Waitable synchronzer types.
 *
 --*/

//
// Returns true if the Waitable is a notification event.
//

FORCEINLINE
BOOLEAN
IsNotificationEvent (
    __in PST_WAITABLE Waitable
    )
{
    extern WAITABLE_VTBL NotificationEventVtbl;
    return (Waitable->Vptr == &NotificationEventVtbl);
}

//
// Returns true if the Waitable is a synchronization event.
//

FORCEINLINE
BOOLEAN
IsSynchronizationEvent (
    __in PST_WAITABLE Waitable
    )
{
    extern WAITABLE_VTBL SynchronizationEventVtbl;
    return (Waitable->Vptr == &SynchronizationEventVtbl);
}

//
// Returns true if the Waitable is a reentrant lock.
//

FORCEINLINE
BOOLEAN
IsReentrantLock (
    __in PST_WAITABLE Waitable
    )
{
    extern WAITABLE_VTBL ReentrantFairLockVtbl;
    extern WAITABLE_VTBL ReentrantReadWriteLockVtbl;

    return (Waitable->Vptr == &ReentrantFairLockVtbl ||
            Waitable->Vptr == &ReentrantReadWriteLockVtbl);
}

//
// Returns true if the Waitable is a semaphore.
//

FORCEINLINE
BOOLEAN
IsSemaphore (
    __in PST_WAITABLE Waitable
    )
{
    extern WAITABLE_VTBL SemaphoreVtbl;
    return (Waitable->Vptr == &SemaphoreVtbl);
}


/*++
 *
 * Park spot.
 *
 --*/

//
// The structure spot structure.
//

struct _TEB_EXTENSION;

typedef struct _PARK_SPOT {
    volatile LONG State;
    ULONG ThreadId;
    HANDLE EventHandle;
    PVOID Context;
    struct _PARK_SPOT *Next;
    struct _TEB_EXTENSION *Extension;
} PARK_SPOT, *PPARK_SPOT;

//
// The virtual functions associated to the TEB extension and
// the virtual function table itself.
//

typedef
PPARK_SPOT
FASTCALL
AllocParkSpotFn (
    __inout struct _TEB_EXTENSION *TebExtension
    );

typedef
VOID
FASTCALL
WaitForParkSpotFn (
    __inout PST_PARKER Parker,
    __inout PPARK_SPOT ParkSpot,
    __in ULONG Timeout
    );

typedef
VOID
FASTCALL
SetParkSpotFn (
    __inout PPARK_SPOT ParkSpot
    );

typedef struct _TEB_EXTENSION_VTBL {
    WaitForParkSpotFn *WaitForParkSpot;
    SetParkSpotFn *SetParkSpot;
} TEB_EXTENSION_VTBL, *PTEB_EXTENSION_VTBL;

//
// The TEB extension structure.
//

typedef struct _TEB_EXTENSION {
    PTEB_EXTENSION_VTBL Vtbl;
    RESOURCE_TABLE ResourceTable;
    ULONG ThreadId;
    PVOID Context;
    PPARK_SPOT FreeParkSpotList;
    USHORT WaitsInProgress;
} TEB_EXTENSION, *PTEB_EXTENSION;

//
// Returns the TEB extension for the current thread.
//

PTEB_EXTENSION
FASTCALL
GetTebExtension (
    );


//#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

//
// Ensures that the current thread gets a TEB extension.
//

PTEB_EXTENSION
FASTCALL
EnsureUmsThreadTebExtension (
    );

//#endif

//
// Allocates a park spot to block the current thread.
//

HANDLE
FASTCALL
AllocParkSpot (
    );

//
// Frees a previously allocated park spot.
//

VOID
FASTCALL
FreeParkSpot (
    __in HANDLE ParkSpotHandle
    );

//
// Waits on the specified park spot until it is set or the
// specified timeout expires.
//

VOID
FASTCALL
WaitForParkSpot (
    __inout PST_PARKER Parker,
    __in HANDLE ParkSpotHandle,
    __in ULONG Timeout
    );

#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

//
// Waits on the specified UMS scheduler park spot.
//

VOID
FASTCALL
WaitForUmsSchedulerParkSpot (
    __inout PST_PARKER Parker,
    __in HANDLE ParkSpotHandle,
    __in ULONG Timeout,
    __in HANDLE CompletionListEvent
    );

#endif

//
// Sets the specified park spot.
//

VOID
FASTCALL
SetParkSpot (
    __in HANDLE ParkSpot
    );
//
// Sets or clears the COM STA affinity for the current thread.
//

BOOLEAN
FASTCALL
SetStaAffinity (
    __in BOOLEAN StaAffinity
    );

//
// Returns true if the current thread is a STA thread.
//

BOOL
FASTCALL
IsStaThread (
    );

//
// Returns true if the current thread is a non-STA thread.
//

BOOL
FASTCALL
IsNonStaThread (
    );

//
// Returns true if the current thread is a UMS worker thread.
//

BOOL
FASTCALL
IsUmsWorkerThread (
    );


#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

/*++
 *
 * UMS scheduler related definitions.
 *
 --*/

//
// Sets the UMS park spot.
//

VOID
FASTCALL
SetUmsWorkerParkSpot (
    __inout PPARK_SPOT ParkSpot
    );

//
// Waits on a UMS park spot.
//

VOID
FASTCALL
WaitForUmsWorkerParkSpot (
    __inout PPARK_SPOT ParkSpot
    );

#endif


