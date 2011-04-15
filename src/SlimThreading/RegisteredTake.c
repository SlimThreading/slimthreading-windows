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

#define INACTIVE				((PST_PARKER)2)
#define ACTIVE					((PST_PARKER)((ULONG_PTR)~1))
#define BUSY(p)					((PST_PARKER)(((ULONG_PTR)(p)) | 3))
#define StateIsBusy(s)			((((ULONG_PTR)(s)) & 1) != 0)
#define StateIsParker(s)		((((ULONG_PTR)(s)) & 3) == 0)

#define BUSY_SPINS		100

//
// The unpark callback worker.
//

//
// Entry for the unpark callback.
//

static
VOID
CALLBACK
UnparkCallback (
    __inout_opt PVOID Context,
    __in ULONG WaitStatus
    )
{
    PST_REGISTERED_TAKE RegisteredTake;
    PST_PARKER OldState;
    PST_PARKER MyBusy;

    _ASSERTE(WaitStatus == WAIT_SUCCESS || WaitStatus == WAIT_TIMEOUT ||
             WaitStatus == WAIT_DISPOSED || WaitStatus == WAIT_UNREGISTERED);

    //
    // If the registered wait was disposed (externally unregistered), cancel
    // the acquire attempt and return immediately.
    //

    RegisteredTake = (PST_REGISTERED_TAKE)Context;
    if (WaitStatus == WAIT_UNREGISTERED) {
        CANCEL_TAKE(RegisteredTake->Queue, &RegisteredTake->WaitBlock);
        return;
    }

    //
    // Set state to *our busy* grabing the current state and execute
    // the unpark callback processing.
    //

    MyBusy = BUSY(&MyBusy);
    OldState = (PST_PARKER)InterlockedExchangePointer(&RegisteredTake->State, MyBusy);

    //
    // ...
    //

    do {

        //
        // If the acquire operation was cancelled, cancel the acquire
        // attempt on the waitable.
        //

        if (WaitStatus != WAIT_SUCCESS && WaitStatus != WAIT_DISPOSED) {
            CANCEL_TAKE(RegisteredTake->Queue, &RegisteredTake->WaitBlock);
        }

        //
        // Execute the user callback routine.
        //

        RegisteredTake->ThreadId = GetCurrentThreadId();
        (*RegisteredTake->UserCallback)(RegisteredTake->UserCallbackContext,
                                        RegisteredTake->WaitBlock.Channel, WaitStatus);
        RegisteredTake->ThreadId = 0;

        //
        // If the *ExecuteOnlyOnce* is true or there is an unregister
        // in progress, set the state to INACTIVE. If any thread is waiting,
        // unpark it.
        //

        if (RegisteredTake->ExecuteOnlyOnce != FALSE || StateIsParker(OldState)) {
            if (StateIsParker(OldState)) {
                UnparkThread(OldState, WAIT_SUCCESS);
            } else {
                RegisteredTake->State = INACTIVE;
            }
            return;
        }

        //
        // We must re-register with the Blocking Queue.
        // So, initialize the parker and execute the TakeAny prologue.
        //

        InitializeParker(&RegisteredTake->CbParker.Parker, 1);
        TAKE_PROLOGUE(RegisteredTake->Queue, &RegisteredTake->CbParker.Parker,
                      &RegisteredTake->WaitBlock, WAIT_SUCCESS);

        //
        // Enable the unpark callback.
        //

        if ((WaitStatus = EnableUnparkCallback(&RegisteredTake->CbParker, RegisteredTake->Timeout,
                                               &RegisteredTake->RawTimer)) == WAIT_PENDING) {

            //
            // If the *State* field constains still our *busy* set it to ACTIVE.
            //

            if (RegisteredTake->State == MyBusy) {
                CasPointer(&RegisteredTake->State, MyBusy, ACTIVE);
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
// Registers a take with a Blocking Queue.
//

BOOL
WINAPI
StBlockingQueue_RegisterTake (
    __inout PVOID Queue,
    __inout PST_REGISTERED_TAKE RegisteredTake,
    __in ST_TAKE_CALLBACK *UserCallback,
    __in_opt PVOID UserCallbackContext,
    __in ULONG Timeout,
    __in BOOL ExecuteOnlyOnce
    )
{
    PST_BLOCKING_QUEUE TheQueue;
    ULONG WaitStatus;

    //
    // Validate the arguments.
    //

    TheQueue = (PST_BLOCKING_QUEUE)Queue;
    if (Timeout == 0 || UserCallback == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    //
    // Initialize the registered take object.
    //

    InitializeRawTimer(&RegisteredTake->RawTimer, &RegisteredTake->CbParker.Parker);
    InitializeCallbackParker(&RegisteredTake->CbParker, UnparkCallback, RegisteredTake);
    RegisteredTake->Queue = TheQueue;
    RegisteredTake->UserCallback = UserCallback;
    RegisteredTake->UserCallbackContext = UserCallbackContext;
    RegisteredTake->Timeout = Timeout;
    RegisteredTake->ExecuteOnlyOnce = ExecuteOnlyOnce;
    RegisteredTake->ThreadId = 0;

    //
    // Initialize the registered wait's parker and execute the Take prologue.
    //

    InitializeParker(&RegisteredTake->CbParker.Parker, 1);
    TAKE_PROLOGUE(TheQueue, &RegisteredTake->CbParker.Parker,
                  &RegisteredTake->WaitBlock, WAIT_SUCCESS);

    //
    // Set the registered take to the active and enable the unpark callback.
    //

    RegisteredTake->State = ACTIVE;
    if ((WaitStatus = EnableUnparkCallback(&RegisteredTake->CbParker, Timeout,
                                           &RegisteredTake->RawTimer)) != WAIT_PENDING) {

        //
        // The acquire operation was already accomplished. So, execute the
        // unpark callback inline.
        //

        UnparkCallback(RegisteredTake, WaitStatus);
    }
    return TRUE;
}

//
// Unregisters the registered take.
//

BOOL
WINAPI
StRegisteredTake_Unregister (
    __inout PST_REGISTERED_TAKE RegisteredTake
    )
{
    ST_PARKER Parker;
    PST_PARKER OldState;
    SPIN_WAIT Spinner;


    //
    // If the take is being unregistered from the user callback function,
    // we just set the *ExecuteOnlyOnce* to TRUE and return.
    //

    if (RegisteredTake->ThreadId == GetCurrentThreadId()) {
        RegisteredTake->ExecuteOnlyOnce = TRUE;
        return TRUE;
    }

    //
    // If the take is registered, we must try to unregister it and
    // return only after the take is actually unregistered.
    // If the wait was already unregistered or an unregister is taking place,
    // we return FALSE. 
    //

    InitializeSpinWait(&Spinner);
    InitializeParker(&Parker, 0);
    do {
        if ((OldState = RegisteredTake->State) == INACTIVE) {
            return FALSE;
        }
        if (OldState == ACTIVE) {
            if (CasPointer(&RegisteredTake->State, ACTIVE, &Parker)) {
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
    // Try to cancel the take's callback parker. If we succeed, call
    // the unpark with status disposed. Otherwise, a callback is taking
    // place and we must synchronize with its end.
    //

    if (TryCancelParker(&RegisteredTake->CbParker.Parker)) {
        UnparkThread(&RegisteredTake->CbParker.Parker, WAIT_UNREGISTERED);
    } else {
        ParkThreadEx(&Parker, BUSY_SPINS, INFINITE, NULL);
    }

    //
    // Set the registered take object to inactive and return success.
    //

    RegisteredTake->State = INACTIVE;
    return TRUE;
}
