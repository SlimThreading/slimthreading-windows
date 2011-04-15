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
// Initializes the init once lock.
//

VOID
WINAPI
StInitOnceLock_Init (
    __out PST_INIT_ONCE_LOCK Lock
    )
{
    Lock->State = IOL_FREE;
}

//
// Slow path for try init target.
//

BOOLEAN
FASTCALL
SlowTryInitTarget (
    __inout PST_INIT_ONCE_LOCK Lock,
    __in ULONG SpinCount
    )
{
    PST_PARKER State;
    ST_PARKER Parker;

    //
    // Spin the specified number of cycles, checking if the
    // initialization is completed or the lock becomes free.
    //

    do {
        if ((State = Lock->State) == IOL_FREE && CasPointer(&Lock->State, IOL_FREE, IOL_BUSY)) {

            //
            // The current thread acquired the lock; so, return true to
            // signal that it must try to intialize the underlying
            // target.
            //

            return TRUE;
        }
        if (State == IOL_AVAILABLE) {
            _ReadBarrier();
            return FALSE;
        }
        if (SpinCount-- == 0) {
            break;
        }
        SpinWait(1);
    } while (TRUE);

    //
    // The initialization is taking place. So initialize a locked
    // parker and try to insert it in the lock's wait list,
    // but only if the lock remains busy.
    //

    InitializeParker(&Parker, 0);
    do {
        if ((State = Lock->State) == IOL_AVAILABLE) {
            _ReadBarrier();
            return FALSE;
        }
        if (State == IOL_FREE && CasPointer(&Lock->State, IOL_FREE, IOL_BUSY)) {
            return TRUE;
        }
        Parker.Next = State;
        if (CasPointer(&Lock->State, State, &Parker)) {
            break;
        }
    } while (TRUE);

    //
    // Park the current thread and, after it is released, return
    // true if the current thread must perform the initialization
    // and false otherwise.
    //

    return ParkThreadEx(&Parker, 0, INFINITE, NULL) == IOL_STATUS_INIT;
}

//
// Acquires the lock and returns true; or, returns false if the
// initialization already succeed and the associated target
// value is available.
//

BOOL
WINAPI
StInitOnceLock_TryInitTarget(
    __inout PST_INIT_ONCE_LOCK Lock,
    __in ULONG SpinCount
    )
{
    if (Lock->State == IOL_AVAILABLE) {
        _ReadBarrier();
        return FALSE;
    }
    return SlowTryInitTarget(Lock, SpinCount);
}

//
// Signals that the current thread completed the initialization.
//

VOID
WINAPI
StInitOnceLock_InitTargetCompleted (
    __inout PST_INIT_ONCE_LOCK Lock
    )
{
    InitTargetCompleted(Lock);
}
//
// Signals that the current thread failed the initialization.
//

VOID
WINAPI
StInitOnceLock_InitFailed (
    __inout PST_INIT_ONCE_LOCK Lock
    )
{
    InitTargetFailed(Lock);
}
