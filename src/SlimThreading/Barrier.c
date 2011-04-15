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
// The barrier synchronization state is used as follows:
// - bits 0-15: hold the number of arrived partners;
// - bit 16..31: hold the number of expected partners.
//

#define ARRIVED_MASK		((1 << 16) - 1)
#define MAX_PARTNERS		ARRIVED_MASK
#define PARTNERS_MASK		(~ARRIVED_MASK)
#define PARTNERS_SHIFT		16

//
// Initializes the cyclic barrier.
//

BOOL
WINAPI
StBarrier_Init (
    __out PST_BARRIER Barrier,
    __in USHORT Partners,
    __in BARRIER_ACTION *Action,
    __in PVOID ActionContext
    )
{
    if (Partners == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Barrier->States[0].State = Partners << PARTNERS_SHIFT;
    StNotificationEvent_Init(&Barrier->States[0].Event, FALSE, 0);
    Barrier->PhaseState = Barrier->States;
    Barrier->PhaseNumber = 0;
    Barrier->Action = Action;
    Barrier->ActionContext = (ActionContext != NULL) ? ActionContext : Barrier;
    return TRUE;
}

//
// Finishes the current phase and starts a new one.
//

FORCEINLINE
VOID
FinishPhase (
    __inout PST_BARRIER Barrier
    )
{
    PPHASE_STATE PhaseState, NextPhaseState;

    //
    // if there is a post phase action defined, call it.
    //

    if (Barrier->Action != NULL) {
        (*Barrier->Action)(Barrier->ActionContext);
    }

    //
    // Starts the new phase and signals the current one.
    //

    Barrier->PhaseNumber++;
    PhaseState = Barrier->PhaseState;
    NextPhaseState = (PhaseState == Barrier->States) ? &Barrier->States[1] : Barrier->States;
    NextPhaseState->State = PhaseState->State & PARTNERS_MASK;
    StNotificationEvent_Init(&NextPhaseState->Event, FALSE, 0);
    Barrier->PhaseState = NextPhaseState;
    StNotificationEvent_Set(&PhaseState->Event);
}

//
// Adds the specified number of partners to the current phase.
//

static
FORCEINLINE
BOOLEAN
AddPartners (
    __inout PST_BARRIER Barrier,
    __in USHORT Count,
    __out_opt ULONGLONG *AddedPhase
    )
{
    PPHASE_STATE PhaseState;
    ULONG State;
    ULONG Partners;
    ULONG Arrived;

    PhaseState = Barrier->PhaseState;
    do {
        Partners = (State = PhaseState->State) >> PARTNERS_SHIFT;
        Arrived = State & ARRIVED_MASK;

        //
        // Validate the argument.
        //

        if (Count == 0 || (Count + Partners) > MAX_PARTNERS) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }

        //
        // If the current phase was already reached, wait unconditionally
        // until a new phase starts.
        //

        if (Arrived == Partners) {
            StNotificationEvent_WaitEx(&PhaseState->Event, 50, NULL);

            //
            // Get the new phase state and retry.
            //

            PhaseState = Barrier->PhaseState;
            continue;
        }

        //
        // Update the number of partners and, if succeed, return.
        //

        if (CasLong(&PhaseState->State, State, ((Partners + Count) << PARTNERS_SHIFT) | Arrived)) {
            if (ARGUMENT_PRESENT(AddedPhase)) {
                *AddedPhase = Barrier->PhaseNumber;
            }
            return TRUE;
        }
    } while (TRUE);
}

//
// Adds the specified number of partners to the current phase.
//

BOOL
WINAPI
StBarrier_AddPartners (
    __inout PST_BARRIER Barrier,
    __in USHORT Count,
    __out_opt ULONGLONG *AddedPhase
    )
{
    return AddPartners(Barrier, Count, AddedPhase);
}

//
// Adds a partner to the current phase of the barrier.
//

BOOL
WINAPI
StBarrier_AddPartner (
    __inout PST_BARRIER Barrier,
    __out_opt ULONGLONG *AddedPhase
    )
{
    return AddPartners(Barrier, 1, AddedPhase);
}

//
// Removes the specified number of partners from the current phase
// of the barrier.
//

FORCEINLINE
BOOLEAN
RemovePartners (
    __inout PST_BARRIER Barrier,
    __in USHORT Count
    )
{
    PPHASE_STATE PhaseState;
    ULONG State;
    ULONG Partners, NewPartners;
    ULONG Arrived;

    PhaseState = Barrier->PhaseState;
    do {

        Partners = (State = PhaseState->State) >> PARTNERS_SHIFT;
        Arrived = State & ARRIVED_MASK;
        
        //
        // Validate the argument.
        //

        if (Count == 0 || Partners <= Count) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        NewPartners = Partners - Count;
        if (Arrived == NewPartners) {

            if (CasLong(&PhaseState->State, State, NewPartners << PARTNERS_SHIFT)) {
                FinishPhase(Barrier);
                return TRUE;
            }
        } else {
            if (CasLong(&PhaseState->State, State, (NewPartners << PARTNERS_SHIFT) | Arrived)) {
                return TRUE;
            }
        }
    } while (TRUE);
}

//
// Removes the specified number of partners from the current phase
// of the barrier.
//

BOOL
WINAPI
StBarrier_RemovePartners (
    __inout PST_BARRIER Barrier,
    __in USHORT Count
    )
{
    return RemovePartners(Barrier, Count);
}

//
// Removes a partner from the current phase of the barrier.
//

BOOL
WINAPI
StBarrier_RemovePartner (
    __inout PST_BARRIER Barrier
    )
{
    return RemovePartners(Barrier, 1);
}

//
// Signals the barrier and then waits until the current phase completes,
// activating the specified cancellers.
//

ULONG
WINAPI
StBarrier_SignalAndWaitEx (
    __inout PST_BARRIER Barrier,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    PPHASE_STATE PhaseState;
    ULONG State;
    ULONG Partners;
    ULONG Arrived;
    ULONG WaitStatus;
    
    //
    // Get current phase state.
    //

    PhaseState = Barrier->PhaseState;
    do {
        Partners = (State = PhaseState->State) >> PARTNERS_SHIFT;
        Arrived = State & ARRIVED_MASK;
        if (Arrived == Partners) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return WAIT_FAILED;
        }

        //
        // Increment the number of arrived partners.
        //

        if (CasLong(&PhaseState->State, State, State + 1)) {
            if (Arrived + 1 == Partners) {

                //
                // The current thread is the last partner. So, finish the current
                // barrier's phase, starts a new one and return success.
                //

                FinishPhase(Barrier);
                return WAIT_SUCCESS;
            }
            break;
        }
    } while (TRUE);

    //
    // Wait until the phase's event is signalled or the wait is cancelled.
    //
    
    WaitStatus =  StNotificationEvent_WaitEx(&PhaseState->Event, Timeout, Alerter);

    //
    // If the phase synchronization completed, return success.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        return WAIT_SUCCESS;
    }

    //
    // The wait was cancelled, so try to decrement the number of
    // arrived partners.
    //
            
    do {
                
        Partners = (State = PhaseState->State) >> PARTNERS_SHIFT;
        Arrived = State & ARRIVED_MASK;
        if (Arrived == Partners) {

            //
            // Our phase was already synchronized. So, wait unconditionally
            // on the phase state's event and return success.
            //

            StNotificationEvent_Wait(&PhaseState->Event);
            return WAIT_SUCCESS;
        }
        
        //
        // Try to decrement the number of arrived partners, and return
        // a failure status if success.
        //

        if (CasLong(&PhaseState->State, State, State - 1)) {
            return WaitStatus;
        }
    } while (TRUE);
}

//
// Signals the barrier and then waits unconditionally until the current
// phase completes.
//

BOOL
WINAPI
StBarrier_SignalAndWait (
    __inout PST_BARRIER Barrier
    )
{
    return (StBarrier_SignalAndWaitEx(Barrier, INFINITE, NULL) == WAIT_SUCCESS);
}

//
// Returns the number of the current phase.
//

ULONGLONG
WINAPI
StBarrier_GetPhaseNumber (
    __in PST_BARRIER Barrier
    )
{
    ULONGLONG PhaseNumber = Barrier->PhaseNumber;
    _ReadBarrier();
    return PhaseNumber;
}

//
// Returns the current number of partners.
//

ULONG
WINAPI
StBarrier_GetPartners (
    __in PST_BARRIER Barrier
    )
{
    LONG State = Barrier->PhaseState->State;
    _ReadBarrier();
    return (State >> PARTNERS_SHIFT);
}

//
// Returns the number of partners remaining to complete the
// current phase.
//

ULONG
WINAPI
StBarrier_GetPartnersRemaining (
    __in PST_BARRIER Barrier
    )
{
    ULONG State = Barrier->PhaseState->State;
    _ReadBarrier();
    return (State >> PARTNERS_SHIFT) - (State & ARRIVED_MASK);
}
