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
// Macros used with the *State* field.
//

#define INACTIVE			((PST_PARKER)2)
#define ACTIVE				((PST_PARKER)((ULONG_PTR)~1))
#define BUSY(p)				((PST_PARKER)(((ULONG_PTR)(p)) | 3))
#define StateIsBusy(s)		((((ULONG_PTR)(s)) & 1) != 0)
#define StateIsParker(s)	((((ULONG_PTR)(s)) & 3) == 0)

#define BUSY_SPINS		100

//
// Executes the unpark callback.
//

static
VOID
CALLBACK
UnparkCallback (
    __inout_opt PVOID Context,
    __in ULONG WaitStatus
    )
{
    PST_REGISTERED_WAIT RegisteredWait;
    PST_PARKER OldState;
    PST_PARKER MyBusy;
    ULONG Ignored;

    _ASSERTE(WaitStatus == WAIT_SUCCESS || WaitStatus == WAIT_TIMEOUT ||
             WaitStatus == WAIT_UNREGISTERED);

    //
    // If the registered wait was disposed (externally unregistered), cancel
    // the acquire attempt and return immediately.
    //

    RegisteredWait = (PST_REGISTERED_WAIT)Context;
    if (WaitStatus == WAIT_UNREGISTERED) {
        CANCEL_ACQUIRE(RegisteredWait->Waitable, &RegisteredWait->WaitBlock);
        return;
    }

    //
    // Set state to *our busy* grabing the current state and execute
    // the unpark callback processing.
    //

    MyBusy = BUSY(&MyBusy);
    OldState = (PST_PARKER)InterlockedExchangePointer(&RegisteredWait->State, MyBusy);

    //
    // ...
    //

    do {

        //
        // If the acquire operation was cancelled, cancel the acquire
        // attempt on the waitable.
        //

        if (WaitStatus != WAIT_SUCCESS) {
            CANCEL_ACQUIRE(RegisteredWait->Waitable, &RegisteredWait->WaitBlock);
        }

        //
        // Execute the user callback routine.
        //

        RegisteredWait->ThreadId = GetCurrentThreadId();
        (*RegisteredWait->UserCallback)(RegisteredWait->UserCallbackContext, WaitStatus);
        RegisteredWait->ThreadId = 0;

        //
        // If the *ExecuteOnlyOnce* is true or there is an unregister
        // in progress, set the state to INACTIVE. If any thread is waiting,
        // unpark it.
        //

        if (RegisteredWait->ExecuteOnlyOnce != FALSE || StateIsParker(OldState)) {
            if (StateIsParker(OldState)) {
                UnparkThread(OldState, WAIT_SUCCESS);
            } else {
                RegisteredWait->State = INACTIVE;
            }
            return;
        }

        //
        // We must re-register with the Waitable.
        // So, initialize the parker and execute the WaitAny prologue.
        //

        InitializeParker(&RegisteredWait->CbParker.Parker, 1);
        WAIT_ANY_PROLOGUE(RegisteredWait->Waitable, &RegisteredWait->CbParker.Parker,
                          &RegisteredWait->WaitBlock, WAIT_SUCCESS, &Ignored);

        //
        // Enable the unpark callback.
        //

        if ((WaitStatus = EnableUnparkCallback(&RegisteredWait->CbParker, RegisteredWait->Timeout,
                                               &RegisteredWait->RawTimer)) == WAIT_PENDING) {

            //
            // If the *State* field constains still our *busy* set it to ACTIVE.
            //

            if (RegisteredWait->State == MyBusy) {
                CasPointer(&RegisteredWait->State, MyBusy, ACTIVE);
            }
            return;
        }

        //
        // The waitable was already signalled. So, execute the unpark
        // callback inline.
        //

    } while (TRUE);
}

//
// Registers a wait with a Waitable synchronizer.
//

BOOL
WINAPI
StWaitable_RegisterWait (
    __inout PVOID Waitable,
    __inout PST_REGISTERED_WAIT RegisteredWait,
    __in ST_WAIT_OR_TIMER_CALLBACK *UserCallback,
    __in_opt PVOID UserCallbackContext,
    __in ULONG Timeout,
    __in BOOL ExecuteOnlyOnce
    )
{
    PST_WAITABLE TheWaitable;
    ULONG WaitStatus;
    ULONG Ignored;

    //
    // Validate the arguments.
    //

    TheWaitable = (PST_WAITABLE)Waitable;
    if (Timeout == 0 || UserCallback == NULL ||
        (IsNotificationEvent(TheWaitable) && ExecuteOnlyOnce == FALSE) ||
        IsReentrantLock(TheWaitable)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    //
    // Initialize the registered wait object.
    //

    InitializeRawTimer(&RegisteredWait->RawTimer, &RegisteredWait->CbParker.Parker);
    InitializeCallbackParker(&RegisteredWait->CbParker, UnparkCallback, RegisteredWait);
    RegisteredWait->Waitable = TheWaitable;
    RegisteredWait->UserCallback = UserCallback;
    RegisteredWait->UserCallbackContext = UserCallbackContext;
    RegisteredWait->Timeout = Timeout;
    RegisteredWait->ExecuteOnlyOnce = ExecuteOnlyOnce;
    RegisteredWait->ThreadId = 0;

    //
    // Initialize the registered wait's parker and execute the WaitAny
    // prologue.
    //

    InitializeParker(&RegisteredWait->CbParker.Parker, 1);
    WAIT_ANY_PROLOGUE(TheWaitable, &RegisteredWait->CbParker.Parker,
                      &RegisteredWait->WaitBlock, WAIT_SUCCESS, &Ignored);

    //
    // Set the registered to active and enable the unpark callback.
    //

    RegisteredWait->State = ACTIVE;
    if ((WaitStatus = EnableUnparkCallback(&RegisteredWait->CbParker, Timeout,
                            &RegisteredWait->RawTimer)) != WAIT_PENDING) {

        //
        // The acquire operation was already accomplished. So, execute the
        // unpark callback inline.
        //

        UnparkCallback(RegisteredWait, WaitStatus);
    }
    return TRUE;
}

//
// Unregisters the registered wait.
//

BOOL
WINAPI
StRegisteredWait_Unregister (
    __inout PST_REGISTERED_WAIT RegisteredWait
    )
{
    ST_PARKER Parker;
    PST_PARKER OldState;
    SPIN_WAIT Spinner;


    //
    // If the wait is being unregistered from the user callback function,
    // we just set the *ExecuteOnlyOnce* to TRUE and return.
    //

    if (RegisteredWait->ThreadId == GetCurrentThreadId()) {
        RegisteredWait->ExecuteOnlyOnce = TRUE;
        return TRUE;
    }

    //
    // If the wait is registered, we must try to unregister it and return
    // only after the wait is actually unregistered.
    // If the wait was already unregistered or an unregister is taking place,
    // we return FALSE. 
    //

    InitializeSpinWait(&Spinner);
    InitializeParker(&Parker, 0);
    do {
        if ((OldState = RegisteredWait->State) == INACTIVE) {
            return FALSE;
        }
        if (OldState == ACTIVE) {
            if (CasPointer(&RegisteredWait->State, ACTIVE, &Parker)) {
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
    // Try to cancel the wait's callback parker. If we succeed, call
    // the unpark with status disposed. Otherwise, a callback is taking
    // place and we must synchronize with its end.
    //

    if (TryCancelParker(&RegisteredWait->CbParker.Parker)) {
        UnparkThread(&RegisteredWait->CbParker.Parker, WAIT_UNREGISTERED);
    } else {
        ParkThreadEx(&Parker, BUSY_SPINS, INFINITE, NULL);
    }

    //
    // Set the registered wait object to inactive and return success.
    //

    RegisteredWait->State = INACTIVE;
    return TRUE;
}
