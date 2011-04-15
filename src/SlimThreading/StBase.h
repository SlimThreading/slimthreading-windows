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

#include <windows.h>
#include <crtdbg.h>

//
// Internal functions use the fastcall call convention.
//

#ifndef FASTCALL
#define FASTCALL	__fastcall
#endif

//
// Determine if an argument is present by testing the value of the pointer
// to the argument value.
//

#define ARGUMENT_PRESENT(ArgumentPointer)	\
    ((PVOID)((ULONG_PTR)(ArgumentPointer)) != NULL)

//
// Wrapper macros to InterlockedCompareExchangeXxx functions. 
//

#define	CasLong(Location, OldValue, NewValue)		\
    (InterlockedCompareExchange(Location, NewValue, OldValue) == (OldValue))

#define CasPointer(Location, OldValue, NewValue)	\
    (InterlockedCompareExchangePointer((volatile PVOID *)(Location), NewValue, OldValue) == (OldValue))


/*++
 *
 * Doubly linked list definitions.
 *
 --*/

//
// Computes the address of the structure given an address of a field,
// the structure name and the name of the field.
//

#ifndef CONTAINING_RECORD

#define CONTAINING_RECORD(address, type, field) ((type *)( \
                          (PCHAR)(address) - \
                          (ULONG_PTR)(&((type *)0)->field)))

#endif

//
// This definition overrides the LIST_ENTRY definition to make
// the Flink field volatile, because we spin testing this field
// in order to detect when the list entry is unlinked.
//

typedef struct _LIST_ENTRY_ {
    struct _LIST_ENTRY_ * volatile Flink;
    struct _LIST_ENTRY_ * Blink;
} LIST_ENTRY_, *PLIST_ENTRY_;

//
// Initialized the doubly linked list.
//

FORCEINLINE
VOID
InitializeListHead(
    __in PLIST_ENTRY_ ListHead
    )
{
    ListHead->Flink = ListHead->Blink = ListHead;
}

//
// Returns true if the list is empty.
//

FORCEINLINE
BOOLEAN
IsListEmpty(
    __in const LIST_ENTRY_ * ListHead
    )
{
    return (BOOLEAN)(ListHead->Flink == ListHead);
}

//
// Removes the specified entry from the list where it
// is inserted. This function returns true if the list
// becomes empty.
//

FORCEINLINE
BOOLEAN
RemoveEntryList(
    __in PLIST_ENTRY_ Entry
    )
{
    PLIST_ENTRY_ Blink;
    PLIST_ENTRY_ Flink;

    Flink = Entry->Flink;
    Blink = Entry->Blink;
    Blink->Flink = Flink;
    Flink->Blink = Blink;
    return (BOOLEAN)(Flink == Blink);
}

//
// Removes the entry that is at the front of the list.
//

FORCEINLINE
PLIST_ENTRY_
RemoveHeadList(
    __in PLIST_ENTRY_ ListHead
    )
{
    PLIST_ENTRY_ Flink;
    PLIST_ENTRY_ Entry;

    Entry = ListHead->Flink;
    Flink = Entry->Flink;
    ListHead->Flink = Flink;
    Flink->Blink = ListHead;
    return Entry;
}

//
// Removes the entry that is at the tail of the list.
//

FORCEINLINE
PLIST_ENTRY_
RemoveTailList(
    __in PLIST_ENTRY_ ListHead
    )
{
    PLIST_ENTRY_ Blink;
    PLIST_ENTRY_ Entry;

    Entry = ListHead->Blink;
    Blink = Entry->Blink;
    ListHead->Blink = Blink;
    Blink->Flink = ListHead;
    return Entry;
}

//
// Inserts the specified entry at the tail of the list.
//

FORCEINLINE
VOID
InsertTailList(
    __in PLIST_ENTRY_ ListHead,
    __in PLIST_ENTRY_ Entry
    )
{
    PLIST_ENTRY_ Blink;

    Blink = ListHead->Blink;
    Entry->Flink = ListHead;
    Entry->Blink = Blink;
    Blink->Flink = Entry;
    ListHead->Blink = Entry;
}

//
// Inserts the specified entry at the head of the list.
//

FORCEINLINE
VOID
InsertHeadList(
    __in PLIST_ENTRY_ ListHead,
    __in PLIST_ENTRY_ Entry
    )
{
    PLIST_ENTRY_ Flink;

    Flink = ListHead->Flink;
    Entry->Flink = Flink;
    Entry->Blink = ListHead;
    Flink->Blink = Entry;
    ListHead->Flink = Entry;
}

//
// Appends a list to the tail of another list.
//

FORCEINLINE
VOID
AppendTailList(
    __in PLIST_ENTRY_ ListHead,
    __in PLIST_ENTRY_ ListToAppend
    )
{
    PLIST_ENTRY_ ListEnd = ListHead->Blink;

    ListEnd->Flink = ListToAppend->Flink;
    ListToAppend->Blink->Flink = ListHead;
    ListToAppend->Flink->Blink = ListEnd;
    ListHead->Blink = ListToAppend->Blink;
}

/*++
 *
 * A singly linked list used with thread wake up operations.
 * 
 * NOTE: It is a FIFO queue, linked through the LIST_ENTRY_.Blink field.
 *
 --*/

//
// The wake list structure.
//

typedef struct _WAKE_LIST {
    PLIST_ENTRY_ First;
    PLIST_ENTRY_ Last;
} WAKE_LIST, *PWAKE_LIST;

//
// Initialize the specified wake list.
//

FORCEINLINE
VOID
InitializeWakeList(
    __in PWAKE_LIST List
    )
{
    List->First = List->Last = NULL;
}

//
// Adds the specified entry to the specified wake list.
//

FORCEINLINE
VOID
AddEntryToWakeList(
    __in PWAKE_LIST List,
    __in PLIST_ENTRY_ Entry
    )
{
    if (List->First == NULL)
        List->First = Entry;
    else
        List->Last->Blink = Entry;
    List->Last = Entry;
    Entry->Blink = NULL;
}


/*++
 *
 * Wait block definitions.
 *
 --*/

//
// Types used by wait operations.
//

typedef enum _WAIT_TYPE {
    WaitAll,
    WaitAny
} WAIT_TYPE;

//
// Wait block used to queue acquire requets on the synchronizers.
//

//
// Forward declaration of the parker structure.
//

struct _ST_PARKER;

//
// The wait block structure (stack-allocated).
//

typedef struct _WAIT_BLOCK {
    LIST_ENTRY_ WaitListEntry;
    struct _ST_PARKER *Parker;
    PVOID Channel;
    LONG Request;
    SHORT WaitType;
    SHORT WaitKey;
} WAIT_BLOCK, *PWAIT_BLOCK;

//
// Initializes the wait block.
//


FORCEINLINE
VOID
InitializeWaitBlock (
    __out PWAIT_BLOCK WaitBlock,
    __in struct _ST_PARKER *Parker,
    __in LONG Request,
    __in WAIT_TYPE WaitType,
    __in ULONG WaitKey
    )
{
    WaitBlock->Parker = Parker;
    WaitBlock->Request = Request;
    WaitBlock->WaitType = (SHORT)WaitType;
    WaitBlock->WaitKey = (SHORT)WaitKey;
}

FORCEINLINE
VOID
InitializeWaitBlockEx (
    __out PWAIT_BLOCK WaitBlock,
    __in struct _ST_PARKER *Parker,
    __in LONG Request,
    __in PVOID Channel,
    __in WAIT_TYPE WaitType,
    __in LONG WaitKey
    )
{
    WaitBlock->Parker = Parker;
    WaitBlock->Channel = Channel;
    WaitBlock->Request = Request;
    WaitBlock->WaitType = (SHORT)WaitType;
    WaitBlock->WaitKey = (SHORT)WaitKey;
}

//
// The *Request* field of the wait blocks is used as follows.
// - bit 31 - is set when the request is a locked request.
// - bit 30 - is set when the request is a special request.
// - bits 29,0 - request code.
//

#define LOCKED_REQUEST		(1 << 31)
#define SPECIAL_REQUEST		(1 << 30)
#define MAX_REQUEST			(SPECIAL_REQUEST - 1)

//
// The maximum number of objects where a thread can
// simultaneously wait on.
//

#define MAXIMUM_WAIT_OBJECTS		64

//
// Internal status code.
//

#define WAIT_STATE_CHANGED		MAXIMUM_WAIT_OBJECTS

//
// Internal failure wait codes.
//

#define WAIT_PENDING			(WAIT_FAILED - 1)
#define WAIT_UNREGISTERED		(WAIT_FAILED - 2)
#define WAIT_TIMER_CANCELLED	(WAIT_FAILED - 3)

//
// Value used for when a *thread ID* is invalid.
//

#define INVALID_THREAD_ID		((ULONG)0)

//
// Average processor clocks per spin cycle.
//

#define AVERAGE_CLOCKS_PER_SPIN		10

//
// Number of spin cycles used for internal critical sections.
//

#define SHORT_CRITICAL_SECTION_SPINS	(150 / AVERAGE_CLOCKS_PER_SPIN)
#define LONG_CRITICAL_SECTION_SPINS		(500 / AVERAGE_CLOCKS_PER_SPIN)

//
// Flags used to define wait queue's discipline.
//

#define LIFO_WAIT_LIST		(1UL << 0)


/*++
 *
 * Waitable synchronizers.
 *
 --*/

//
// Virtual functions that support the Waitable synchronizers.
//
 
//
// Forward declaration for _ST_WAITABLE structure.
//

struct _ST_WAITABLE;

//
// Returns true if the synchronizer allows an immediate acquire.
//

typedef
BOOLEAN
FASTCALL
AllowsAcquireFn (
    __in struct _ST_WAITABLE *Waitable
    );

//
// Tries the acquire operation.
//

typedef
BOOLEAN
FASTCALL
TryAcquireFn (
    __inout struct _ST_WAITABLE *Waitable
    );

//
// Executes the release operation.
//

typedef
BOOLEAN
FASTCALL
ReleaseFn (
    __inout struct _ST_WAITABLE *Waitable
    );

//
// Executes the WaitAny prologue.
//

typedef
BOOLEAN
FASTCALL
WaitAnyPrologueFn (
    __inout struct _ST_WAITABLE *Waitable,
    __inout struct _ST_PARKER *Parker,
    __out PWAIT_BLOCK WaitBlock,
    __in ULONG WaitKey,
    __out ULONG *SpinCount
    );

//
// Executes the WaitAll prologue.
//

typedef
BOOLEAN
FASTCALL
WaitAllPrologueFn (
    __inout struct _ST_WAITABLE *Waitable,
    __inout struct _ST_PARKER *Parker,
    __out PWAIT_BLOCK WaitBlock,
    __out ULONG *SpinCount
    );

//
// Executes the Wait epilogue.
//

typedef
VOID
FASTCALL
WaitEpilogueFn (
    __inout struct _ST_WAITABLE *Waitable
    );

//
// Undoes a previous acquire.
//

typedef
VOID
FASTCALL
UndoAcquireFn (
    __inout struct _ST_WAITABLE *Waitable
    );

//
// Cancels an acquire attempt.
//

typedef
VOID
FASTCALL
CancelAcquireFn(
    __inout struct _ST_WAITABLE *Waitable,
    __in PWAIT_BLOCK WaitBlock
    );

//
// The virtual function table of the Waitable synchronizers.
//
 
typedef struct _WAITABLE_VTBL {
    AllowsAcquireFn *AllowsAcquire;
    TryAcquireFn *TryAcquire;
    ReleaseFn *Release;
    WaitAnyPrologueFn *WaitAnyPrologue;	
    WaitAllPrologueFn *WaitAllPrologue;	
    WaitEpilogueFn *WaitEpilogue;	
    UndoAcquireFn *UndoAcquire;
    CancelAcquireFn *CancelAcquire;
} WAITABLE_VTBL, *PWAITABLE_VTBL;

//
// Macros used to call virtual functions on waitable synchronizers.
//

#define ALLOWS_ACQUIRE(Waitable)		\
    ((*(Waitable)->Vptr->AllowsAcquire)(Waitable))

#define TRY_ACQUIRE(Waitable)		\
    ((*(Waitable)->Vptr->TryAcquire)(Waitable))

#define RELEASE(Waitable)		\
    ((*(Waitable)->Vptr->Release)(Waitable))

#define WAIT_ANY_PROLOGUE(Waitable, Parker, WaitBlock, WaitKey, SpinCount)	\
    ((*(Waitable)->Vptr->WaitAnyPrologue)(Waitable, Parker, WaitBlock, WaitKey, SpinCount))

#define WAIT_ALL_PROLOGUE(Waitable, Parker, WaitBlock, SpinCount)	\
    ((*(Waitable)->Vptr->WaitAllPrologue)(Waitable, Parker, WaitBlock, SpinCount))

#define WAIT_EPILOGUE(Waitable)	\
    if ((Waitable)->Vptr->WaitEpilogue != NULL) { \
        ((*(Waitable)->Vptr->WaitEpilogue)(Waitable)); \
    } else

#define UNDO_ACQUIRE(Waitable)		\
    if ((Waitable)->Vptr->UndoAcquire != NULL) {	\
        ((*(Waitable)->Vptr->UndoAcquire)(Waitable));	\
    } else

#define CANCEL_ACQUIRE(Waitable, WaitBlock)	\
    ((*(Waitable)->Vptr->CancelAcquire)(Waitable, WaitBlock))

//
// Returns the number of processors used by the current process.
//

ULONG
FASTCALL
GetProcessorCount (
    );

BOOLEAN
FORCEINLINE
IsMultiProcessor (
    )
{
    return GetProcessorCount() > 1;
}

/*++
 *
 * Spin wait.
 *
 --*/

//
// The Spin Wait structure.
//

typedef struct _SPIN_WAIT {
    LONG Count;
} SPIN_WAIT, *PSPIN_WAIT;

//
// Initializes the spin wait.
//

FORCEINLINE
VOID
InitializeSpinWait (
    __out PSPIN_WAIT Spin
    )
{
    Spin->Count = 0;
}

//
// Executes the specified number of iterations yielding the
// processor.
//

VOID
FASTCALL
SpinWait (
    __in LONG Iterations
    );

//
// Spins once using the specified spin wait object.
//

VOID
FASTCALL
SpinOnce (
    __out PSPIN_WAIT Spin
    );

/*++
 *
 * Virtual table implemented by the several types of locks that
 * can be used with the condition variables.
 *
 --*/

//
// Returns true if the lock is owned by the current thread.
//

typedef
BOOLEAN
FASTCALL
IsOwnedFn (
    __in PVOID Lock
    );

//
// Releases completely the lock and returns the current lock state.
//

typedef
LONG
FASTCALL
ReleaseCompletelyFn (
    __inout PVOID Lock
    );

//
// Acquires the specified lock and restores its previous state.
//

typedef
VOID
FASTCALL
ReacquireFn (
    __inout PVOID Lock,
    __in ULONG WaitStatus,
    __in LONG PreviousState
    );

//
// Enqueues a locked wait block on the lock's wait queue.
//

typedef
VOID
FASTCALL
EnqueueWaiterFn (
    __in PVOID Lock,
    __inout PWAIT_BLOCK WaitBlock
    );

//
// The virtual function table used by StLock, StCriticalSection,
// StMutex and StReadWriteLock.
//

typedef struct _ILOCK_VTBL {
    IsOwnedFn *IsOwned;
    ReleaseCompletelyFn *ReleaseCompletely;
    ReacquireFn *Reacquire;
    EnqueueWaiterFn *EnqueueWaiter;
} ILOCK_VTBL, *PILOCK_VTBL;
    
/*++
 *
 * Virtual functions implemented by the blocking queues in
 * order to support the TakeFromMultipleBlockingQueues function.
 *
 --*/

//
// Forward declarations.
//

struct _ST_BLOCKING_QUEUE;

//
// Tries to take a data item from the queue.
//

typedef
BOOLEAN
FASTCALL
TryTakeFn (
    __inout struct _ST_BLOCKING_QUEUE *Queue,
    __out PVOID *Result
    );

//
// Executes the prologue for the TakeFromMultipleBlockingQueues.
//

typedef
BOOLEAN
FASTCALL
TakePrologueFn (
    __inout struct _ST_BLOCKING_QUEUE *Queue,
    __inout struct _ST_PARKER *Parker,
    __out PWAIT_BLOCK WaitBlock,
    __in ULONG WaitKey
    );

//
// Cancels a take attempt.
//
                                               
typedef
VOID
FASTCALL
CancelTakeFn (
    __inout struct _ST_BLOCKING_QUEUE *Queue,
    __in PWAIT_BLOCK WaitBlock
    );

//
// The virtual function table for blocking queues.
//

typedef struct _BLOCKING_QUEUE_VTBL {
    TryTakeFn *TryTake;
    TakePrologueFn *TakePrologue;
    CancelTakeFn *CancelTake;
} BLOCKING_QUEUE_VTBL, *PBLOCKING_QUEUE_VTBL;

//
// Macros used to call the blocking queue's virtual functions.
//

#define TRY_TAKE(Queue, Result)		\
    (*(Queue)->Vtbl->TryTake)(Queue, Result)

#define TAKE_PROLOGUE(Queue, Parker, WaitBlock, WaitKey)		\
    (*(Queue)->Vtbl->TakePrologue)(Queue, Parker, WaitBlock, WaitKey)

#define CANCEL_TAKE(Queue, WaitBlock)		\
    (*(Queue)->Vtbl->CancelTake)(Queue, WaitBlock)
