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
// Initializes the alerter.
//

VOID
WINAPI
StAlerter_Init (
    __out PST_ALERTER Alerter
    )
{
    Alerter->State = NULL;
}

//
// Returns true if the alerter is set.
//

BOOL
WINAPI
StAlerter_IsSet (
    __in PST_ALERTER Alerter
    )
{
    PST_PARKER State = Alerter->State;
    _ReadBarrier();
    return State == ALERTED;
}

//
// Deregisters the parker from the alerter - slow case.
//

VOID
FASTCALL
SlowDeregisterParker (
     __inout PST_ALERTER Alerter,
     __inout PST_PARKER Parker
     )
{
    PST_PARKER State;
    PST_PARKER First;
    PST_PARKER Last;
    PST_PARKER Next;
    PST_PARKER Current;
    SPIN_WAIT Spinner;

    //
    // Initialize the spinner.
    //

    InitializeSpinWait(&Spinner);

Retry:
    do {

        //
        // If the parker is already unlinked, return.
        //

        if (Parker->Next == Parker) {
            return;
        }

        //
        // If the alerter's list is already empty, the parker will be
        // unlinked soon by some other thread, unless that the alerter's list
        // becomes non-empty again. So, we must spin until detect that the
        // parker was unlinked or the alerter's list changes.
        //

        if ((State = Alerter->State) == NULL || State == ALERTED) {
            goto SpinUntilUnlinkedOrListChange;
        }

        //
        // It is likely that our parker is inserted in the alerter's list.
        // So, grab the alerter list, emptying it.
        //

        if (CasPointer(&Alerter->State, State, NULL)) {
            
            //
            // If our parker is the only entry in the alerter's list, return;
            // otherwise, continue, removing the locked parkers from the list.
            //

            if (State == Parker && Parker->Next == NULL) {
                return;
            }
            break;
        }
    } while (TRUE);

    //
    // Build a new list with the non-locked parkers.
    //

    First = Last = NULL;
    Current = State;
    do {
        Next = Current->Next;
        if (IsParkerLocked(Current)) {

            //
            // Mark the current parker as unlinked.
            //

            Current->Next = Current;
        } else {

            //
            // Add the parker to the new list.
            //

            if (First == NULL) {
                First = Current;
            } else {
                Last->Next = Current;
            }
            Last = Current;
        }
    } while ((Current = Next) != NULL);

    //
    // If we have a non-empty list on hand, we must merge it with
    // the current alerter's list, if the alerter is not set.
    //

    if (First != NULL) {
        do {
            
            //
            // If the alerter is already set, alert all parkers in
            // the list and return.
            //

            State = Alerter->State;
            if (State == ALERTED) {
                Last->Next = NULL;
                AlertParkerList(First);
                break;
            }

            //
            // Try to merge our list with the alerter's list.
            //

            Last->Next = State;
            if (CasPointer(&Alerter->State, State, First)) {

                //
                // Save the new alerter's list head, in order to detect
                // changes on the list's head.
                //

                State = First;
                break;
            }
        } while (TRUE);
    } else {
        State = NULL;
    }

    //
    // Spin until our parker is marked as unlinked or the alerter's
    // list head changes.
    //

SpinUntilUnlinkedOrListChange:

    while (Parker->Next != Parker) {
        PST_PARKER NewState = Alerter->State;
        if (NewState != State && NewState != ALERTED) {
            goto Retry;
        }
        SpinOnce(&Spinner);
    }
}

//
// Sets the alerter (internal function).
//

BOOLEAN
FASTCALL
SetAlerter (
    __inout PST_ALERTER Alerter
    )
{
    PST_PARKER State;

    do {

        //
        // If the alerter is already set, return false.
        //

        if ((State = Alerter->State) == ALERTED) {
            return FALSE;
        }

        if (CasPointer(&Alerter->State, State, ALERTED)) {
            
            //
            // Alert all parkers inserted in the alerter's list
            // and return true.
            //

            AlertParkerList(State);
            return TRUE;
        }
    } while (TRUE);
}

//
// Sets the alerter (public API).
//

BOOL
WINAPI
StAlerter_Set (
    __inout PST_ALERTER Alerter
    )
{
    return SetAlerter(Alerter);
}
