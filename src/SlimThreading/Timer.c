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
// Macros used with the Timer's *State* field.
//

#define INACTIVE			((PST_PARKER)2)
#define ACTIVE				((PST_PARKER)((ULONG_PTR)~1))
#define SETTING				((PST_PARKER)((ULONG_PTR)~0))
#define BUSY(p)				((PST_PARKER)(((ULONG_PTR)(p)) | 3))
#define StateIsBusy(s)		((((ULONG_PTR)(s)) & 1) != 0)
#define StateIsParker(s)	((((ULONG_PTR)(s)) & 3) == 0)

//
// Executes the timer callback
//

static
VOID
CALLBACK
TimerCallback (
    __inout_opt PVOID Context,
    __in ULONG WaitStatus
    )
{
    PST_TIMER Timer;
    PST_PARKER OldState;
    PST_PARKER MyBusy;
    ULONG Timeout;

    _ASSERTE(WaitStatus == WAIT_TIMEOUT || WaitStatus == WAIT_TIMER_CANCELLED);

    //
    // If the timer was disposed, return immediately.
    //

    if (WaitStatus == WAIT_TIMER_CANCELLED) {
        return;
    }

    Timer = (PST_TIMER)Context;

    //
    // Set timer state to *our busy* state, grabing the current state and
    // execute the timer callback processing.
    //

    MyBusy = BUSY(&MyBusy);
    OldState = (PST_PARKER)InterlockedExchangePointer(&Timer->State, MyBusy);

    //
    // ...
    //

    do {

        //
        // Signals the timer's event.
        //

        StWaitable_Signal(&Timer->NotEvent);
        
        //
        // Call the user-defined callback, if specified.
        //

        if (Timer->UserCallback != NULL) {
            Timer->ThreadId = GetCurrentThreadId();
            (*Timer->UserCallback)(Timer->UserCallbackContext, WAIT_TIMEOUT);
            Timer->ThreadId = 0;
        }

        //
        // If the  timer isn't periodic or if someone is trying to cancel it,
        // process cancellation.
        //

        if (Timer->Period == 0 || StateIsParker(OldState)) {
            if (StateIsParker(OldState)) {
                UnparkThread(OldState, WAIT_SUCCESS);
            } else {
                Timer->State = INACTIVE;
            }
            return;
        }

        //
        // Initialize the timer's parker.
        //

        InitializeParker(&Timer->CbParker.Parker, 1);

        //
        // Compute the timer delay and enable the unpark callback.
        //

        if (Timer->SetWithDueTime) {
            Timeout = Timer->DueTime;
            Timer->SetWithDueTime = FALSE;
        } else {
            Timeout = Timer->Period | (1 << 31);
        }
        if ((WaitStatus = EnableUnparkCallback(&Timer->CbParker, Timeout,
                                               &Timer->RawTimer)) == WAIT_PENDING) {
            if (Timer->State == MyBusy) {
                CasPointer(&Timer->State, MyBusy, ACTIVE);
            }
            return;
        }

        //
        // The timer already expired. So, execute the timer callback inline.
        //

    } while (TRUE);
}

//
// Sets the timer.
//

BOOL
WINAPI
StTimer_Set (
    __inout PST_TIMER Timer,
    __in ULONG DueTime,
    __in LONG Period,
    __in_opt ST_WAIT_OR_TIMER_CALLBACK *Callback,
    __in_opt PVOID Context
    )
{
    ST_PARKER Parker;
    PST_PARKER OldState;
    SPIN_WAIT Spinner;
    ULONG WaitStatus;

    //
    // If the timer is being set from the user callback function,
    // we just save the new settings and return success.
    //

    if (Timer->ThreadId == GetCurrentThreadId()) {
        Timer->DueTime = DueTime;
        Timer->Period = Period;
        Timer->SetWithDueTime = TRUE;
        Timer->UserCallback = Callback;
        Timer->UserCallbackContext = Context;
        return TRUE;
    }

    //
    // The timer is being set externally. So, we must first cancel the
    // timer. If the timer is already being set or cancelled, we return
    // failure.
    //

    InitializeSpinWait(&Spinner);
    InitializeParker(&Parker, 0);
    do {
        if ((OldState = Timer->State) == INACTIVE) {
            if (CasPointer(&Timer->State, INACTIVE, SETTING)) {
                goto SetTimer;
            }
            continue;
        }
        if (OldState == ACTIVE) {
            if (CasPointer(&Timer->State, ACTIVE, &Parker)) {
                break;
            }
            continue;
        }
        if (!StateIsBusy(OldState)) {
            return FALSE;
        }
        SpinOnce(&Spinner);
    } while (TRUE);

    //
    // Try to cancel the timer's callback parker. If succeed, call
    // the unpark method; otherwise, the timer callback is taking
    // place, so we must synchronize with its end.
    //

    if (TryCancelParker(&Timer->CbParker.Parker)) {
        UnparkThread(&Timer->CbParker.Parker, WAIT_TIMER_CANCELLED);
    } else {
        ParkThreadEx(&Parker, 100, INFINITE, NULL);
    }

    //
    // Here, we know that the timer is cancelled and no one else
    // is trying to set or cancel the timer.
    // So, set the timer state to *our busy state* and start it with
    // the new settings.
    //

    _ASSERTE(Timer->State == &Parker);
    Timer->State = SETTING;

SetTimer:

    Timer->DueTime = DueTime;
    Timer->Period = Period;
    Timer->SetWithDueTime = FALSE;
    Timer->UserCallback = Callback;
    Timer->UserCallbackContext = Context;
    if (IsNotificationEvent(&Timer->NotEvent.Waitable)) {
        StNotificationEvent_Reset(&Timer->NotEvent);
    } else {
        StSynchronizationEvent_Reset(&Timer->SyncEvent);
    }

    //
    // Initialize the timer's parker, set the timer state to ACTIVE and
    // enable the unpark callback.
    //

    InitializeParker(&Timer->CbParker.Parker, 1);
    Timer->State = ACTIVE;
    if ((WaitStatus = EnableUnparkCallback(&Timer->CbParker, DueTime,
                                           &Timer->RawTimer)) != WAIT_PENDING) {
        
        //
        // If the timer already fired or cancelled, call the unpark
        // callback inline.
        //

        TimerCallback(Timer, WaitStatus);
    }
    return TRUE;
}

//
// Cancels the timer.
//

BOOL
WINAPI
StTimer_Cancel (
    __inout PST_TIMER Timer
    )
{
    ST_PARKER Parker;
    PST_PARKER OldState;
    SPIN_WAIT Spinner;

    //
    // If the timer is being cancelled from the user callback function,
    // set the period to zero and return success. The timer will be
    // cancelled on return from the callback function.
    //

    if (Timer->ThreadId == GetCurrentThreadId()) {
        Timer->Period = 0;
        return TRUE;
    }

    //
    // This is an external cancellation.
    // So, if the timer is active we must try to cancel the timer
    // and return only after the timer is actually cancelled.
    // if the timer is already inactive or a cancel or a set is taking
    // place, this function returns FALSE. 
    //

    InitializeSpinWait(&Spinner);
    InitializeParker(&Parker, 0);
    do {
        if ((OldState = Timer->State) == INACTIVE) {
            return FALSE;
        }
        if (OldState == ACTIVE) {
            if (CasPointer(&Timer->State, ACTIVE, &Parker)) {
                break;
            }
            continue;
        }
        if (!StateIsBusy(OldState)) {
            return FALSE;
        }
        SpinOnce(&Spinner);
    } while (TRUE);

    //
    // Try to cancel the timer's callback parker.
    //

    if (TryCancelParker(&Timer->CbParker.Parker)) {
        UnparkThread(&Timer->CbParker.Parker, WAIT_TIMER_CANCELLED);
    } else {
        ParkThreadEx(&Parker, 100, INFINITE, NULL);
    }

    //
    // Set the timer state to inactive and return success.
    //

    Timer->State = INACTIVE;
    return TRUE;
}

//
// Initializes the timer.
//

VOID
WINAPI
StTimer_Init (
    __out PST_TIMER Timer,
    __in BOOL NotificationTimer
    )
{

    ZeroMemory(Timer, sizeof(*Timer));

    //
    // Initialize the associated event.
    //

    if (NotificationTimer) {
        StNotificationEvent_Init(&Timer->NotEvent, FALSE, 0);
    } else {
        StSynchronizationEvent_Init(&Timer->SyncEvent, FALSE, 0);
    }

    //
    // Initialize the raw timer and the callback parker.
    //

    InitializeRawTimer(&Timer->RawTimer, &Timer->CbParker.Parker);
    InitializeCallbackParker(&Timer->CbParker, TimerCallback, Timer);
    Timer->State = INACTIVE;
}
