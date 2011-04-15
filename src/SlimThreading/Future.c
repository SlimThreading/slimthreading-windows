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
// Initializes the future.
//

VOID
WINAPI
StFuture_Init (
    __inout PST_FUTURE Future,
    __in ULONG SpinCount
    )
{
    Future->Value = (ULONG_PTR)0;
    Future->State = 0;
    StNotificationEvent_Init(&Future->IsAvailable, FALSE, SpinCount);
}

//
// Waits until the future's value is available, activating the
// specified cancellers.
//

ULONG
WINAPI
StFuture_GetValueEx (
    __inout PST_FUTURE Future,
    __out ULONG_PTR *Result,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ULONG WaitStatus;

    //
    // If the future's value is already available, return success.
    //

    if (StNotificationEvent_IsSet(&Future->IsAvailable)) {
        *Result = Future->Value;
        return WAIT_SUCCESS;
    }

    //
    // The future value isn't available; so, if a null timeout was
    // specified, return failure.
    //

    if (Timeout == 0) {
        return WAIT_TIMEOUT;
    }

    //
    // Wait until the associated notification event is signalled.
    //

    if ((WaitStatus = StNotificationEvent_WaitEx(&Future->IsAvailable, Timeout,
                                                 Alerter)) == WAIT_SUCCESS) {
        *Result = Future->Value;
        return WAIT_SUCCESS;
    }

    //
    // The wait was cancelled due to timeout or alert; so, return
    // the appropriate failure status.
    //

    return WaitStatus;
}

//
// Waits unconditionally until the future's value is available.
//

BOOL
WINAPI
StFuture_GetValue (
    __inout PST_FUTURE Future,
    __out ULONG_PTR *Result
    )
{
    ULONG WaitStatus;

    //
    // If the future's value is already available, return the value
    // and success. Otherwise, wait until the event is set.
    //

    if (StNotificationEvent_IsSet(&Future->IsAvailable) ||
        (WaitStatus = StNotificationEvent_Wait(&Future->IsAvailable)) == WAIT_SUCCESS) {
        *Result = Future->Value;
        return TRUE;
    }
    return FALSE;
}

//
// Returns the future value if it is available.
//

BOOL
WINAPI
StFuture_TryGetValue (
    __inout PST_FUTURE Future,
    __out ULONG_PTR *Result
    )
{

    //
    // If the future's value is already available, return the value
    // and success. Otherwise, return failure.
    //

    if (StNotificationEvent_IsSet(&Future->IsAvailable)) {
        *Result = Future->Value;
        return TRUE;
    }
    return FALSE;
}


//
// Sets the future's value.
//

BOOL
WINAPI
StFuture_SetValue (
    __inout PST_FUTURE Future,
    __in ULONG_PTR Value
    )
{
    //
    // Each future object can only be set once.
    // (The *State* field is used as a lock to grant this).
    // If the future is set by the current thread, make the future
    // value available and set the associated notification event.
    //
    // NOTE: The visibility of the *Value* field is granted because
    //		 the notification event is set using an atomic instruction.
    //

    if (CasLong(&Future->State, 0, 1)) {
        Future->Value = Value;
        StNotificationEvent_Set(&Future->IsAvailable);
        return TRUE;
    }

    //
    // The future value was alreay set; so, return failure.
    //
    
    return FALSE;
}
