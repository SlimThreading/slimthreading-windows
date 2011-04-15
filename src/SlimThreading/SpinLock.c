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
// Acquires the spin lock when it is seen as busy.
//

VOID
FASTCALL
SlowAcquireSpinLock (
    __inout PSPINLOCK Lock
    )
{
    ST_PARKER Parker;
    PST_PARKER State;
    ULONG SpinCount;

    do {

        //
        // Try to acquire spinning for the specified number of cycles.
        //

        SpinCount = Lock->SpinCount;
        do {
            if ((State = Lock->State) == SL_FREE) {
                if  (CasPointer(&Lock->State, SL_FREE, SL_BUSY)) {
                    return;
                }
                continue;
            }

            //
            // If the spinlock's queue isn't empty, we stop spinning.
            //

            if (State != SL_BUSY || SpinCount-- == 0) {
                break;
            }
            SpinWait(1);
        } while (TRUE); 
    
        //
        // Initialize a locked parker and insert in the lock's queue,
        // but only if the lock remains busy.
        //

        InitializeParker(&Parker, 0);
        do {
            if ((State = Lock->State) == SL_FREE) {
                if  (CasPointer(&Lock->State, SL_FREE, SL_BUSY)) {
                    return;
                }
                continue;
            }
            Parker.Next = State;
            if  (CasPointer(&Lock->State, State, &Parker)) {
                break;
            }
        } while (TRUE);

        //
        // Park unconditionally the current thread and, after it is released,
        // retry the spinlock acquire.
        //

        ParkThread(&Parker);
    } while (TRUE);
}

//
// Releases the spinlock waiters.
//

VOID
FASTCALL
ReleaseSpinLockWaiters (
    __inout PST_PARKER WaitList
    )
{
    PST_PARKER Next;
    PST_PARKER WakeStackTop;

    //
    // Build a stack with the waiting threads in order to unpark
    // them by its lock arrival order.
    //

    WakeStackTop = NULL;
    do {
        Next = WaitList->Next;
        WaitList->Next = WakeStackTop;
        WakeStackTop = WaitList;
    } while ((WaitList = Next) != SL_BUSY);

    do {
        Next = WakeStackTop->Next;
        UnparkThread(WakeStackTop, WAIT_SUCCESS);
    } while ((WakeStackTop = Next) != NULL);
}	
