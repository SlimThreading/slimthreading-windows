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
// Inits once synchronously.
//

BOOL
WINAPI
StInitOnce_ExecuteOnce (
    __inout PST_INIT_ONCE InitOnce,
    __in PST_INIT_ONCE_CTOR Ctor,
    __inout_opt PVOID CtorParameter,
    __out PVOID *Context
    )
{
    PVOID Target;

    //
    // If the target was already initialized, return success.
    //

    if (!TryInitTarget(&InitOnce->Lock)) {
        *Context = InitOnce->Target;
        return TRUE;
    }

    //
    // The current thread must perform the initialization
    // of the underlying target.
    //
                
    if (Ctor(InitOnce, CtorParameter, &Target)) {

        //
        // The initialization succeed; so, set the *Target* of the
        // init once object and release the init once lock. 
        //

        InitOnce->Target = Target;
        InitTargetCompleted(&InitOnce->Lock);
        *Context = Target;
        return TRUE;
    }
                    
    //
    // The initialization failed; so, release the init once lock
    // and return failure.
    //
            
    InitTargetFailed(&InitOnce->Lock);
    *Context = NULL;
    return FALSE;
}

//
// Begins one-time initialization on synchronous or asynchronous mode.
//

BOOL
WINAPI
StInitOnce_BeginInit (
    __inout PST_INIT_ONCE InitOnce,
    __in ULONG Flags,
    __out BOOL *Pending,
    __out PVOID *Context
    )
{

    //
    // If check-only, return the current target if it is
    // non-null.
    //

    if (Flags == INIT_ONCE_CHECK_ONLY) {
        PVOID Target = InitOnce->Target;
        if (!(*Pending = (Target == NULL))) {
            *Context = Target;
        }
        _ReadBarrier();
        return TRUE;
    }

    //
    // If this is a synchronous one-time initializaton, proceed
    // as appropriate.
    //

    if (Flags == 0) {

        //
        // Try to acquire the init once lock.
        //

        if (!TryInitTarget(&InitOnce->Lock)) {
            *Pending = FALSE;
            *Context = InitOnce->Target;
            return TRUE;
        }

        //
        // The current thread must initiaize the target, so return
        // true.
        //

        *Pending = TRUE;
        return TRUE;
    }

    //
    // Asynchronous initialization.
    // If the target is available, sets the pending to false and
    // return the target.
    //

    if (InitOnce->Target != NULL) {
        _ReadBarrier();
        *Pending = FALSE;
        *Context = InitOnce->Target;
        return TRUE;
    }

    //
    // If a synchronous initialization didn't start yet, set *Pending*
    // to true and return success.
    //

    if (InitOnce->Lock.State == NULL) {
        *Pending = TRUE;
        return TRUE;
    }

    //
    // The initialization already started in synchronous mode, so
    // return failure.
    //

    return FALSE;
}

//
// Completes the one-time initialization started with
// StInitOnce_BeginInit function.
//

BOOL
WINAPI
StInitOnce_Complete (
    __inout PST_INIT_ONCE InitOnce,
    __in ULONG Flags,
    __in PVOID Context
    )
{
    
    //
    // If this is a synchronous one-time initialization,
    // does the appropriate processing.
    //

    if ((Flags & INIT_ONCE_ASYNC) == 0) {
        if ((Flags & INIT_ONCE_INIT_FAILED) != 0) {
            InitTargetFailed(&InitOnce->Lock);
            return FALSE;
        }

        //
        // Set the target field, signal the init once lock and
        // return success.
        //

        InitOnce->Target = Context;
        InitTargetCompleted(&InitOnce->Lock);
        return TRUE;
    }

    //
    // This is a asynchronous initialization, so try to set the *Target*
    // field if it is still null.
    //

    return CasPointer(&InitOnce->Target, NULL, Context);
}

//
// Initializes the specified init once object.
//

VOID
WINAPI
StInitOnce_Init (
    __inout PST_INIT_ONCE InitOnce
    )
{
    InitializeInitOnceLock(&InitOnce->Lock);
    InitOnce->Target = NULL;
}

/*++
 *
 * Ensure Initialized API.
 *
 --*/


PVOID
WINAPI
StEnsureInitialized_Asynchronous (
    __inout PVOID *Target,
    __in PST_ENSURE_INITIALIZED_CTOR Ctor,
    __in PST_ENSURE_INITIALIZED_DTOR Dtor,
    __in PVOID Context
    )
{
    PVOID LocalTarget;
    PVOID NewTarget;

    if ((LocalTarget = *Target) != NULL) {
        return LocalTarget;
    }

    if ((LocalTarget = Ctor(Context)) != NULL) {
        if ((NewTarget = InterlockedCompareExchangePointer(Target, LocalTarget, NULL)) == NULL) {
            return LocalTarget;
        }
        Dtor(LocalTarget, Context);
        return NewTarget;
    }

    //
    // Initialization of the target failed, so return NULL.
    //

    return NULL;
}

//
// Ensures initialized synchronous.
//

STAPI
PVOID
WINAPI
StEnsureInitialized_Synchronous (
    __inout PVOID *Target,
    __in PST_INIT_ONCE_LOCK Lock,
    __in PST_ENSURE_INITIALIZED_CTOR Ctor,
    __in PVOID Context,
    __in ULONG SpinCount
    )
{

    if (TryInitOnce(Lock, SpinCount)) {
        PVOID LocalTarget;
        if ((LocalTarget = Ctor(Context)) != NULL) {
            *Target = LocalTarget;
            InitTargetCompleted(Lock);
        } else {
            InitTargetFailed(Lock);
        }
        return LocalTarget;
    }
    return *Target;
}
