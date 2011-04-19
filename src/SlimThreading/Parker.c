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
// Unparks the parker's owner thread.
//

VOID
FASTCALL
UnparkThread (
    __inout PST_PARKER Parker,
    __in ULONG WaitStatus
    )
{
    
    //
    // If the specified thread isn't already waiting on the park spot,
    // it will unparked because we clear the wait-in-progress bit;
    // otherwise, we need to set the park spot.
    //
    
    if (!UnparkInProgressThread(Parker, WaitStatus)) {
        if (Parker->ParkSpotHandle != NULL) {
            SetParkSpot(Parker->ParkSpotHandle);
        } else {

            //
            // This is a callback parker!
            //

            PCB_PARKER CbParker = (PCB_PARKER)Parker;
            if (CbParker->RawTimer != NULL && WaitStatus != WAIT_TIMEOUT) {
                UnlinkRawTimer(CbParker->RawTimer);
            }

            //
            // Execute the specified callback.
            //

            (*CbParker->Callback)(CbParker->CallbackContext, WaitStatus);
        }
    }	
}

//
// Initializes the parker.
//

VOID
WINAPI
StParker_Init (
    __out PST_PARKER Parker,
    __in USHORT Count
    )
{
    InitializeParker(Parker, Count);
}

//
// Returns true if the parker is locked.
//

BOOL
WINAPI
StParker_IsLocked (
    __in PST_PARKER Parker
    )
{
    return IsParkerLocked(Parker);
}

//
// Tries to lock the parker.
//

BOOL
WINAPI
StParker_TryLock (
    __inout PST_PARKER Parker
    )
{
    return TryLockParker(Parker);
}

//
// Tries to cancel the park operation associated with
// the parker.
//

BOOL
WINAPI
StParker_TryCancel (
    __inout PST_PARKER Parker
    )
{
    return TryCancelParker(Parker);
}

//
// Unparks the owner thread of the parker.
//

VOID
WINAPI
StParker_Unpark (
    __inout PST_PARKER Parker,
    __in ULONG WaitStatus
    )
{
    UnparkThread(Parker, WaitStatus);
}

//
// Unparks the owner thread of the parker,
// but only if it isn't already blocked on the park spot.
//

BOOL
WINAPI
StParker_UnparkInProgress (
    __inout PST_PARKER Parker,
    __in ULONG WaitStatus
    )
{
    return UnparkInProgressThread(Parker, WaitStatus);
}

//
// Unparks the current thread.
//

VOID
WINAPI
StParker_UnparkSelf (
    __inout PST_PARKER Parker,
    __in ULONG WaitStatus
    )
{
    UnparkSelf(Parker, WaitStatus);
}

//
// Parks the current thread until it is unparked, the specified
// timeout expires or the specified alerter is set.
//

PTEB_EXTENSION
FASTCALL
GetTebExtension (
    );

BOOL
FASTCALL
IsUmsWorkerThread (
    );

ULONG
FASTCALL
ParkThreadEx (
    __inout PST_PARKER Parker,
    __in ULONG SpinCount,
    __in ULONG Timeout,
    __inout PST_ALERTER Alerter
    )
{
    BOOLEAN Registered;

    //
    // Spin for the specified amount of cycles.
    //

    do {
        if (Parker->State >= 0) {
            _ReadBarrier();
            return Parker->WaitStatus;
        }
        if (SpinCount-- == 0) {
            break;
        }
        if (Alerter != NULL && Alerter->State== ALERTED && TryCancelParker(Parker)) {
            return WAIT_ALERTED;
        }
        SpinWait(1);
    } while (TRUE);

    //
    // Allocate a park spot to block the current thread.
    //
        
    if ((Parker->ParkSpotHandle = AllocParkSpot()) == NULL) {
        SPIN_WAIT Spinner;

        //
        // If we can't allocate a park spot, due to resource exaustion,
        // try to cancel the current park operation.
        //

        if (TryCancelParker(Parker)) {
            SetLastError(ERROR_OUTOFMEMORY);
            return WAIT_FAILED;
        }

        //
        // Someone else already locked us. Since that we can't allocate
        // a park spot to block, we wait spinning in order to don't corrupt
        // the state of the synchronizer where we are waiting.
        //

        InitializeSpinWait(&Spinner);
        while (Parker->State < 0) {
            SpinOnce(&Spinner);
        }
        _ReadBarrier();
        return Parker->WaitStatus;
    }

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
    // If an alerter was specified, register the parker with it.
    // If the alerter is already set, try to cancel the parker with
    // WAIT_ALERTED.
    //

    Registered = FALSE;
    if (Alerter != NULL) {
        if (!(Registered = RegisterParker(Alerter, Parker))) {			

            //
            // Try to cancel the parker and, if succeed, return wait
            // alerted.
            //

            if (TryCancelParker(Parker)) {
                FreeParkSpot(Parker->ParkSpotHandle);
                return WAIT_ALERTED;
            }

            //
            // We can't cancel the parker because someone else locked
            // us. So, wait unconditionally on the park spot until it
            // is set.
            //

            Timeout = INFINITE;
        }
    }

    //
    // Wait on the park spot.
    //

    WaitForParkSpot(Parker, Parker->ParkSpotHandle, Timeout);
    
    //
    // Free the park spot, deregister the parker from the alerter,
    // if it is registered, and return the wait status.
    //

    if (Registered) {
        DeregisterParker(Alerter, Parker);
    }
    FreeParkSpot(Parker->ParkSpotHandle);
    return Parker->WaitStatus;
}

//
// Parks unconditionally the current thread.
//

ULONG
FASTCALL
ParkThread (
    __inout PST_PARKER Parker
    )
{

    //
    // If the thread was already unparked, return the park wait status.
    //

    if (Parker->State >= 0) {
        _ReadBarrier();
        return Parker->WaitStatus;
    }

    //
    // Allocate a park spot to block the current thread.
    //
        
    if ((Parker->ParkSpotHandle = AllocParkSpot()) == NULL) {
        SPIN_WAIT Spinner;

        //
        // If we can't allocate a park spot due to resource exaustion,
        // we spin unconditonally until the thread is unparked.
        //

        do {
            if (Parker->State >= 0) {
                _ReadBarrier();
                return Parker->WaitStatus;
            }
            SpinOnce(&Spinner);
        } while (TRUE);
    }
    
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
    // Wait unconditionally until the park spot is set.
    //

    WaitForParkSpot(Parker, Parker->ParkSpotHandle, INFINITE);

    //
    // Free the park spot and return the wait status.
    //

    FreeParkSpot(Parker->ParkSpotHandle);
    return Parker->WaitStatus;
}

//
// Enables the unpark callback.
//

ULONG
FASTCALL
EnableUnparkCallback (
    __inout PCB_PARKER CbParker,
    __in ULONG Timeout,
    __inout_opt PRAW_TIMER TimeoutTimer
    )
{
    ULONG WaitStatus;

    //
    // If the unpark was already called, return immeditely.
    //

    if (CbParker->Parker.State >= 0) {
        _ReadBarrier();
        return CbParker->Parker.WaitStatus;
    }

    //
    // Clear the raw timer and park spot handle fields.
    //

    CbParker->RawTimer = NULL;
    CbParker->Parker.ParkSpotHandle = NULL;

    if (Timeout == 0) {

        //
        // If a zero timeout is specified and a timer, we record the current
        // time as *FireTime* in order to support periodic timers.
        //

        if (TimeoutTimer != NULL) {
            TimeoutTimer->FireTime = GetTickCount();
        }
        if (TryCancelParker(&CbParker->Parker)) {
            return WAIT_TIMEOUT;
        }
    } else if (Timeout != INFINITE) {
        CbParker->RawTimer = TimeoutTimer;

        if ((LONG)Timeout < 0) {
            SetPeriodicRawTimer(TimeoutTimer, Timeout & ~(1 << 31));
        } else {
            SetRawTimer(TimeoutTimer, Timeout);
        }
    }

    //
    // Clear the wait-in-progress bit. If this bit is already cleared,
    // the current thread was already unparked; ...
    //
    
    if (!InterlockedBitTestAndReset(&CbParker->Parker.State, WAIT_IN_PROGRESS_BIT)) {
        WaitStatus = CbParker->Parker.WaitStatus;
        
        if (CbParker->RawTimer != NULL && WaitStatus != WAIT_TIMEOUT) {
            UnlinkRawTimer(TimeoutTimer);
        }
        return WaitStatus;
    }
    return WAIT_PENDING;
}

//
// Parks the current thread - public API.
//

ULONG
WINAPI
StParker_ParkEx (
    __inout PST_PARKER Parker,
    __in ULONG SpinCycles,
    __in ULONG Timeout,
    __inout PST_ALERTER Alerter
    )
{
    return ParkThreadEx(Parker, SpinCycles, Timeout, Alerter);
}

//
// Parks unconditionally the current thread - public API.
//

ULONG
WINAPI
StParker_Park (
    __inout PST_PARKER Parker
    )
{
    return ParkThread(Parker);
}

//
// Delays the execution of the current thread for the specified time
// interval or until the alerter is set.
//

ULONG
WINAPI
StParker_SleepEx (
    __in ULONG Time,
    __inout_opt PST_ALERTER Alerter
    )
{
    ST_PARKER Parker;

    InitializeParker(&Parker, 1);
    return ParkThreadEx(&Parker, 0, Time, Alerter);
}

//
// Delays the execution of the current thread for the specified time
// interval or until the alerter is set.
//

BOOL
WINAPI
StParker_Sleep (
    __in ULONG Time
    )
{
    ST_PARKER Parker;

    InitializeParker(&Parker, 1);
    return ParkThreadEx(&Parker, 0, Time, NULL);
}

//
// Signals that the current thread entered a COM STA apartment.
//

STAPI
BOOL
WINAPI
StParker_EnterStaApartment (
    )
{
    return SetStaAffinity(TRUE);
}

//
// Signals that the current thread exited a COM STA apartment.
//

STAPI
BOOL
WINAPI
StParker_ExitStaApartment (

    )
{
    return SetStaAffinity(FALSE);
}
