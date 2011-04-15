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
// Initializes the count down event.
//

VOID
WINAPI
StCountDownEvent_Init (
    __out PST_COUNT_DOWN_EVENT CountDown,
    __in LONG Count,
    __in ULONG SpinCount
    )
{
    StNotificationEvent_Init(&CountDown->Event, Count == 0, SpinCount);
    CountDown->State = Count;
}

//
// Signals the count down event for the specified amount.
//

BOOL
WINAPI
StCountDownEvent_Signal (
    __inout PST_COUNT_DOWN_EVENT CountDown,
    __in LONG Count
    )
{
    LONG State;
    LONG NewState;

    //
    // Validate the argument.
    //

    if (Count <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    do {
        NewState = (State = CountDown->State) - Count;
        if (NewState < 0) {
            return FALSE;
        }
        if (CasLong(&CountDown->State, State, NewState)) {

            //
            // If the counter reaches zero, set the associated notification
            // event where the waiter threads are blocked.
            //

            if (State == Count) {
                StNotificationEvent_Set(&CountDown->Event);
            }
            return TRUE;
        }
    } while (TRUE);
}

//
// Signals the count down event for the specified amount.
//

BOOL
WINAPI
StCountDownEvent_SignalEx (
    __inout PST_COUNT_DOWN_EVENT CountDown,
    __in LONG Count,
    __in ST_COUNT_DOWN_EVENT_ACTION *Action,
    __in PVOID ActionContext
    )
{
    LONG State;
    LONG NewState;

    //
    // Validate the argument.
    //

    if (Count <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    do {
        NewState = (State = CountDown->State) - Count;
        if (NewState < 0) {
            return FALSE;
        }
        if (CasLong(&CountDown->State, State, NewState)) {

            //
            // If the counter reaches zero, set the associated notification
            // event where the waiter threads are blocked.
            //

            if (State == Count) {
                (*Action)(ActionContext);
                StNotificationEvent_Set(&CountDown->Event);
            }
            return TRUE;
        }
    } while (TRUE);
}

//
// Tries to add the specified amount to the count down event's
// state, but only if it greater that zero.
//

BOOL
WINAPI
StCountDownEvent_TryAdd (
    __inout PST_COUNT_DOWN_EVENT CountDown,
    __in LONG Count
    )
{
    LONG State;

    //
    // Validate the argument.
    //

    if (Count <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    do {
        if ((State = CountDown->State) == 0) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;
        }
        if (CasLong(&CountDown->State, State, State + Count)) {
            return TRUE;
        }
    } while (TRUE);
}

//
// Waits until the count down event reaches zero, activating the
// specified cancellers.
//

ULONG
WINAPI
StCountDownEvent_WaitEx (
    __inout PST_COUNT_DOWN_EVENT CountDown,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    if (CountDown->State == 0) {
        _ReadBarrier();
        return WAIT_SUCCESS;
    }
    return StNotificationEvent_WaitEx(&CountDown->Event, Timeout, Alerter);
}

//
// Waits unconditionally until the count down event reaches zero.
//

BOOL
WINAPI
StCountDownEvent_Wait (
    __inout PST_COUNT_DOWN_EVENT CountDown
    )
{
    if (CountDown->State == 0) {
        _ReadBarrier();
        return TRUE;
    }
    return StNotificationEvent_Wait(&CountDown->Event);
}

//
// Returns the current value of the count down event.
//

LONG
WINAPI
StCountDownEvent_Read (
    __inout PST_COUNT_DOWN_EVENT CountDown
    )
{
    LONG State = CountDown->State;
    _ReadBarrier();
    return State;
}
