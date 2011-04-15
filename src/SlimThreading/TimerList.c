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
// The timer list structure.
//

typedef struct _TIMER_LIST {
    SPINLOCK Lock;
    RAW_TIMER TimerListHead;
    PRAW_TIMER FiredListHead;
    RAW_TIMER LimitTimer;
    ULONG BaseTime;
    ST_PARKER Parker;
} TIMER_LIST, *PTIMER_LIST;

//
// Maximum sleep time - used by the Limit Timer.
//

#define MAXIMUM_SLEEP_TIME	30

//
// Returns the pointer to the current timer list, performing
// the initialization, if needed.
//

static
PTIMER_LIST
FASTCALL
GetTimerList (
    );

//
// Acquires the timer list's lock.
//

static
FORCEINLINE
VOID
AcquireTimerListLock (
    __inout PTIMER_LIST TimerList
    )
{
    AcquireSpinLock(&TimerList->Lock);
}

//
// Releases the timer list's lock.
//

static
FORCEINLINE
VOID
ReleaseTimerListLock (
    __inout PTIMER_LIST TimerList
    )
{
    ReleaseSpinLock(&TimerList->Lock);
}

//
// Removes a timer from the timer list.
//

static
FORCEINLINE
VOID
RemoveTimerFromList (
    __inout PRAW_TIMER Timer
    )
{
    PRAW_TIMER Next = Timer->Next;
    PRAW_TIMER Previous = Timer->Previous;

    Previous->Next = Next;
    Next->Previous = Previous;
}

//
// Inserts a timer at the tail of the timer list defined by *Head*.
//

static
FORCEINLINE
VOID
InsertTailTimerList (
    __inout PRAW_TIMER Head,
    __inout PRAW_TIMER Timer
    )
{
    PRAW_TIMER Last = Head->Previous;

    Timer->Next = Head;
    Timer->Previous = Last;
    Last->Next = Timer;
    Head->Previous = Timer;
}

//
// Inserts a timer at the head of the timer list defined by *Head*.
//

static
FORCEINLINE
VOID
InsertHeadTimerList (
    __inout PRAW_TIMER Head,
    __inout PRAW_TIMER Timer
    )
{
    PRAW_TIMER First = Head->Next;

    Timer->Next = First;
    Timer->Previous = Head;
    First->Previous = Timer;
    Head->Next = Timer;
}

//
// Adjusts the base time and remove expired timers from the list.
//


static
FORCEINLINE
VOID
AdjustBaseTime (
    __inout PTIMER_LIST TimerList
    )
{
    PRAW_TIMER TimerListHead;
    PRAW_TIMER Timer;
    ULONG Now;
    ULONG Sliding;

    TimerListHead = &TimerList->TimerListHead;
    Sliding = (Now = GetTickCount()) - TimerList->BaseTime;

    while (Sliding != 0 && (Timer = TimerListHead->Next) != TimerListHead) {
        if (Timer->Delay <= Sliding) {
            Sliding -= Timer->Delay;
            RemoveTimerFromList(Timer);
            if (Timer->Parker != NULL && TryCancelParker(Timer->Parker)) {
                        
                //
                // Add timer to fired timer list.
                //

                Timer->Previous = TimerList->FiredListHead;
                TimerList->FiredListHead = Timer;
            } else {
                Timer->Next = Timer;
            }
        } else {
            Timer->Delay -= Sliding;
            break;
        }
    }
    TimerList->BaseTime = Now;
}

//
// Adds a raw timer to the timer list.
//

FORCEINLINE
VOID
SetRawTimerWorker (
    __inout PTIMER_LIST TimerList,
    __inout PRAW_TIMER Timer,
    __in ULONG Delay
    )
{
    PRAW_TIMER TimerListHead = &TimerList->TimerListHead;
    PRAW_TIMER CurrentTimer;
    BOOLEAN WakeTimerThread;

    //
    // Set the next fire time.
    //

    Timer->FireTime = TimerList->BaseTime + Delay;

    //
    // Find the insertion position for the new timer.
    //

    CurrentTimer = TimerListHead->Next;
    do {
        if (CurrentTimer->Delay > Delay) {
            break;
        }
        Delay -= CurrentTimer->Delay;
        CurrentTimer = CurrentTimer->Next;
    } while (TRUE);

    //
    // Insert the new timer in the list, adjust the next timer and
    // redefine the delay sentinel.
    //

    Timer->Delay = Delay;
    InsertTailTimerList(CurrentTimer, Timer);
    CurrentTimer->Delay -= Delay;
    TimerList->TimerListHead.Delay = ~0;

    //
    // If the timer was inserted at front of the timer list we need
    // to wake the timer thread if it is blocked on its parker.
    //

    WakeTimerThread = (TimerListHead->Next == Timer && TryLockParker(&TimerList->Parker));

    //
    // Release the timer list lock and unpark the timer thread, if needed.
    //

    ReleaseTimerListLock(TimerList);
    if (WakeTimerThread) {
        UnparkThread(&TimerList->Parker, WAIT_SUCCESS);
    }
}

//
// Adds a raw timer to the timer list.
//

BOOLEAN
FASTCALL
SetRawTimer (
    __inout PRAW_TIMER Timer,
    __in ULONG Delay
    )
{
    PTIMER_LIST TimerList;

    //
    // Validate the *Delay* argument.
    //

    if (Delay == ~0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    //
    // Get the timer list pointer.
    //
    
    if ((TimerList = GetTimerList()) == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    //
    // Acquire the timer list lock and adjust the base time.
    //

    AcquireTimerListLock(TimerList);
    AdjustBaseTime(TimerList);

    //
    // Set the timer and return success.
    //

    SetRawTimerWorker(TimerList, Timer, Delay);
    return TRUE;
}

//
// Adds a periodic raw timer to the timer list.
//

BOOLEAN
FASTCALL
SetPeriodicRawTimer (
    __inout PRAW_TIMER Timer,
    __in ULONG Period
    )
{
    PTIMER_LIST TimerList;

    //
    // Validate the *Period* argument.
    //

    if (Period == ~0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    //
    // Get the timer list pointer.
    //
    
    if ((TimerList = GetTimerList()) == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    //
    // Acquire the timer list lock and adjust the base time.
    //

    AcquireTimerListLock(TimerList);
    AdjustBaseTime(TimerList);

    //
    // Set the relative raw timer and return success.
    //

    SetRawTimerWorker(TimerList, Timer, (Timer->FireTime + Period) - TimerList->BaseTime);
    return TRUE;
}

//
// The timer thread.
//

static
ULONG
WINAPI
TimerThread (
    __in PVOID Argument
    )
{
    PTIMER_LIST TimerList = (PTIMER_LIST)Argument;
    PRAW_TIMER TimerListHead = &TimerList->TimerListHead;
    PRAW_TIMER LimitTimer = &TimerList->LimitTimer;
    PRAW_TIMER FiredList;
    ULONG BaseTime;
    ULONG Delay;
    ULONG Sliding;

    do {

        //
        // Acquire the timer list lock and adjust the base time.
        //

        AcquireTimerListLock(TimerList);

        //
        // If the limit timer is inserted, remove it.
        //

        if (LimitTimer->Next != LimitTimer) {
            LimitTimer->Next->Delay += LimitTimer->Delay;
            TimerListHead->Delay = ~0;
            RemoveTimerFromList(LimitTimer);
        }

        //
        // Adjust base time and get expired timers.
        //

        AdjustBaseTime(TimerList);
        FiredList = TimerList->FiredListHead;
        TimerList->FiredListHead = NULL;

        //
        // Initializes the parker and compute the time at which the
        // front timer must expire.
        //

        InitializeParker(&TimerList->Parker, 1);

        //
        // If the delay of the first timeris greater than the maximum sleep
        // time insert the limit timer in the timer list.
        //

        BaseTime = TimerList->BaseTime;
        if ((Delay = TimerListHead->Next->Delay) > MAXIMUM_SLEEP_TIME) {
            LimitTimer->Delay = MAXIMUM_SLEEP_TIME;
            InsertHeadTimerList(TimerListHead, LimitTimer);
            TimerListHead->Next->Delay -= MAXIMUM_SLEEP_TIME;
            TimerListHead->Delay = ~0;
            Delay = MAXIMUM_SLEEP_TIME;
        } else {
            LimitTimer->Next = LimitTimer;
        }

        //
        // Release the timer list's lock.
        //

        ReleaseTimerListLock(TimerList);

        //
        // Call unpark method on the expired timer's parkers.
        //
    
        while (FiredList != NULL) {
            PRAW_TIMER Next = FiredList->Previous;
            UnparkThread(FiredList->Parker, WAIT_TIMEOUT);
            FiredList = Next;
        }

        //
        // Since that the timer thread can take significant time to execute
        // the timer's callbacks, we must ajust the *Delay*, if needed.
        //

        if ((Sliding = GetTickCount() - BaseTime) != 0) {
            Delay = (Sliding >= Delay) ? 0 : (Delay - Sliding);	
        }

        //
        // Park the timer thread until the next timer expires or a new
        // timer is inserted at front of the timer list.
        //

        ParkThreadEx(&TimerList->Parker, 0, Delay, NULL);
    } while (TRUE);

    //
    // We never get here!
    //

    return 0;
}

//
// Unlinks a cancelled raw timer from the timer list.
//

VOID
FASTCALL
UnlinkRawTimer (
    __inout PRAW_TIMER Timer
    )
{
    if (Timer->Next != Timer) {
        PTIMER_LIST TimerList = GetTimerList();
        AcquireTimerListLock(TimerList);
        if (Timer->Next != Timer) {
            Timer->Next->Delay += Timer->Delay;
            RemoveTimerFromList(Timer);
            TimerList->TimerListHead.Delay = ~0;
        }
        ReleaseTimerListLock(TimerList);
    }
}

//
// Tries to cancel a raw timer if it is still active.
//

BOOLEAN
FASTCALL
TryCancelRawTimer (
    __inout PRAW_TIMER Timer
    )
{
    if (TryCancelParker(Timer->Parker)) {
        UnlinkRawTimer(Timer);
        return TRUE;
    }
    return FALSE;
}

//
// Initialize timer list.
//

#define TIMER_LIST_LOCK_SPINS	200

static
PTIMER_LIST
FASTCALL
InitializeTimerList (
    __in PVOID Context
    )
{
    PTIMER_LIST TimerList = (PTIMER_LIST)Context;
    HANDLE TimerThreadHandle;

    InitializeSpinLock(&TimerList->Lock, TIMER_LIST_LOCK_SPINS);
    TimerList->TimerListHead.Next = TimerList->TimerListHead.Previous = &TimerList->TimerListHead;
    TimerList->FiredListHead = NULL;
    TimerList->LimitTimer.Next = &TimerList->LimitTimer;
    TimerList->LimitTimer.Parker = NULL;
    TimerList->BaseTime = GetTickCount();
    TimerList->TimerListHead.Delay = ~0;

    //
    // Create the timer thread.
    //

    if ((TimerThreadHandle = CreateThread(NULL, 0, TimerThread, TimerList, 0, NULL)) != NULL) {
        CloseHandle(TimerThreadHandle);
        return TimerList;
    }
    return NULL;
}

//
// Returns the timer list pointer granting one time initialization.
//

PTIMER_LIST
FASTCALL
GetTimerList (
    )
{

    static ST_INIT_ONCE_LOCK TimerInitLock;
    static TIMER_LIST TheTimerList;
    static PTIMER_LIST TimerList;

    if (TryInitOnce(&TimerInitLock, 0)) {
        PTIMER_LIST LocalTimerList = InitializeTimerList(&TheTimerList);
        if (LocalTimerList != NULL) {
            TimerList = LocalTimerList;
            InitTargetCompleted(&TimerInitLock);
        } else {
            InitTargetFailed(&TimerInitLock);
        }
        return LocalTimerList;
    }
    return TimerList;
}
