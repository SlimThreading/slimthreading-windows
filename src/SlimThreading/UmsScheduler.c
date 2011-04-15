// Copyright 2011 Carlos Martins, Duarte Nunes
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

#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

//
// UMS thread priorities.
//

#define UMS_PRIO_IDLE			0
#define UMS_PRIO_LOWEST			1
#define UMS_PRIO_BELOW_NORMAL	2
#define UMS_PRIO_NORMAL			3
#define UMS_PRIO_ABOVE_NORMAL	4
#define UMS_PRIO_HIGHEST		5
#define UMS_PRIO_TIME_CRITICAL	6
#define UMS_PRIO_LEVELS			7

//
// The UMS thread structure.
//

struct _UMS_SCHEDULER;
struct _UMS_READY_QUEUE;

typedef struct _UMS_THREAD {
    struct _UMS_THREAD *Next;
    PUMS_CONTEXT UmsContext;
    struct _UMS_SCHEDULER *Scheduler;
    struct _UMS_READY_QUEUE *ReadyQueue;
    PVOID YieldParameter;
    SHORT UmsPriority;
    SHORT WinPriority;
    ULONG ReadySetMember;
    LPTHREAD_START_ROUTINE EntryPoint;
    PVOID Argument;
} UMS_THREAD, *PUMS_THREAD;

//
// A singly-linked list of UMS thread structures.
//

typedef struct _UMS_READY_QUEUE {
    PUMS_THREAD Head;
    PUMS_THREAD Tail;
} UMS_READY_QUEUE, *PUMS_READY_QUEUE;

//
// The UMS processor structure that is associated to each
// UMS scheduler thread.
//

typedef struct _UMS_PROCESSOR {
    struct _UMS_SCHEDULER *Scheduler;
    LONG Number;
    ULONG SetMember;
    PUMS_THREAD NextThread;
    ST_PARKER Parker;
    volatile ULONG EntryIdleTime;
} UMS_PROCESSOR, *PUMS_PROCESSOR;

//
// The UMS scheduler structure.
//

typedef struct _UMS_SCHEDULER {
    SPINLOCK Lock;
    CRITICAL_SECTION CsLock;
    PUMS_COMPLETION_LIST CompletionList;
    HANDLE CompletionListEvent;
    SLIST_HEADER FreeList;
    UMS_READY_QUEUE ReadyQueues[UMS_PRIO_LEVELS];
    PUMS_THREAD SuspendedList;
    volatile ULONG LastTime;
    volatile ULONG ReadySummary;
    ULONG IdleSummary;
    volatile ULONG IdleTimeBase;
    ULONG IdleTime;
    volatile ULONG IdlePercentage;
    ULONG ProcessorCount;
    UMS_PROCESSOR Processors[];
} UMS_SCHEDULER, *PUMS_SCHEDULER;

//
// The number of spin cycles executed when the scheduler lock is
// busy, before block the acquirer thread.
//

#define SCHEDULER_LOCK_SPINS	150

//
// Timeout used when a UMS scheduler thread parks.
//

#define UMS_SCHEDULER_THREADS_TIMEOUT	30

//
// The number of times that an idle UMS scheduler thread checks the
// completion list for ready/suspended threads before it parks.
//

#define IDLE_SPIN_COUNT		50

//
// Maximum interval between idle percentage computing.
//

#define IDLE_COMPUTING_INTERVAL		250

//
// Prototype of the yield processing functions.
//

typedef VOID YIELD_PROCESSING(__inout PUMS_PROCESSOR Processor,
                              __inout PUMS_THREAD YieldedThread); 

//
// Acquires the scheduler lock.
//

FORCEINLINE
VOID
AcquireSchedulerLock (
    __inout PUMS_SCHEDULER Scheduler
    )
{
    //EnterCriticalSection(&Scheduler->CsLock);
    AcquireSpinLock(&Scheduler->Lock);
}

//
// Releases the scheduler lock.
//

FORCEINLINE
VOID
ReleaseSchedulerLock (
    __inout PUMS_SCHEDULER Scheduler
    )
{
    //LeaveCriticalSection(&Scheduler->CsLock);
    ReleaseSpinLock(&Scheduler->Lock);
}

//
// Initializes a ready queue.
//

static
FORCEINLINE
VOID
InitializeReadyQueue (
    __out PUMS_READY_QUEUE ReadyQueue
    )
{
    ReadyQueue->Head = NULL;
    ReadyQueue->Tail = (PUMS_THREAD)(&ReadyQueue->Head);
}

//
// Returns true if the ready queue is empty.
//

static
FORCEINLINE
BOOLEAN
IsReadyQueueEmpty (
    __in PUMS_READY_QUEUE ReadyQueue
    )
{
    return (ReadyQueue->Head == NULL);
}

//
// Inserts a thread at the tail of its ready queue.
//

static
VOID
InsertTailReadyQueue (
    __inout PUMS_THREAD Thread
    )
{
    PUMS_READY_QUEUE ReadyQueue = Thread->ReadyQueue;

    Thread->Next = NULL;
    ReadyQueue->Tail->Next = Thread;
    ReadyQueue->Tail = Thread;
    Thread->Scheduler->ReadySummary |= Thread->ReadySetMember;
}

//
// Inserts a thread at the front of its ready queue.
//

static
VOID
InsertHeadReadyQueue (
    __inout PUMS_THREAD Thread
    )
{
    PUMS_READY_QUEUE ReadyQueue = Thread->ReadyQueue;

    if ((Thread->Next = ReadyQueue->Head) == NULL) {
        ReadyQueue->Tail = Thread;
        Thread->Scheduler->ReadySummary |= Thread->ReadySetMember;
    }
    ReadyQueue->Head = Thread;
}

//
// Removes a thread from a non-empty ready queue.
//

static
FORCEINLINE
PUMS_THREAD
RemoveHeadReadyQueue (
    __inout PUMS_READY_QUEUE ReadyQueue
    )
{
    PUMS_THREAD Thread = ReadyQueue->Head;

    _ASSERTE(Thread != NULL);

    if ((ReadyQueue->Head = Thread->Next) == NULL) {
        ReadyQueue->Tail = (PUMS_THREAD)(&ReadyQueue->Head);
        Thread->Scheduler->ReadySummary &= ~Thread->ReadySetMember;
    }
    return Thread;
}

//
// Allocates a UMS thread structure.
//

static
FORCEINLINE
PUMS_THREAD
AllocUmsThread (
    __inout PUMS_SCHEDULER Scheduler
    )
{
    PUMS_THREAD NewUmsThread;
    if ((NewUmsThread = (PUMS_THREAD)InterlockedPopEntrySList(&Scheduler->FreeList)) != NULL) {
        return NewUmsThread;
    }
    NewUmsThread = (PUMS_THREAD)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(UMS_THREAD));
    if (NewUmsThread != NULL) {
        NewUmsThread->Scheduler = Scheduler;
    }
    return NewUmsThread;
}

//
// Frees a UMS thread structure.
//

static
FORCEINLINE
VOID
FreeUmsThread (
    __inout PUMS_THREAD UmsThread
    )
{
    InterlockedPushEntrySList(&UmsThread->Scheduler->FreeList, (PSLIST_ENTRY)UmsThread);
}

//
// Binds a UMS thread structure to a UMS thread context.
//

static
FORCEINLINE
VOID
SetUmsThread (
    __inout PUMS_CONTEXT UmsContext,
    __inout PUMS_THREAD UmsThread
    )
{
    BOOL Success;

    Success = SetUmsThreadInformation(UmsContext, UmsThreadUserContext, &UmsThread,
                                      sizeof(UmsThread));
    _ASSERTE(Success);
}

//
// Returns the UMS thread associated with a UMS thread context.
//

static
FORCEINLINE
PUMS_THREAD
GetThread (
    __in PUMS_CONTEXT UmsContext
    )
{
    PUMS_THREAD UmsThread;
    BOOL Success;
    
    Success = QueryUmsThreadInformation(UmsContext, UmsThreadUserContext, &UmsThread,
                                        sizeof(UmsThread), NULL);
    _ASSERTE(Success);
    return UmsThread;
}

//
// Returns true if the UMS thread is suspended.
//

static
FORCEINLINE
BOOLEAN
IsThreadSuspended (
    __in PUMS_THREAD Thread
    )
{
    BOOLEAN IsSuspended = 0;
    BOOL Success;
    
    Success = QueryUmsThreadInformation(Thread->UmsContext, UmsThreadIsSuspended, &IsSuspended,
                                        sizeof(IsSuspended), NULL);
    _ASSERTE(Success);
    return IsSuspended;
}

//
// Returns true if the UMS thread is terminated.
//

static
FORCEINLINE
BOOLEAN
IsThreadTerminated (
    __in PUMS_THREAD Thread
    )
{
    BOOLEAN IsTerminated;
    BOOL Success;
    
    Success = QueryUmsThreadInformation(Thread->UmsContext, UmsThreadIsTerminated, &IsTerminated,
                                        sizeof(IsTerminated), NULL);
    _ASSERTE(Success);
    return IsTerminated;
}

//
// Selects the next thread to run on the current UMS scheduler thread.
//
// NOTE: When this function is called we know that the idle summary is
//		 non-zero.
//

static
FORCEINLINE
PUMS_THREAD
SelectNextThread (
    __inout PUMS_SCHEDULER Scheduler
    )
{
    PUMS_READY_QUEUE ReadyQueue;
    ULONG Index;

    _ASSERTE(Scheduler->ReadySummary != 0);

    _BitScanReverse(&Index, Scheduler->ReadySummary);
    ReadyQueue = Scheduler->ReadyQueues + Index;
    return RemoveHeadReadyQueue(ReadyQueue);
}

//
// Checks the state of the threads inserted in the suspended list.
//
// NOTE: This function is called with the scheduler lock held.
//

static
FORCEINLINE
BOOLEAN
CheckSuspendedThreads (	
    __inout PUMS_SCHEDULER Scheduler,
    __in ULONG Now
    )
{
    PUMS_THREAD Current;
    PUMS_THREAD Next;
    BOOLEAN ReadyQueueChanged;

    //
    // Get the first suspended thread and empty the suspended list.
    //

    Current = Scheduler->SuspendedList;
    Scheduler->SuspendedList = NULL;
    ReadyQueueChanged = FALSE;

    //
    // Check each thread, moving to the ready state all threads
    // that aren't suspended.
    //

    while (Current != NULL) {
        Next = Current->Next;
        if (IsThreadSuspended(Current)) {
            Current->Next = Scheduler->SuspendedList;
            Scheduler->SuspendedList = Current;
        } else {
            InsertTailReadyQueue(Current);
            ReadyQueueChanged = TRUE;
        }
        Current = Next;
    }

    //
    // Store the current tick count and return true if at least one
    // suspended thread became ready.
    //

    Scheduler->LastTime = Now;
    return ReadyQueueChanged;
}

//
// Readies a UMS thread.
//

static
VOID
ReadyThread (
    __inout PUMS_THREAD Thread,
    __in BOOLEAN Preempted
    )
{
    PUMS_SCHEDULER Scheduler;

    //
    // Initialize the local variable.
    //

    Scheduler = Thread->Scheduler;

    //
    // Acquire the scheduler lock.
    //

    AcquireSchedulerLock(Scheduler);

    //
    // Try to dispatch the ready thread on an idle UMS scheduler thread.
    //

    while (Scheduler->IdleSummary != 0) {
        ULONG Index;
        PUMS_PROCESSOR Processor;
        _BitScanForward(&Index, Scheduler->IdleSummary);
        Processor = Scheduler->Processors + Index;
        Scheduler->IdleSummary &= ~Processor->SetMember;
        if (TryLockParker(&Processor->Parker)) {

            //
            // Compute the idle time
            //

            Scheduler->IdleTime += GetTickCount() - Processor->EntryIdleTime;
            ReleaseSchedulerLock(Scheduler);
            Processor->NextThread = Thread;
            UnparkThread(&Processor->Parker, WAIT_SUCCESS);
            return;
        }
    }

    //
    // There is no idle UMS scheduler thread. So, add the ready thread to
    // its ready queue, release the scheduler lock and return.
    // If the thread was preempted it is inserted at the front of the
    // ready queue; otherwise, it its inserted at the tail of the queue.
    //

    if (Preempted) {
        InsertHeadReadyQueue(Thread);
    } else {
        InsertTailReadyQueue(Thread);
    }
    ReleaseSchedulerLock(Scheduler);
}

//
// Gett the UMS thread's contexts that are in the completion list
// and process them.
//

static
FORCEINLINE
BOOLEAN
ProcessCompletionList (
    __inout PUMS_SCHEDULER Scheduler,
    __out BOOLEAN *LockTaken
    )
{
    PUMS_CONTEXT ContextList;
    PUMS_CONTEXT NextContext;
    PUMS_THREAD CurrentThread;
    BOOLEAN ReadyListChanged;
    BOOL Success;

    //
    // Get all the UMS thread contexts that are in the completion list.
    //

    Success = DequeueUmsCompletionListItems(Scheduler->CompletionList, 0, &ContextList);
    _ASSERTE(Success);

    //
    // If the context list isn't empty, process it.
    //

    *LockTaken = FALSE;
    ReadyListChanged = FALSE;
    if (ContextList != NULL) {
        do {
            NextContext = GetNextUmsListItem(ContextList);
            CurrentThread = GetThread(ContextList);
            if (IsThreadTerminated(CurrentThread)) {
                Success = DeleteUmsThreadContext(ContextList);
                _ASSERTE(Success);
                FreeUmsThread(CurrentThread);
            } else {
                if (!*LockTaken) {
                    *LockTaken = TRUE;
                    AcquireSchedulerLock(Scheduler);
                }
                if (IsThreadSuspended(CurrentThread)) {
                    if ((CurrentThread->Next = Scheduler->SuspendedList) == NULL) {
                        Scheduler->LastTime = GetTickCount();
                    }
                    Scheduler->SuspendedList = CurrentThread;
                } else {
                    InsertTailReadyQueue(CurrentThread);
                    ReadyListChanged = TRUE;
                }
            }
        } while ((ContextList = NextContext) != NULL);
    }
    return ReadyListChanged;
}

//
// Parks the current UMS scheduler thread.
//

static
FORCEINLINE
ULONG
ParkUmsSchedulerThread (
    __inout PUMS_SCHEDULER Scheduler,
    __inout PUMS_PROCESSOR Processor
    )
{
    ULONG SpinCount;
    PST_PARKER Parker;

    //
    // Initialize the local variable.
    //

    Parker = &Processor->Parker;

    //
    // Spin for the configured number of cycles, checking the completion list.
    //

    for (SpinCount = 0; SpinCount < IDLE_SPIN_COUNT; SpinCount++) {
        BOOLEAN LockTaken;

        //
        // If the UMS scheduler thread was already unparked, return
        // the wait status.
        //

        if (Parker->State >= 0) {
            return Parker->WaitStatus;
        }

        //
        // Check if there are UMS thread's contexts in the completion list.
        // If so, try to lock the parker and return the approptiate wait status.
        //

        if (ProcessCompletionList(Scheduler, &LockTaken)) {

            //
            // Since that there are new ready threads, try to cancel our parker.
            //

            if (TryCancelParker(Parker)) {
                Scheduler->IdleSummary &= ~Processor->SetMember;
                Processor->NextThread = SelectNextThread(Scheduler);
                ReleaseSchedulerLock(Scheduler);
                return WAIT_SUCCESS;
            }

            //
            // Our parker is already locked; So, release the scheduler lock
            // and wait until the thread is unparked.
            //

            ReleaseSchedulerLock(Scheduler);
            break;
        }
        if (LockTaken) {
            ReleaseSchedulerLock(Scheduler);
        }
    }

    //
    // Allocate a park spot to block the current thread.
    //
        
    Parker->ParkSpotHandle = AllocParkSpot();
        
    //
    // Clear the wait-in-progress bit. If this bit is already cleared,
    // the current thread was already unparked; so, free the allocated
    // park spot and return the wait status.
    //
    
    if (!InterlockedBitTestAndReset(&Parker->State, WAIT_IN_PROGRESS_BIT)) {
        FreeParkSpot(Parker->ParkSpotHandle);
        return Parker->WaitStatus;
    }

    //
    // Wait on the park spot and on the completion list event.
    //

    WaitForUmsSchedulerParkSpot(Parker, Parker->ParkSpotHandle, UMS_SCHEDULER_THREADS_TIMEOUT,
                                Scheduler->CompletionListEvent);
    
    //
    // Free the park spot and return the wait status.
    //

    FreeParkSpot(Parker->ParkSpotHandle);
    return Parker->WaitStatus;
}

//
// Waits until a ready UMS thread is available to execute on the
// current UMS scheduler thread.
//

static
FORCEINLINE
VOID
WaitForReadyThread (
    __inout PUMS_PROCESSOR Processor,
    __inout PUMS_THREAD ExecutionFailedThread,
    __in BOOLEAN LockTaken
    )
{
    PUMS_SCHEDULER Scheduler;
    ULONG Now;
    ULONG WaitStatus;

    //
    // Initialize the local variable and acquire the scheduler lock,
    // if the current thread doesn't own it.
    //

    Scheduler = Processor->Scheduler;

    if (!LockTaken) {
        AcquireSchedulerLock(Scheduler);
    }

    //
    // If we have a UMS worker thread whose execution failed, perhaps due
    // to suspension, process it according to its state.
    //

    if (ExecutionFailedThread != NULL) {
        if (IsThreadSuspended(ExecutionFailedThread)) {
            if ((ExecutionFailedThread->Next = Scheduler->SuspendedList) == NULL) {
                Scheduler->LastTime = GetTickCount();
            }
            Scheduler->SuspendedList = ExecutionFailedThread;
        } else {
            InsertTailReadyQueue(ExecutionFailedThread);
        }
    }

    //
    // Loop until we get a ready UMS worker thread to execute on the
    // current UMS scheduler thread.
    //

    do {

        //
        // If there are suspended threads and at least a tick occurred since the
        // last check, process the suspended threads.
        //

        if (Scheduler->SuspendedList != NULL && (Now = GetTickCount()) != Scheduler->LastTime) {
            CheckSuspendedThreads(Scheduler, Now);
        }

        //
        // If there are ready threads, select a thread to execute on the current
        // UMS scheduler thread, release the scheduler lock and return.
        //

        if (Scheduler->ReadySummary != 0) {
            Processor->NextThread = SelectNextThread(Scheduler);
            ReleaseSchedulerLock(Scheduler);
            return;
        }

        //
        // Initialize the processor's parker and declare the UMS scheduler
        // thread as idle.
        //

        InitializeParker(&Processor->Parker, 1);
        Processor->NextThread = NULL;
        Processor->EntryIdleTime = GetTickCount();
        Scheduler->IdleSummary |= Processor->SetMember;

        //
        // Release the scheduler lock and park the current UMS scheduler thread.
        //

        ReleaseSchedulerLock(Scheduler);
        WaitStatus = ParkUmsSchedulerThread(Scheduler, Processor);

        //
        // If someone assigned us a ready UMS thread to execute, return.
        //

        if (WaitStatus == WAIT_SUCCESS) {
            _ASSERTE(Processor->NextThread != NULL);
            return;
        }

        //
        // We get here when the timeout is expired or we were alerted
        // because the completion list's event was set.
        //

        _ASSERTE(WaitStatus == WAIT_ALERTED || WaitStatus == WAIT_TIMEOUT);

        //
        // If we were alerted, process the completion list.
        //

        LockTaken = FALSE;
        if (WaitStatus == WAIT_ALERTED) {
            if (ProcessCompletionList(Scheduler, &LockTaken)) {
                Processor->NextThread = SelectNextThread(Scheduler);

                //
                // Update the total idle time, release the scheduler lock
                // and return.
                //

                Scheduler->IdleTime += (GetTickCount() - Processor->EntryIdleTime);
                ReleaseSchedulerLock(Scheduler);
                return;
            }
        }

        //
        // If we don't own the scheduler lock, acquire it.
        //

        if (!LockTaken) {
            AcquireSchedulerLock(Scheduler);
        }

        //
        // Update the global idle time.
        //

        Scheduler->IdleTime += (GetTickCount() - Processor->EntryIdleTime);
    } while (TRUE);
}

//
// Converts a Windows relative thread priority into a UMS priority.
//

static
CHAR PriorityTranslationTable[] = {
    UMS_PRIO_IDLE,				// THREAD_PRIORITY_IDLE (-15)
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    UMS_PRIO_LOWEST,			// THREAD_PRIORITY_LOWEST (-2)
    UMS_PRIO_BELOW_NORMAL, 		// THREAD_PRIORITY_BELOW_NORMAL (-1)
    UMS_PRIO_NORMAL,			// THREAD_NORMAL (0)
    UMS_PRIO_ABOVE_NORMAL, 		// THREAD_PRIORITY_ABOVE_NORMAL (1)
    UMS_PRIO_HIGHEST, 			// THREAD_PRIORITY_HIGHEST (2)
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    UMS_PRIO_TIME_CRITICAL		// THREAD_PRIORITY_TIME_CRITICAL (15)
};

static
FORCEINLINE
SHORT
WinPriorityToUmsPriority (
    __in LONG WinPriority
    )
{
    if (WinPriority < THREAD_PRIORITY_IDLE || WinPriority > THREAD_PRIORITY_TIME_CRITICAL) {
        return -1;
    }
    return PriorityTranslationTable[WinPriority + THREAD_PRIORITY_TIME_CRITICAL];
}

//
// Yields execution of a UMS worker thread.
//

static
VOID
OnYield (
    __inout PUMS_PROCESSOR Processor,
    __inout PUMS_THREAD YieldedThread
    )
{
    PUMS_SCHEDULER Scheduler;
    ULONG Now;
    BOOLEAN CheckSuspended;
    BOOLEAN LockTaken;

    //
    // Initialize the local variable.
    //

    Scheduler = Processor->Scheduler;
    
    //
    // if there are suspended threads, check if the tick count advanced
    // since the last time we checked the state of the suspended threads.
    //

    CheckSuspended = (Scheduler->SuspendedList != NULL &&
                     (Now = GetTickCount()) != Scheduler->LastTime);

    //
    // Process the UMS thread that are in the completion list, if any.
    //

    if (ProcessCompletionList(Scheduler, &LockTaken)) {
        
        if (CheckSuspended) {
            CheckSuspendedThreads(Scheduler, Now);
        }

        //
        // Select a next thread to run on the current UMS scheduler
        // thread, but without considering the yielded thread. Then, insert
        // the yielded thread in its ready queue.
        //

        Processor->NextThread = SelectNextThread(Scheduler);
        InsertTailReadyQueue(YieldedThread);
        ReleaseSchedulerLock(Scheduler);
        return;
    }

    //
    // If there are other ready threads or we must process the suspended
    // threads, acquire the scheduler lock and select the next thread
    // to run without considering the yielded UMS thread.
    //

    if (Scheduler->ReadySummary != 0 || CheckSuspended) {
        if (!LockTaken) {
            AcquireSchedulerLock(Scheduler);
        }
        if (CheckSuspended) {
            CheckSuspendedThreads(Scheduler, Now);
        }
        if (Scheduler->ReadySummary != 0) {
            Processor->NextThread = SelectNextThread(Scheduler);
            InsertTailReadyQueue(YieldedThread);
        } 
        ReleaseSchedulerLock(Scheduler);
    } else if (LockTaken) {
        ReleaseSchedulerLock(Scheduler);
    }

    //
    // If the *Processor->NextThread* field was not affected by this
    // function, the yielded UMS worker thread will continue executing
    // on the current UMS scheduler thread.
    //
}

//
// Blocks the current UMS thread.
//
// NOTE: This function is called when the UMS scheduler thread starts,
//		 when it blocks in the kernel and when it blocks on a park spot.
//

static
FORCEINLINE
VOID
OnBlocking (
    __inout PUMS_PROCESSOR Processor
    )
{
    PUMS_SCHEDULER Scheduler;
    BOOLEAN LockTaken;
    
    //
    // Initialize the local variable.
    //

    Scheduler = Processor->Scheduler;

    //
    // Process the UMS worker threads that are in the completion list.
    //

    if (ProcessCompletionList(Scheduler, &LockTaken)) {

        //
        // We have at least a ready thread; so, select the next thread to
        // run, release the scheduler lock and return.
        //

        Processor->NextThread = SelectNextThread(Scheduler);
        ReleaseSchedulerLock(Scheduler);
        return;
    }
    
    //
    // Wait until a ready UMS worker thread is ready.
    //

    WaitForReadyThread(Processor, NULL, LockTaken);
}

//
// Called when a UMS thread blocks on a park spot.
//

static
VOID
OnWaitForParkSpot (
    __inout PUMS_PROCESSOR Processor,
    __inout PUMS_THREAD WaiterThread
    )
{
    PPARK_SPOT ParkSpot;

    //
    // Get the park spot address that was passed through the
    // *YieldParameter* field.
    //

    ParkSpot = (PPARK_SPOT)WaiterThread->YieldParameter;

    //
    // If the park spot was already set, the UMS thread continues
    // execution; otherwise it blocks.
    //
                
    if (ParkSpot->State != 0 || !CasLong(&ParkSpot->State, 0, 1)) {
        ParkSpot->State = 0;
        return;
    }
    
    //
    // Block the UMS worker thread.
    //

    OnBlocking(Processor);
}

//
// Called when a UMS worker thread sets a park spot of another
// UMS worker thread.
//
static
VOID
OnSetParkSpot (
    __inout PUMS_PROCESSOR Processor,
    __inout PUMS_THREAD YieldedThread
    )
{
    PPARK_SPOT ParkSpot;
    PUMS_THREAD ParkSpotOwner;

    //
    // The park spot address is passed through the *YieldParameter* field.
    // Get also the park spot's owner thread.
    //

    ParkSpot = (PPARK_SPOT)YieldedThread->YieldParameter;
    ParkSpotOwner = (PUMS_THREAD)ParkSpot->Context;

    //
    // If the priority of current UMS thread is greater or equal the
    // released thread, the current thread continues executing on
    // the current UMS scheduler thread. Otherwise, the current UMS
    // worker thread is preempted by the released UMS thread.
    //

    if (YieldedThread->UmsPriority >= ParkSpotOwner->UmsPriority) {  
        ReadyThread(ParkSpotOwner, FALSE);
        Processor->NextThread = YieldedThread;
    } else {
        ReadyThread(YieldedThread, TRUE);
        Processor->NextThread = ParkSpotOwner;
    }
}

//
// Called when a UMS thread changes its priority.
//

static
VOID
OnSetPriority (
    __inout PUMS_PROCESSOR Processor,
    __inout PUMS_THREAD YieldedThread
    )
{
    PUMS_SCHEDULER Scheduler;

    //
    // Initialize the local variable.
    //

    Scheduler = YieldedThread->Scheduler;

    //
    // Acquire the scheduler lock return the current thread to the
    // ready queues, select the next thread to run, release the
    // scheduler lock and return.
    //

    AcquireSchedulerLock(Scheduler);
    InsertTailReadyQueue(YieldedThread);
    Processor->NextThread = SelectNextThread(Scheduler);
    ReleaseSchedulerLock(Scheduler);
}

//
// Compute the percentage of idle time.
//

static
LONG
GetIdlePercentage (
    __inout PUMS_SCHEDULER Scheduler
    )
{
    ULONG Now;
    ULONG Elapsed;
    ULONG IdleTime;
    LONG IdlePercentage;

    AcquireSchedulerLock(Scheduler);
    Now = GetTickCount();
    if ((Elapsed = Now - Scheduler->IdleTimeBase) >= IDLE_COMPUTING_INTERVAL) {
        IdleTime = Scheduler->IdleTime / Scheduler->ProcessorCount;
        IdlePercentage = (((IdleTime >= Elapsed) ? 100 : ((IdleTime * 100) / Elapsed)) +
                           Scheduler->IdlePercentage) >> 1;
        Scheduler->IdleTime = 0;
        Scheduler->IdlePercentage = IdlePercentage;
        _WriteBarrier();
        Scheduler->IdleTimeBase = Now;
    } else {
        IdlePercentage = Scheduler->IdlePercentage;
    }
    ReleaseSchedulerLock(Scheduler);
    return IdlePercentage;
}

//
// Called when a UMS thread managed by this scheduler asks for
// the percentage of the idle time.
//

static
VOID
OnGetIdlePercentage (
    __inout PUMS_THREAD YieldedThread
    )
{
    YieldedThread->YieldParameter = (PVOID)GetIdlePercentage(YieldedThread->Scheduler);
}

//
// The UMS Scheduler entry point.
//

static
VOID
WINAPI
UmsSchedulerEntry (
    __in UMS_SCHEDULER_REASON Reason,
    __in ULONG_PTR ActivationPayload,
    __in PVOID SchedulerParam
    )
{
    PUMS_PROCESSOR Processor;
    PUMS_THREAD NextThread;
    ULONG ErrorCode;

    //
    // Get the to the processor structures associated to the current
    // UMS scheduler thread.
    //

    Processor = (PUMS_PROCESSOR)GetTebExtension()->Context;
    
    //
    // If the thread yielded, process the yield functionality.
    // Otherwise, consider that the execution a UMS worker thread
    // blocked in the kernel or that this is the first time that a UMS
    // scheduler thread calls the scheduler entry point.
    //

    if (Reason == UmsSchedulerThreadYield) {
        PUMS_THREAD YieldedThread;
        
        //
        // Get the yielded UMS thread structure.
        //

        YieldedThread = GetThread((PUMS_CONTEXT)ActivationPayload);

        //
        // Execute the appropriate yield processing.
        //

        (*((YIELD_PROCESSING *)SchedulerParam))(Processor, YieldedThread);
    } else {
        OnBlocking(Processor);
    }

    //
    // Execute the UMS worker thread selected above. This is a loop
    // because the UMS thread execution can fail due to thread suspension.
    //

    do {
        NextThread = Processor->NextThread;

        //
        // Switch to the selected UMS worker thread context.
        //

        do {
            ExecuteUmsThread(NextThread->UmsContext);
            if ((ErrorCode = GetLastError()) != ERROR_RETRY) {
                break;
            }
        } while (TRUE);

        //
        // The execution can fail because the UMS thread was suspended,
        // terminated or due to bad luck.
        //

        if (IsThreadTerminated(NextThread)) {
            BOOL Success = DeleteUmsThreadContext(NextThread->UmsContext);
            _ASSERTE(Success);
            FreeUmsThread(NextThread);
            NextThread = NULL;
        }

        //
        // Select the new thread to run on the UMS scheduler thread
        // and try to execute it.
        //

        WaitForReadyThread(Processor, NextThread, FALSE);
    } while(TRUE);
}

/*++
 *
 * The UMS worker thread park spot API.
 *
 --*/

//
// Sets the UMS park spot.
//

VOID
FASTCALL
SetUmsWorkerParkSpot (
    __inout PPARK_SPOT ParkSpot
    )
{

    //
    // If the thread owner of the park spot isn't still waiting, set
    // the *State* field and return. Otherwise, we must to ready the
    // UMS worker thread.
    //

    if (ParkSpot->State != 0 || !CasLong(&ParkSpot->State, 0, 1)) {

        //
        // If the current thread is a UMS worker thread managed by this
        // scheduler, we must yield execution in order to get the UMS
        // scheduler thread identity to prevent deadlock when acquiring
        // the scheduler lock, when the current UMS thread, for some reason,
        // blocks in the kernel. Otherwise, ready the park spot's owner
        // thread.
        //

        ParkSpot->State = 0;
        if (IsUmsWorkerThread()) {
            PUMS_THREAD CurrentUmsThread = (PUMS_THREAD)(GetTebExtension()->Context);
            
            CurrentUmsThread->YieldParameter = ParkSpot;
            UmsThreadYield(OnSetParkSpot);
        } else {
            ReadyThread((PUMS_THREAD)ParkSpot->Context, FALSE);
        }
    }
}

//
// Waits on a UMS park spot.
//

VOID
FASTCALL
WaitForUmsWorkerParkSpot (
    __inout PPARK_SPOT ParkSpot
    )
{
    PUMS_THREAD CurrentThread;
    
    if (ParkSpot->State != 0) {
        ParkSpot->State = 0;
        return;
    }
    
    CurrentThread = (PUMS_THREAD)(GetTebExtension()->Context);
    CurrentThread->YieldParameter = ParkSpot;
    UmsThreadYield(OnWaitForParkSpot);
}

//
// The UMS scheduler thread.
//

static
ULONG
WINAPI
UmsSchedulerThread (
    __in PVOID Argument
    )
{
    UMS_SCHEDULER_STARTUP_INFO SchedulerStartupInfo;
    PUMS_PROCESSOR Processor;

    Processor = (PUMS_PROCESSOR)Argument;

    SetThreadIdealProcessor(GetCurrentThread(), Processor->Number);

    //
    // Set the *Context" field of the TEB extension to point to the
    // UMS processor structure.
    //

    GetTebExtension()->Context = Processor;

    //
    // Format the scheduler startup information and enter in the
    // UMS scheduler thread mode.
    //

    SchedulerStartupInfo.UmsVersion = UMS_VERSION;
    SchedulerStartupInfo.CompletionList = Processor->Scheduler->CompletionList;
    SchedulerStartupInfo.SchedulerProc = UmsSchedulerEntry;
    SchedulerStartupInfo.SchedulerParam = NULL;
    EnterUmsSchedulingMode(&SchedulerStartupInfo);

    //
    // TODO : UMS scheduler shutdown. Meanwhile, we should never get here.
    //
            
    _ASSERT(!"***FATAL ERROR: The UMS scheduler threads can't return from"
            " the UMS scheduling mode\n");
    DebugBreak();
    return 0;
}

//
// Initializes the UMS scheduler.
//

static
PUMS_SCHEDULER
FASTCALL
InitializeUmsScheduler (
    )
{
    PUMS_SCHEDULER Scheduler;
    PUMS_PROCESSOR Processor;
    ULONG_PTR ProcessAffinityMask;
    ULONG_PTR SystemAffinityMask;
    ULONG Index;
    BOOL Success;
    SYSTEM_INFO SystemInfo;

    //
    // Get the total number of processors in the system.
    //

    GetSystemInfo(&SystemInfo);

    //
    // Allocate memory to the UMS scheduler structure.
    //

    Scheduler = (PUMS_SCHEDULER)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                          sizeof(UMS_SCHEDULER) +
                                          SystemInfo.dwNumberOfProcessors * sizeof(UMS_PROCESSOR));

    if (Scheduler == NULL) {
        return NULL;
    }

    //
    // Initialize the scheduler lock.
    //

    InitializeSpinLock(&Scheduler->Lock, SCHEDULER_LOCK_SPINS);
    InitializeCriticalSection(&Scheduler->CsLock);

    //
    // Create the UMS completion list and get the handle of the
    // completion list event.
    //

    Success = CreateUmsCompletionList(&Scheduler->CompletionList);
    _ASSERTE(Success);
    Success = GetUmsCompletionListEvent(Scheduler->CompletionList,
                                        &Scheduler->CompletionListEvent);
    _ASSERTE(Success);

    //
    // Initialize the ready queues.
    //

    for (Index = 0; Index < UMS_PRIO_LEVELS; Index++) {
        InitializeReadyQueue(Scheduler->ReadyQueues + Index);
    }

    //
    // Initialize the fields related to idle time computing.
    //

    Scheduler->IdleTimeBase = GetTickCount() - (IDLE_COMPUTING_INTERVAL + 1);

    //
    // Create as many UMS scheduler threads as the number of processors
    // that are being used by the current process.
    //

    Success = GetProcessAffinityMask(GetCurrentProcess(), &ProcessAffinityMask,
                                     &SystemAffinityMask);
    _ASSERTE(Success);

    //ProcessAffinityMask = (1 << 0);

    for (Index = 0; Index < SystemInfo.dwNumberOfProcessors; Index++) {
        Processor = Scheduler->Processors + Index;
        Processor->Number = Index;
        Processor->SetMember = 1 << Index;
        Processor->Scheduler = Scheduler;
        if ((ProcessAffinityMask & (((ULONG_PTR)1) << Index)) != 0) {
            HANDLE SchedThreadHandle = CreateThread(NULL, 0, UmsSchedulerThread, Processor,
                                                    0, NULL);
            _ASSERT(SchedThreadHandle != NULL);
            CloseHandle(SchedThreadHandle);
            Scheduler->ProcessorCount++;
        }
    }

    //
    // Return the initialized UMS scheduler.
    //

    return Scheduler;
}

//
// Returns the pointer to the UMS scheduler ensuring
// initialization.
//

PUMS_SCHEDULER
FASTCALL
GetUmsScheduler (
    )
{

    static ST_INIT_ONCE_LOCK UmsInitLock;
    static PUMS_SCHEDULER TheUmsScheduler;

    if (TryInitOnce(&UmsInitLock, 0)) {
        PUMS_SCHEDULER LocalUmsScheduler = InitializeUmsScheduler();
        if (LocalUmsScheduler != NULL) {
            TheUmsScheduler = LocalUmsScheduler;
            InitTargetCompleted(&UmsInitLock);
        } else {
            InitTargetFailed(&UmsInitLock);
        }
        return LocalUmsScheduler;
    }
    return TheUmsScheduler;
}

//
// The first entry point of the UMS the worker threads.
//

static
ULONG
WINAPI
UmsWorkerThreadEntry (
    __in  PVOID Argument
    )
{
    PUMS_THREAD UmsThread;
    PTEB_EXTENSION TebExtension;

    //
    // The UMS thread structure is passed through the thread's argument.
    //

    UmsThread = (PUMS_THREAD)Argument;

    //
    // Ensure that the UMS worker thread gets the appropriate TEB Extension;
    // set the *Context* field of the TEB extension to point to the UMS
    // thread structure and transfer the control to the user defined entry point.
    //

    TebExtension = EnsureUmsThreadTebExtension();
    TebExtension->Context = UmsThread;
    return (*UmsThread->EntryPoint)(UmsThread->Argument);
}

//
// Create a UMS worker thread.
//

HANDLE
WINAPI
StUmsThread_Create (
    __in LPSECURITY_ATTRIBUTES ThreadAttributes,
    __in SIZE_T StackSize,
    __in LPTHREAD_START_ROUTINE EntryPoint,
    __in_opt LPVOID Argument,
    __in ULONG CreationFlags,
    __out_opt PULONG ThreadId
    )
{
    LPPROC_THREAD_ATTRIBUTE_LIST AttributeList;
    SIZE_T AttributeListSize;
    PUMS_CONTEXT UmsContext;
    PUMS_THREAD UmsThread;
    PUMS_SCHEDULER Scheduler;
    UMS_CREATE_THREAD_ATTRIBUTES UmsAttributes;
    HANDLE UmsThreadHandle;
    BOOL Success;

    //
    // Get the pointer to the UMS Scheduler.
    //

    if ((Scheduler = GetUmsScheduler()) == NULL) {		
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    //
    // Create the UMS worker thread context.
    //

    if (!CreateUmsThreadContext(&UmsContext)) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    //
    // Allocate a UMS thread structure.
    //
    
    if ((UmsThread = AllocUmsThread(Scheduler)) == NULL) {
        DeleteUmsThreadContext(UmsContext);		
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    //
    // Format the UMS thread structure and bind it to the UMS context.
    //

    UmsThread->UmsContext = UmsContext;
    UmsThread->WinPriority = THREAD_PRIORITY_NORMAL;
    UmsThread->UmsPriority = UMS_PRIO_NORMAL;
    UmsThread->ReadySetMember = (1 << UMS_PRIO_NORMAL);
    UmsThread->ReadyQueue = UmsThread->Scheduler->ReadyQueues + UMS_PRIO_NORMAL;
    UmsThread->EntryPoint = EntryPoint;
    UmsThread->Argument = Argument;
    SetUmsThread(UmsContext, UmsThread);

    //
    // Create the process thread attribute list.
    //

    AttributeListSize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &AttributeListSize);
    AttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)
                            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, AttributeListSize);
    if (AttributeList == NULL ||
        InitializeProcThreadAttributeList(AttributeList, 1, 0, &AttributeListSize) == FALSE) {
        DeleteUmsThreadContext(UmsContext);
        FreeUmsThread(UmsThread);
        HeapFree(GetProcessHeap(), 0, AttributeList);
        return NULL;
    }

    //
    // Format the UMS thread attribute and, if succeed, create the
    // UMS worker thread.
    //

    UmsAttributes.UmsVersion = UMS_VERSION;
    UmsAttributes.UmsContext = UmsContext;
    UmsAttributes.UmsCompletionList = Scheduler->CompletionList;
    UmsThreadHandle = NULL;
    Success = UpdateProcThreadAttribute(AttributeList, 0,
                                        PROC_THREAD_ATTRIBUTE_UMS_THREAD,
                                        &UmsAttributes,
                                        sizeof(UMS_CREATE_THREAD_ATTRIBUTES),
                                        NULL, NULL) && 
              (UmsThreadHandle = CreateRemoteThreadEx(GetCurrentProcess(),
                                                      ThreadAttributes,
                                                      StackSize,
                                                      UmsWorkerThreadEntry,
                                                      UmsThread,
                                                      CreationFlags,
                                                      AttributeList,
                                                      ThreadId)) != NULL;

    //
    // If the create thread failed, release the UMS thread context
    // and update the number of UMS threads.
    //

    if (!Success) {
        DeleteUmsThreadContext(UmsContext);
        FreeUmsThread(UmsThread);
    }

    //
    // Delete the process thread attribute list, free the
    // underlying memory and return the new UMS worker thread's handle.
    //

    DeleteProcThreadAttributeList(AttributeList);
    HeapFree(GetProcessHeap(), 0, AttributeList);
    return Success ? UmsThreadHandle : NULL;
}

//
// Yields execution of the current thread.
//

VOID
WINAPI
StThread_Yield (
    )
{
    if (IsUmsWorkerThread()) {
        UmsThreadYield(OnYield);
    } else {
        SwitchToThread();
    }
}

//
// Sets the priority of the current UMS thead.
//

BOOL
WINAPI
StUmsThread_SetCurrentThreadPriority (
    __in LONG NewWinPriority
    )
{
    SHORT NewUmsPriority;
    PUMS_THREAD CurrentThread;

    if (!IsUmsWorkerThread() || (NewUmsPriority = WinPriorityToUmsPriority(NewWinPriority)) < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    CurrentThread = (PUMS_THREAD)(GetTebExtension()->Context);
    CurrentThread->WinPriority = (SHORT)NewWinPriority;
    CurrentThread->UmsPriority = NewUmsPriority;
    CurrentThread->ReadySetMember = (1 << NewUmsPriority);
    CurrentThread->ReadyQueue = CurrentThread->Scheduler->ReadyQueues + NewUmsPriority;
    UmsThreadYield(OnSetPriority);
    return TRUE;
}

//
// Returns the priority of the current UMS thead.
//

LONG
WINAPI
StUmsThread_GetCurrentThreadPriority (
    )
{

    if (!IsUmsWorkerThread()) {
        return GetThreadPriority(GetCurrentThread);
    }

    return ((PUMS_THREAD)(GetTebExtension()->Context))->WinPriority;
}

//
// Returns true meaning that the UMS scheduler is available.
//

BOOL
WINAPI
StUmsScheduler_IsAvailable (
    )
{
    return TRUE;
}

//
// Returns the percentage of the idle time.
//

LONG
WINAPI
StUmsScheduler_GetIdlePercentage (
    )
{
    PUMS_SCHEDULER Scheduler;

    //
    // Get the current UMS scheduler.
    //

    Scheduler = GetUmsScheduler();

    //
    // If we have a reasonable value for the idle percentage, return it.
    //

    if ((GetTickCount() - Scheduler->IdleTimeBase) < IDLE_COMPUTING_INTERVAL) {
        return Scheduler->IdlePercentage;
    }

    //
    // Compute the percentage of idle time, and return it.
    //

    if (IsUmsWorkerThread()) {
        PUMS_THREAD CurrentUmsThread = (PUMS_THREAD)(GetTebExtension()->Context);
            
        UmsThreadYield(OnGetIdlePercentage);
        return (LONG)CurrentUmsThread->YieldParameter;
    }
    return GetIdlePercentage(Scheduler);
}

//
// Allocates a park spot to block the current UMS worker thread.
//

PVOID
WINAPI
StUmsThread_AllocParkSpot (
    )
{
    return AllocParkSpot();
}

//
// Frees a park spot previously allocated by the current UMS worker thread.
//

VOID
WINAPI
StUmsThread_FreeParkSpot (
    __inout PVOID ParkSpot
    )
{
    FreeParkSpot((PPARK_SPOT)ParkSpot);
}

//
// Wait on the specified UMS worker thread´s park spot.
//

BOOL
WINAPI
StUmsThread_WaitForParkSpot (
    __inout PVOID ParkSpot
    )
{
    WaitForUmsWorkerParkSpot((PPARK_SPOT)ParkSpot);
    return TRUE;
}

//
// Sets the specified UMS worker thread´s park spot.
//

STAPI
BOOL
WINAPI
StUmsThread_SetParkSpot (
    __inout PVOID ParkSpot
    )
{
    SetUmsWorkerParkSpot((PPARK_SPOT)ParkSpot);
    return TRUE;
}

#else

//
// Returns false meaning that the UMS scheduler isn't available.
//

BOOL
WINAPI
StUmsScheduler_IsAvailable (
    )
{
    return FALSE;
}

//
// Returns true if the current thread is a UMS worker thread.
//

BOOL
WINAPI
StUmsScheduler_IsUmsThread (
    )
{
    return FALSE;
}

#endif
