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

#pragma once

#ifdef SLIM_THREADING_DLL
#define STAPI	__declspec(dllexport)
#else
#include <windows.h>
#define STAPI	__declspec(dllimport)
#define LIST_ENTRY_		LIST_ENTRY
#define PLIST_ENTRY_	PLIST_ENTRY
#endif

//
// Internal data structures.
//

struct _WAIT_BLOCK;
struct _ALERT_ENTRY;
struct _WAITABLE_VTBL;
struct _ILOCK_VTBL;
struct _BLOCKING_QUEUE_VTBL;

//
// New wait status codes
//

#define WAIT_SUCCESS				WAIT_OBJECT_0
#define WAIT_ALERTED				257
#define WAIT_DISPOSED_0				512
#define WAIT_DISPOSED				WAIT_DISPOSED_0

/*++
 *
 * Alerter.
 *
 --*/

typedef struct _ST_ALERTER {
    struct _ST_PARKER * volatile State;
} ST_ALERTER, *PST_ALERTER;

//
// Alerter API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the alerter.
//

STAPI
VOID
WINAPI
StAlerter_Init (
    __out PST_ALERTER Alerter
    );

//
// Sets the alerter.
//

STAPI
BOOL
WINAPI
StAlerter_Set (
    __inout PST_ALERTER Alerter
    );

//
// Returns true if the alerter is set.
//

STAPI
BOOL
WINAPI
StAlerter_IsSet (
    __in PST_ALERTER Alerter
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StAlerter {
    ST_ALERTER _alerter;
public:

    StAlerter() {
        StAlerter_Init(&_alerter);
    }

    void Init() {
        StAlerter_Init(&_alerter);
    }

    bool Set() {
        return StAlerter_Set(&_alerter) != FALSE;
    }

    bool IsSet() {
        return StAlerter_IsSet(&_alerter) != FALSE;
    }
};
#endif

/*++
 *
 * Parker.
 *
 --*/

typedef struct _ST_PARKER {
    struct _ST_PARKER * volatile Next;
    volatile LONG State;
    HANDLE ParkSpotHandle;
    volatile ULONG WaitStatus;
} ST_PARKER, *PST_PARKER;	



//
// The raw timer structure - an internal type.
//

typedef struct _RAW_TIMER {
    struct _RAW_TIMER * volatile Next;
    struct _RAW_TIMER * volatile Previous;
    ULONG Delay;
    ULONG FireTime;
    PST_PARKER Parker;
} RAW_TIMER, *PRAW_TIMER;


//
// The callback parker structure - an internal type.
//

typedef VOID CALLBACK PARKER_CALLBACK(__inout_opt PVOID Context, __in ULONG WaitStatus);

typedef struct _CB_PARKER {
    ST_PARKER Parker;
    PRAW_TIMER RawTimer;
    PARKER_CALLBACK *Callback;
    PVOID CallbackContext;
} CB_PARKER, *PCB_PARKER;

//
// Parker API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the parker.
//

STAPI
VOID
WINAPI
StParker_Init (
    __out PST_PARKER Parker,
    __in USHORT Count
    );

//
// Returns true if the parker is locked.
//

STAPI
BOOL
WINAPI
StParker_IsLocked (
    __in PST_PARKER Parker
    );

//
// Tries to lock the parker.
//

STAPI
BOOL
WINAPI
StParker_TryLock (
    __inout PST_PARKER Parker
    );

//
// Tries to cancel the parker.
//

STAPI
BOOL
WINAPI
StParker_TryCancel (
    __inout PST_PARKER Parker
    );

//
// Unparks the parker's owner thread.
//

STAPI
VOID
WINAPI
StParker_Unpark (
    __inout PST_PARKER Parker,
    __in ULONG WaitStatus
    );

//
// Unpark the parker's owner thread, if it is not already blocked.
//

STAPI
BOOL
WINAPI
StParker_UnparkInProgress (
    __inout PST_PARKER Parker,
    __in ULONG WaitStatus
    );

//
// Unparks the current thread.
//

STAPI
VOID
WINAPI
StParker_UnparkSelf (
    __inout PST_PARKER Parker,
    __in ULONG WaitStatus
    );

//
// Parks the current thread, activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StParker_ParkEx (
    __inout PST_PARKER Parker,
    __in ULONG SpinCount,
    __in ULONG Timeout,
    __inout PST_ALERTER Alerter
    );

//
// Parks unconditionally the current thread.
//

STAPI
ULONG
WINAPI
StParker_Park (
    __inout PST_PARKER Parker
    );

//
// Delays the thread execution for the specified amount of time,
// using an alerter.
//

STAPI
ULONG
WINAPI
StParker_SleepEx (
    __in ULONG Time,
    __inout_opt PST_ALERTER Alert
    );

//
// Delays the thread execution for the specified amount of time.
//

STAPI
BOOL
WINAPI
StParker_Sleep (
    __in ULONG Time
    );

//
// Signals that the current thread entered a COM STA apartment.
//

STAPI
BOOL
WINAPI
StParker_EnterStaApartment (
    );

//
// Signals that the current thread exited a COM STA apartment.
//

STAPI
BOOL
WINAPI
StParker_ExitStaApartment (
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StParker {
    ST_PARKER _parker;
public:

    StParker(__in USHORT Count = 1) {
        StParker_Init(&_parker, Count);
    }

    void Init(__in USHORT Count = 1) {
        StParker_Init(&_parker, Count);
    }

    bool IsLocked() {
        return StParker_IsLocked(&_parker) != FALSE;
    }

    bool TryLock() {
        return StParker_TryLock(&_parker) != FALSE;
    }

    bool TryCancel() {
        return StParker_TryCancel(&_parker) != FALSE;
    }

    void Unpark(__in ULONG WaitStatus) {
        StParker_Unpark(&_parker, WaitStatus);
    }

    bool UnparkInProgress(__in ULONG WaitStatus) {
        return StParker_UnparkInProgress(&_parker, WaitStatus) != FALSE;
    }

    void UnparkSelf(__in ULONG WaitStatus) {
        StParker_UnparkSelf(&_parker, WaitStatus);
    }

    ULONG Park(__in ULONG SpinCount, __in ULONG Timeout = INFINITE, __inout StAlerter *Alerter = 0) {
        return StParker_ParkEx(&_parker, SpinCount, Timeout, (PST_ALERTER)Alerter);
    }

    ULONG Park() {
        return StParker_Park(&_parker);
    }

    static ULONG Sleep(__in ULONG Time, __inout_opt StAlerter *Alert) {
        return StParker_SleepEx(Time, (PST_ALERTER)Alert);
    }

    static ULONG Sleep(__in ULONG Time) {
        return StParker_Sleep(Time);
    }

    static bool EnterStaApartment() {
        return StParker_EnterStaApartment() != FALSE;
    }

    static bool ExitStaApartment() {
        return StParker_ExitStaApartment() != FALSE;
    }
};

#endif

/*++
 *
 * User-mode Spinlock (an internal type).
 *
 --*/

//
// Spin Lock structure.
//

typedef struct _SPINLOCK {
    volatile PST_PARKER State;
    ULONG SpinCount;
} SPINLOCK, *PSPINLOCK;

/*++
 *
 * Non-fair Lock.
 *
 --*/

typedef struct _ST_LOCK {
    volatile PLIST_ENTRY_ State;
    ULONG SpinCount;
} ST_LOCK, *PST_LOCK;

//
// Non-fair Lock API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the lock.
//

STAPI
VOID
WINAPI
StLock_Init (
    __out PST_LOCK Lock,
    __in ULONG SpinCount
    );

//
// Tries to enter the lock within the specified time.
//

STAPI
BOOL
WINAPI
StLock_TryEnterEx (
    __inout PST_LOCK Lock,
    __in ULONG Timeout
    );

//
// Tries to acquire the lock immediately.
//

STAPI
BOOL
WINAPI
StLock_TryEnter (
    __inout PST_LOCK Lock
    );

//
// Enters the lock.
//

STAPI
BOOL
WINAPI
StLock_Enter (
    __inout PST_LOCK Lock
    );

//
// Exits the lock.
//

STAPI
VOID
WINAPI
StLock_Exit (
    __inout PST_LOCK Lock
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StLock {
    ST_LOCK _lock;
public:

    StLock(__in ULONG SpinCount = 0) {
        StLock_Init(&_lock, SpinCount);
    }

    void Init(__in ULONG SpinCount = 0) {
        StLock_Init(&_lock, SpinCount);
    }

    bool TryEnter(__in ULONG Timeout) {
        return StLock_TryEnterEx(&_lock, Timeout) != FALSE;
    }

    bool TryEnter() {
        return StLock_TryEnter(&_lock) != FALSE;
    }

    bool Enter() {
        return StLock_Enter(&_lock) != FALSE;
    }

    void Exit() {
        StLock_Exit(&_lock);
    }
};

#endif

/*++
 *
 * Reentrant Non-fair Lock.
 *
 --*/

typedef struct _ST_REENTRANT_LOCK {
    ST_LOCK Lock;
    ULONG Owner;
    LONG Count;
} ST_REENTRANT_LOCK, *PST_REENTRANT_LOCK;
    
//
// Reentrant Non-fair Lock API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the lock.
//

STAPI
VOID
WINAPI
StReentrantLock_Init (
    __out PST_REENTRANT_LOCK Lock,
    __in ULONG SpinCount
    );

//
// Tries to enter the the lock within the specified time.
//

STAPI
BOOL
WINAPI
StReentrantLock_TryEnterEx (
    __inout PST_REENTRANT_LOCK Lock,
    __in ULONG Timeout
    );

//
// Tries to acquire the the lock immediately.
//

STAPI
BOOL
WINAPI
StReentrantLock_TryEnter (
    __inout PST_REENTRANT_LOCK Lock
    );

//
// Acquires unconditionally the lock.
//

STAPI
BOOL
WINAPI
StReentrantLock_Enter (
    __inout PST_REENTRANT_LOCK Lock
    );

//
// Exits the reentrant lock once.
//

STAPI
VOID
WINAPI
StReentrantLock_Exit (
    __inout PST_REENTRANT_LOCK Lock
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StReentrantLock {
    ST_REENTRANT_LOCK _rlock;
public:

    StReentrantLock(__in ULONG SpinCount = 0) {
        StReentrantLock_Init(&_rlock, SpinCount);
    }

    void Init(__in ULONG SpinCount = 0) {
        StReentrantLock_Init(&_rlock, SpinCount);
    }

    bool TryEnter(__in ULONG Timeout) {
        return StReentrantLock_TryEnterEx(&_rlock, Timeout) != FALSE;
    }

    bool TryEnter() {
        return StReentrantLock_TryEnter(&_rlock) != FALSE;
    }

    bool Enter() {
        return StReentrantLock_Enter(&_rlock) != FALSE;
    }

    void Exit() {
        StReentrantLock_Exit(&_rlock);
    }
};

#endif

/*++
 *
 * Registered wait object (used with Waitable synchronizers).
 *
 --*/

typedef VOID CALLBACK ST_WAIT_OR_TIMER_CALLBACK (__in_opt PVOID Context, __in ULONG WaitStatus);

//
// The registered wait structure.
//

#ifndef SLIM_THREADING_DLL

//
// The internal wait block structure.
//

struct _WAIT_BLOCK {
    LIST_ENTRY WaitListEntry;
    PST_PARKER Parker;
    PVOID Channel;
    LONG Request;
    SHORT WaitType;
    SHORT WaitKey;
};

#endif

typedef struct _ST_REGISTERED_WAIT {
    volatile PST_PARKER State;
    struct _ST_WAITABLE *Waitable;
    CB_PARKER CbParker;
    struct _WAIT_BLOCK WaitBlock;
    RAW_TIMER RawTimer;
    ST_WAIT_OR_TIMER_CALLBACK *UserCallback;
    PVOID UserCallbackContext;
    ULONG Timeout;
    ULONG ThreadId;
    volatile BOOLEAN ExecuteOnlyOnce;
} ST_REGISTERED_WAIT, *PST_REGISTERED_WAIT;


#ifdef __cplusplus
extern "C" {
#endif

//
// Unregisters the register wait from the associated Waitable.
//

STAPI
BOOL
WINAPI
StRegisteredWait_Unregister (
    __inout PST_REGISTERED_WAIT RegisteredWait
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StRegisteredWait {
    ST_REGISTERED_WAIT _reg_wait;
public:
    StRegisteredWait() {}

    bool Unregister() {
        return StRegisteredWait_Unregister(&_reg_wait) != FALSE;
    }
};

#endif

/*++
 *
 * Waitable interface, implemented by the Waitable synchronizers.
 *
 --*/

typedef struct _ST_WAITABLE {
    struct _WAITABLE_VTBL *Vptr;
} ST_WAITABLE, *PST_WAITABLE;

//
// The Waitable API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Signals the waitable synchronizer.
//

STAPI
BOOL
WINAPI
StWaitable_Signal (
    __inout PVOID Waitable
    );

//
// Waits until the waitable is signalled, activating the specified
// cancellers.
//

STAPI
ULONG
WINAPI
StWaitable_WaitOneEx (
    __inout PVOID Waitable,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Waits unconditionally until the waitable is signalled.
//

STAPI
BOOL
WINAPI
StWaitable_WaitOne (
    __inout PVOID Waitable
    );

//
// Waits until onde of the specified waitables is signalled, activating
// the specified cancellers.
//

STAPI
ULONG
WINAPI
StWaitable_WaitAnyEx (
    __in ULONG Count,
    __inout PVOID Waitables[],
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Waits unconditionally until one of the specified waitables is signalled.
//

STAPI
ULONG
WINAPI
StWaitable_WaitAny (
    __in ULONG Count,
    __inout PVOID Waitables[]
    );

//
// Waits until all specified waitables are signalled, activating the
// specified cancellers.
//

STAPI
ULONG
WINAPI
StWaitable_WaitAllEx (
    __in ULONG Count,
    __inout PVOID Waitables[],
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Waits unconditionally until all specified waitables are signalled.
//

STAPI
BOOL
WINAPI
StWaitable_WaitAll (
    __in ULONG Count,
    __inout PVOID Waitables[]
    );

//
// Signals a waitable and waits on another atomically, activating the
// specified cancellers.
//
    
STAPI
ULONG
WINAPI
StWaitable_SignalAndWaitEx (
    __inout PVOID WaitableToSignal,
    __inout PVOID WaitableToWait,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Signals a waitable and waits unconditionally on another atomically.
//
    
STAPI
BOOL
WINAPI
StWaitable_SignalAndWait (
    __inout PVOID WaitableToSignal,
    __inout PVOID WaitableToWait
    );

//
// Registers a wait with a Waitable.
//

STAPI
BOOL
WINAPI
StWaitable_RegisterWait (
    __inout PVOID Waitable,
    __out PST_REGISTERED_WAIT RegisteredWait,
    __in ST_WAIT_OR_TIMER_CALLBACK *UserCallback,
    __in_opt PVOID UserCallbackContext,
    __in ULONG Timeout,
    __in BOOL ExecuteOnlyOnce
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StWaitable {
protected:
    StWaitable() {}
public:

    bool Signal() {
        return StWaitable_Signal(this) != FALSE;
    }
    
    ULONG WaitOne(__in ULONG Timeout, StAlerter *Alerter = 0) {
        return StWaitable_WaitOneEx(this, Timeout, (PST_ALERTER)Alerter);
    }

    bool WaitOne() {
        return StWaitable_WaitOne(this) != FALSE;
    }

    static ULONG WaitAny(__in ULONG Count, __inout StWaitable* Waitables[],
                         __in ULONG Timeout, __inout_opt StAlerter* Alerter = 0) {
        return StWaitable_WaitAnyEx(Count, (PVOID *)Waitables, Timeout, (PST_ALERTER)Alerter);
    }

    static ULONG WaitAny(__in ULONG Count, __inout StWaitable* Waitables[]) {
        return StWaitable_WaitAny(Count, (PVOID *)Waitables);
    }

    static ULONG WaitAll(__in ULONG Count, __inout StWaitable* Waitables[], __in ULONG Timeout,
                         __inout_opt StAlerter* Alerter = 0) {
        return StWaitable_WaitAllEx(Count, (PVOID *)Waitables, Timeout, (PST_ALERTER)Alerter);
    }

    static bool WaitAll(__in ULONG Count, __inout StWaitable* Waitables[]) {
        return StWaitable_WaitAll(Count, (PVOID *)Waitables) != FALSE;
    }

    static ULONG SignalAndWait(__inout StWaitable* ToSignal, __inout StWaitable* ToWait,
                               __in ULONG Timeout, __inout_opt StAlerter* Alerter = 0) {
        return StWaitable_SignalAndWaitEx(ToSignal, ToWait, Timeout, (PST_ALERTER)Alerter);
    }

    static bool SignalAndWait(__inout StWaitable* ToSignal, __inout StWaitable* ToWait) {
        return StWaitable_SignalAndWait(ToSignal, ToWait) != FALSE;
    }

    static bool RegisterWait(__inout StWaitable *Waitable, __out StRegisteredWait* RegisteredWait,
                             __in ST_WAIT_OR_TIMER_CALLBACK *UserCallback,
                             __in_opt PVOID UserCallbackContext = 0, __in ULONG Timeout = INFINITE,
                             __in bool ExecuteOnlyOnce = false) {
                                 
        return StWaitable_RegisterWait(Waitable, (PST_REGISTERED_WAIT)RegisteredWait,
                                       UserCallback, UserCallbackContext, Timeout,
                                       ExecuteOnlyOnce) != FALSE;								 
    }
};

#endif

/*++
 *
 * Notification Event (Windows manual-reset event's semantics).
 *
 * NOTE: Implements the Waitable interface.
 *
 --*/

typedef struct _ST_NOTIFICATION_EVENT {
    ST_WAITABLE Waitable;
    volatile PLIST_ENTRY_ State;
    ULONG SpinCount;
} ST_NOTIFICATION_EVENT, *PST_NOTIFICATION_EVENT; 

//
// Notification event API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the event.
//

STAPI
VOID
WINAPI
StNotificationEvent_Init (
    __out PST_NOTIFICATION_EVENT Event,
    __in BOOL InitialState,
    __in ULONG SpinCount
    );

//
// Waits until the event is signalled, activating the specified
// cancellers.
//

STAPI
ULONG
WINAPI
StNotificationEvent_WaitEx (
    __inout PST_NOTIFICATION_EVENT Event,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Waits unconditionally until the event is signalled.
//

STAPI
BOOL
WINAPI
StNotificationEvent_Wait (
    __inout PST_NOTIFICATION_EVENT Event
    );

//
// Sets the event.
//

STAPI
BOOL
WINAPI
StNotificationEvent_Set (
    __inout PST_NOTIFICATION_EVENT Event
    );

//
// Resets the event.
//

STAPI
BOOL
WINAPI
StNotificationEvent_Reset (
    __inout PST_NOTIFICATION_EVENT Event
    );

//
// Returns true if the event is set.
//

STAPI
BOOL
WINAPI
StNotificationEvent_IsSet (
    __in PST_NOTIFICATION_EVENT Event
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StNotificationEvent : public StWaitable {
    ST_NOTIFICATION_EVENT _event;
public:

    StNotificationEvent(__in bool InitialState = false, __in ULONG SpinCount = 0) {
        StNotificationEvent_Init(&_event, InitialState, SpinCount);
    }

    void Init(__in bool InitialState = false, __in ULONG SpinCount = 0) {
        StNotificationEvent_Init(&_event, InitialState, SpinCount);
    }


    ULONG Wait(__in ULONG Timeout, __inout_opt StAlerter* Alerter = 0) {
        return StNotificationEvent_WaitEx(&_event, Timeout, (PST_ALERTER)Alerter);
    }

    bool Wait() {
        return StNotificationEvent_Wait(&_event) != FALSE;
    }

    bool Set() {
        return StNotificationEvent_Set(&_event) != FALSE;
    }

    bool Reset() {
        return StNotificationEvent_Reset(&_event) != FALSE;
    }

    bool IsSet() {
        return StNotificationEvent_IsSet(&_event) != FALSE;
    }
};

#endif

/*++
 *
 * Count Down Event.
 *
 * NOTE: Implements the Waitable interface.
 *
 --*/

typedef struct _ST_COUNT_DOWN_EVENT {
    ST_NOTIFICATION_EVENT Event;
    volatile LONG State;
} ST_COUNT_DOWN_EVENT, *PST_COUNT_DOWN_EVENT;

//
// Prototype of the callback function that, when specified, is called when
// the count down event reaches zero, but before to set the associated event.
//

typedef VOID CALLBACK ST_COUNT_DOWN_EVENT_ACTION(PVOID Context);

//
// Count Down Event API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the count down event.
//

STAPI
VOID
WINAPI
StCountDownEvent_Init (
    __out PST_COUNT_DOWN_EVENT CountDown,
    __in LONG Count,
    __in ULONG SpinCount
    );

//
// Signals the count down event.
//

STAPI
BOOL
WINAPI
StCountDownEvent_Signal (
    __inout PST_COUNT_DOWN_EVENT CountDown,
    __in LONG Count 
    );

//
// Signals the count down event specifying a signalled action.
//

STAPI
BOOL
WINAPI
StCountDownEvent_SignalEx (
    __inout PST_COUNT_DOWN_EVENT CountDown,
    __in LONG Count,
    __in ST_COUNT_DOWN_EVENT_ACTION *Action,
    __in PVOID ActionContext
    );

//
// Tries to add the specified amount to the count down event
// if if its value is greater than zero.
//

STAPI
BOOL
WINAPI
StCountDownEvent_TryAdd (
    __inout PST_COUNT_DOWN_EVENT CountDown,
    __in LONG Count
    );

//
// Waits until the count down event reaches zero, activating the
// specified cancellers.
//

STAPI
ULONG
WINAPI
StCountDownEvent_WaitEx (
    __inout PST_COUNT_DOWN_EVENT CountDown,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Waits unconditionally until the count down event reaches zero.
//

STAPI
BOOL
WINAPI
StCountDownEvent_Wait (
    __inout PST_COUNT_DOWN_EVENT CountDown
    );

//
// Returns the current value of the count down event.
//

STAPI
LONG
WINAPI
StCountDownEvent_Read (
    __inout PST_COUNT_DOWN_EVENT CountDown
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StCountDownEvent : public StWaitable {
    ST_COUNT_DOWN_EVENT _cdevent;
public:

    StCountDownEvent(__in LONG Count = 1, __in ULONG SpinCount = 0) {
        StCountDownEvent_Init(&_cdevent, Count, SpinCount);
    }

    void Init(__in LONG Count = 1, __in ULONG SpinCount = 0) {
        StCountDownEvent_Init(&_cdevent, Count, SpinCount);
    }

    bool Signal(__in LONG Count = 1) {
        return StCountDownEvent_Signal(&_cdevent, Count) != FALSE;
    }

    bool Signal(__in LONG Count, __in ST_COUNT_DOWN_EVENT_ACTION *Action,
                __in PVOID ActionContext = 0) {
        return StCountDownEvent_SignalEx(&_cdevent, Count, Action, ActionContext) != FALSE;
    }

    bool TryAdd(__in LONG Count = 1) {
        return StCountDownEvent_TryAdd(&_cdevent, Count) != FALSE;
    }

    ULONG Wait(__in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StCountDownEvent_WaitEx(&_cdevent, Timeout, (PST_ALERTER)Alerter);
    }

    bool Wait() {
        return StCountDownEvent_Wait(&_cdevent) != FALSE;
    }

    LONG Read() {
        return StCountDownEvent_Read(&_cdevent);
    }
};

#endif

/*++
 *
 * Future.
 *
 * NOTE: Implements the Waitable interface.
 *
 --*/

typedef struct _ST_FUTURE {
    ST_NOTIFICATION_EVENT IsAvailable;
    volatile LONG State;
    ULONG_PTR Value;
} ST_FUTURE, *PST_FUTURE;

//
// Future API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the future.
//

STAPI
VOID
WINAPI
StFuture_Init (
    __out PST_FUTURE Future,
    __in ULONG SpinCount
    );

//
// Sets the future's value.
//

STAPI
BOOL
WINAPI
StFuture_SetValue (
    __inout PST_FUTURE Future,
    __in ULONG_PTR Value
    );

//
// Gets the the future's value, activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StFuture_GetValueEx (
    __inout PST_FUTURE Future,
    __out ULONG_PTR *Result,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Gets the the future's value if it is available.
//

STAPI
BOOL
WINAPI
StFuture_TryGetValue (
    __inout PST_FUTURE Future,
    __out ULONG_PTR *Result
    );

//
// Gets the the future's value unconditionally.
//

STAPI
BOOL
WINAPI
StFuture_GetValue (
    __inout PST_FUTURE Future,
    __out ULONG_PTR *Result
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StFuture : public StWaitable {
    ST_FUTURE _future;
public:

    StFuture(__in ULONG SpinCount = 0) {
        StFuture_Init(&_future, SpinCount);
    }

    void Init(__in ULONG SpinCount = 0) {
        StFuture_Init(&_future, SpinCount);
    }

    void Reset(__in ULONG SpinCount = 0) {
        StFuture_Init(&_future, SpinCount);
    }

    bool SetValue(__in ULONG_PTR Value) {
        return StFuture_SetValue(&_future, Value) != FALSE;
    }

    bool TryGetValue(__out ULONG_PTR *Result) {
        return StFuture_TryGetValue(&_future, Result) != FALSE;
    }

    ULONG GetValue(__out ULONG_PTR *Result, __in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StFuture_GetValueEx(&_future, Result, Timeout, (PST_ALERTER)Alerter);
    }

    bool GetValue(__out ULONG_PTR *Result) {
        return StFuture_GetValue(&_future, Result) != FALSE;
    }
};

#endif

/*++
 *
 * Synchonization Event (Windows auto-reset event's semantics).
 *
 * NOTE: Implements the Waitable interface.
 *
 --*/

//
// Locked Queue (an internal type).
//

typedef struct __LOCKED_QUEUE {
    volatile PLIST_ENTRY_ LockState;
    PLIST_ENTRY_ LockPrivateQueue;
    LIST_ENTRY_ Head;
    volatile LONG FrontRequest;
    ULONG SpinCount;
} LOCKED_QUEUE, *PLOCKED_QUEUE;

typedef struct _ST_SYNCHRONIZATION_EVENT {
    ST_WAITABLE Waitable;
    volatile LONG State;
    LOCKED_QUEUE Queue;
    ULONG SpinCount;
} ST_SYNCHRONIZATION_EVENT, *PST_SYNCHRONIZATION_EVENT;

//
// Synchronization Event API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the event.
//

STAPI
VOID
WINAPI
StSynchronizationEvent_Init (
    __out PST_SYNCHRONIZATION_EVENT Event,
    __in BOOL InitialState,
    __in ULONG SpinCount
    );

//
// Sets the event.
//

STAPI
BOOL
WINAPI
StSynchronizationEvent_Set (
    __inout PST_SYNCHRONIZATION_EVENT Event
    );

//
// Resets the event.
//

STAPI
BOOL
WINAPI
StSynchronizationEvent_Reset (
    __inout PST_SYNCHRONIZATION_EVENT Event
    );

//
// Waits until the event is set, activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StSynchronizationEvent_WaitEx (
    __inout PST_SYNCHRONIZATION_EVENT Event,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Waits unconditionally until the event is set.
//

STAPI
BOOL
WINAPI
StSynchronizationEvent_Wait (
    __inout PST_SYNCHRONIZATION_EVENT Event
    );

//
// Returns true if the event is set.
//

STAPI
BOOL
WINAPI
StSynchronizationEvent_IsSet (
    __in PST_SYNCHRONIZATION_EVENT Event
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StSynchronizationEvent : public StWaitable {
    ST_SYNCHRONIZATION_EVENT _event;
public:

    StSynchronizationEvent(__in bool InitialState = false, __in ULONG SpinCount = 0) {
        StSynchronizationEvent_Init(&_event, InitialState, SpinCount);
    }

    void Init(__in bool InitialState = false, __in ULONG SpinCount = 0) {
        StSynchronizationEvent_Init(&_event, InitialState, SpinCount);
    }

    bool Set() {
        return StSynchronizationEvent_Set(&_event) != FALSE;
    }

    bool Reset() {
        return StSynchronizationEvent_Reset(&_event) != FALSE;
    }

    ULONG Wait(__in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StSynchronizationEvent_WaitEx(&_event, Timeout, (PST_ALERTER)Alerter);
    }

    bool Wait() {
        return StSynchronizationEvent_Wait(&_event) != FALSE;
    }

    bool IsSet() {
        return StSynchronizationEvent_IsSet(&_event) != FALSE;
    }
};

#endif

/*++
 *
 * Non-reentrant Fair Lock.
 *
 * NOTE: Implements the Waitable interface.
 *
 --*/

typedef struct _ST_FAIR_LOCK {
    ST_WAITABLE Waitable;
    volatile LONG State;
    LOCKED_QUEUE Queue;
    ULONG SpinCount;
} ST_FAIR_LOCK, *PST_FAIR_LOCK;

//
// Fair Lock API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the lock.
//

STAPI
VOID
WINAPI
StFairLock_Init (
    __out PST_FAIR_LOCK Lock,
    __in ULONG SpinCount
    );

//
// Tries to acquire the lock.
//

STAPI
ULONG
WINAPI
StFairLock_TryEnterEx (
    __inout PST_FAIR_LOCK Lock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Tries to acquire the lock immediately.
//

STAPI
BOOL
WINAPI
StFairLock_TryEnter (
    __inout PST_FAIR_LOCK Lock
    );

//
// Acquires the lock.
//

STAPI
BOOL
WINAPI
StFairLock_Enter (
    __inout PST_FAIR_LOCK Lock
    );

//
// Exits the lock.
//

STAPI
VOID
WINAPI
StFairLock_Exit (
    __inout PST_FAIR_LOCK Lock
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StFairLock : public StWaitable {
    ST_FAIR_LOCK _lock;
public:
    StFairLock(__in ULONG SpinCount = 0) {
        StFairLock_Init(&_lock, SpinCount);
    }

    void Init(__in ULONG SpinCount = 0) {
        StFairLock_Init(&_lock, SpinCount);
    }

    ULONG TryEnter(__in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StFairLock_TryEnterEx(&_lock, Timeout, (PST_ALERTER)Alerter);
    }

    bool TryEnter() {
        return StFairLock_TryEnter(&_lock) != FALSE;
    }

    bool Enter() {
        return StFairLock_Enter(&_lock) != FALSE;
    }

    void Exit() {
        StFairLock_Exit(&_lock);
    }
};

#endif

/*++
 *
 * Reentrant Fair Lock (Windows mutex's semantics).
 *
 * NOTE: Implements the Waitable interface.
 *
 --*/

typedef struct _ST_REENTRANT_FAIR_LOCK {
    ST_WAITABLE Waitable;
    ST_FAIR_LOCK Lock;
    ULONG Owner;
    LONG Count;
} ST_REENTRANT_FAIR_LOCK, *PST_REENTRANT_FAIR_LOCK;

//
// Fair Lock API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the lock.
//

STAPI
VOID
WINAPI
StReentrantFairLock_Init (
    __out PST_REENTRANT_FAIR_LOCK Lock,
    __in ULONG SpinCount
    );

//
// Tries to enter the lock, activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StReentrantFairLock_TryEnterEx (
    __inout PST_REENTRANT_FAIR_LOCK Lock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Tries to enter the lock immediately.
//

STAPI
BOOL
WINAPI
StReentrantFairLock_TryEnter (
    __inout PST_REENTRANT_FAIR_LOCK Lock
    );

//
// Enters the lock unconditionally.
//

STAPI
BOOL
WINAPI
StReentrantFairLock_Enter (
    __inout PST_REENTRANT_FAIR_LOCK Lock
    );

//
// Exits the lock once.
//

STAPI
BOOL
WINAPI
StReentrantFairLock_Exit (
    __inout PST_REENTRANT_FAIR_LOCK Lock
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StReentrantFairLock : public StWaitable {
    ST_REENTRANT_FAIR_LOCK _lock;
public:

    StReentrantFairLock(__in ULONG SpinCount = 0) {
        StReentrantFairLock_Init(&_lock, SpinCount);
    }

    void Init(__in ULONG SpinCount = 0) {
        StReentrantFairLock_Init(&_lock, SpinCount);
    }

    ULONG TryEnter(__in ULONG Timeout, __inout_opt StAlerter *Alerter) {
        return StReentrantFairLock_TryEnterEx(&_lock, Timeout, (PST_ALERTER)Alerter);
    }

    bool TryEnter() {
        return StReentrantFairLock_TryEnter(&_lock) != FALSE;
    }

    bool Enter() {
        return StReentrantFairLock_Enter(&_lock) != FALSE;
    }

    bool Exit() {
        return StReentrantFairLock_Exit(&_lock) != FALSE;
    }
};

#endif

/*++
 *
 * Semaphore (Windows semaphore's semantics).
 *
 * NOTE: Implements the Waitable interface.
 *
 --*/

typedef struct _ST_SEMAPHORE {
    ST_WAITABLE Waitable;
    volatile LONG State;
    LOCKED_QUEUE Queue;
    LONG MaximumCount;
    ULONG SpinCount;
} ST_SEMAPHORE, *PST_SEMAPHORE;

//
// Semaphore API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the semaphore.
//

STAPI
VOID
WINAPI
StSemaphore_Init (
    __out PST_SEMAPHORE Semaphore,
    __in LONG InitialCount,
    __in LONG MaximumCount,
    __in ULONG SpinCount
    );

//
//
// Tries to acquire the specified permits from the semaphore, activating
// the specified cancellers.
//

STAPI
ULONG
WINAPI
StSemaphore_TryAcquireEx (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG AcquireCount,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
//
// Tries to acquire the specified permits from the semaphore immediately.
//

STAPI
BOOL
WINAPI
StSemaphore_TryAcquire (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG AcquireCount
    );

//
// Acquires unconditionally the specified permits from the semaphore.
//

STAPI
BOOL
WINAPI
StSemaphore_Acquire (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG AcquireCount
    );

//
// Returns the specified number of permits to the semaphore.
//

STAPI
BOOL
WINAPI
StSemaphore_Release (
    __inout PST_SEMAPHORE Semaphore,
    __in LONG ReleaseCount
    );

//
// Returns the number of available permits on the semaphore.
//

STAPI
LONG
WINAPI
StSemaphore_Read (
    __in PST_SEMAPHORE Semaphore
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StSemaphore : public StWaitable {
    ST_SEMAPHORE _semaphore;
public:

    StSemaphore(__in LONG InitialCount = 0, __in LONG MaximumCount = ~(1 << 31),
                __in ULONG SpinCount = 0) {
        StSemaphore_Init(&_semaphore, InitialCount, MaximumCount, SpinCount);
    }

    void Init(__in LONG InitialCount = 0, __in LONG MaximumCount = ~(1 << 31),
              __in ULONG SpinCount = 0) {
        StSemaphore_Init(&_semaphore, InitialCount, MaximumCount, SpinCount);
    }

    bool TryAcquire(__in LONG AcquireCount = 1) {
        return StSemaphore_TryAcquire(&_semaphore, AcquireCount) != FALSE;
    }

    ULONG TryAcquire(__in LONG AcquireCount, __in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StSemaphore_TryAcquireEx(&_semaphore, AcquireCount, Timeout,
                                        __inout_opt (PST_ALERTER)Alerter);
    }

    bool Acquire(__in LONG AcquireCount = 1) {
        return StSemaphore_Acquire(&_semaphore, AcquireCount) != FALSE;
    }

    bool Release(__in LONG ReleaseCount = 1) {
        return StSemaphore_Release(&_semaphore, ReleaseCount) != FALSE;
    }

    LONG Read() {
        return StSemaphore_Read(&_semaphore);
    }
};

#endif

/*++
 *
 * Read Write Lock (Similar to Windows Slim Reader/Writer lock).
 *
 * NOTE: Implements the Waitable interface on the write lock.
 *
 --*/

typedef struct _ST_READ_WRITE_LOCK {
    ST_WAITABLE Waitable;
    volatile LONG State;
    LOCKED_QUEUE Queue;
    ULONG SpinCount;
} ST_READ_WRITE_LOCK, *PST_READ_WRITE_LOCK;

//
// Read Write Lock API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the read write lock.
//

STAPI
VOID
WINAPI
StReadWriteLock_Init (
    __out PST_READ_WRITE_LOCK RWLock,
    __in ULONG SpinCount
    );

//
// Tries to enter the read lock, activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StReadWriteLock_TryEnterReadEx (
    __inout PST_READ_WRITE_LOCK RWLock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Tries to enter the read lock immediately.
//

STAPI
BOOL
WINAPI
StReadWriteLock_TryEnterRead (
    __inout PST_READ_WRITE_LOCK RWLock
    );

//
// Enters the read lock unconditionally.
//

STAPI
BOOL
WINAPI
StReadWriteLock_EnterRead (
    __inout PST_READ_WRITE_LOCK RWLock
    );

//
// Tries to enter the write lock activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StReadWriteLock_TryEnterWriteEx (
    __inout PST_READ_WRITE_LOCK RWLock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Tries to enter the write lock immediately.
//

STAPI
BOOL
WINAPI
StReadWriteLock_TryEnterWrite (
    __inout PST_READ_WRITE_LOCK RWLock
    );

//
// Enters the write lock unconditionally.
//

STAPI
BOOL
WINAPI
StReadWriteLock_EnterWrite (
    __inout PST_READ_WRITE_LOCK RWLock
    );

//
// Exits the read lock.
//

STAPI
VOID
WINAPI
StReadWriteLock_ExitRead (
    __inout PST_READ_WRITE_LOCK RWLock
    );

//
// Exits the write lock.
//

STAPI
VOID
WINAPI
StReadWriteLock_ExitWrite (
    __in PST_READ_WRITE_LOCK RWLock
    );


#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StReadWriteLock : public StWaitable {
    ST_READ_WRITE_LOCK _rwlock;
public:

    StReadWriteLock(__in ULONG SpinCount = 0) {
        StReadWriteLock_Init(&_rwlock, SpinCount);
    }

    void Init(__in ULONG SpinCount = 0) {
        StReadWriteLock_Init(&_rwlock, SpinCount);
    }

    bool TryEnterRead() {
        return StReadWriteLock_TryEnterRead(&_rwlock) != FALSE;
    }

    ULONG TryEnterRead(__in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StReadWriteLock_TryEnterReadEx(&_rwlock, Timeout, (PST_ALERTER)Alerter);
    }

    bool EnterRead() {
        return StReadWriteLock_EnterRead(&_rwlock) != FALSE;
    }

    bool TryEnterWrite() {
        return StReadWriteLock_TryEnterWrite(&_rwlock) != FALSE;
    }

    ULONG TryEnterWrite(__in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StReadWriteLock_TryEnterWriteEx(&_rwlock, Timeout, (PST_ALERTER)Alerter);
    }

    bool EnterWrite() {
        return StReadWriteLock_EnterWrite(&_rwlock) != FALSE;
    }

    void ExitRead() {
        StReadWriteLock_ExitRead(&_rwlock);
    }

    void ExitWrite() {
        StReadWriteLock_ExitWrite(&_rwlock);
    }
};

#endif

/*++
 *
 * Reentrant Read Write Lock.
 *
 * NOTE: Implements the Waitable interface on the write lock.
 *
 --*/

typedef struct _ST_REENTRANT_READ_WRITE_LOCK {
    ST_WAITABLE Waitable;
    ST_READ_WRITE_LOCK Lock;
    ULONG Writer;
    ULONG Count;
    ULONG FirstReader;
    ULONG FirstReaderCount;
} ST_REENTRANT_READ_WRITE_LOCK, *PST_REENTRANT_READ_WRITE_LOCK;

//
// Reentrant Read Write Lock API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the reentrant read write lock.
//

STAPI
VOID
WINAPI
StReentrantReadWriteLock_Init (
    __out PST_REENTRANT_READ_WRITE_LOCK RWLock,
    __in ULONG SpinCount
    );

//
// Tries to enter the read lock, activating the specifed cancellers.
//

STAPI
ULONG
WINAPI
StReentrantReadWriteLock_TryEnterReadEx (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER ALerter
    );

//
// Tries to enter the read lock immediately.
//

STAPI
BOOL
WINAPI
StReentrantReadWriteLock_TryEnterRead (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock
    );

//
// Enters the read lock unconditionally.
//

STAPI
BOOL
WINAPI
StReentrantReadWriteLock_EnterRead (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock
    );

//
// Tries to enter the write lock, activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StReentrantReadWriteLock_TryEnterWriteEx (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER ALerter
    );

//
// Tries to enter the write lock immediately.
//

STAPI
BOOL
WINAPI
StReentrantReadWriteLock_TryEnterWrite (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock
    );

//
// Enters the write lock unconditionally.
//

STAPI
BOOL
WINAPI
StReentrantReadWriteLock_EnterWrite (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock
    );

//
// Exits the read lock once.
//

STAPI
BOOL
WINAPI
StReentrantReadWriteLock_ExitRead (
    __inout PST_REENTRANT_READ_WRITE_LOCK RWLock
    );

//
// Exits the write lock once.
//

STAPI
BOOL
WINAPI
StReentrantReadWriteLock_ExitWrite (
    __in PST_REENTRANT_READ_WRITE_LOCK RWLock
    );


#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StReentrantReadWriteLock : public StWaitable {
    ST_REENTRANT_READ_WRITE_LOCK _rwlock;
public:

    StReentrantReadWriteLock(__in ULONG SpinCount = 0) {
        StReentrantReadWriteLock_Init(&_rwlock, SpinCount);
    }

    void Init(__in ULONG SpinCount = 0) {
        StReentrantReadWriteLock_Init(&_rwlock, SpinCount);
    }

    bool TryEnterRead() {
        return StReentrantReadWriteLock_TryEnterRead(&_rwlock) != FALSE;
    }

    ULONG TryEnterRead(__in ULONG Timeout, __inout_opt StAlerter *ALerter = 0) {
        return StReentrantReadWriteLock_TryEnterReadEx(&_rwlock, Timeout, (PST_ALERTER)ALerter);
    }

    bool EnterRead() {
        return StReentrantReadWriteLock_EnterRead(&_rwlock) != FALSE;
    }

    bool TryEnterWrite() {
        return StReentrantReadWriteLock_TryEnterWrite(&_rwlock) != FALSE;
    }

    ULONG TryEnterWrite(__in ULONG Timeout, __inout_opt StAlerter *ALerter = 0) {
        return StReentrantReadWriteLock_TryEnterWriteEx(&_rwlock, Timeout, (PST_ALERTER)ALerter);
    }

    bool EnterWrite() {
        return StReentrantReadWriteLock_EnterWrite(&_rwlock) != FALSE;
    }

    bool ExitRead() {
        return StReentrantReadWriteLock_ExitRead(&_rwlock) != FALSE;
    }

    bool ExitWrite() {
        return StReentrantReadWriteLock_ExitWrite(&_rwlock) != FALSE;
    }
};

#endif

/*++
 *
 * Read Upgrade Write Lock.
 *
 --*/

typedef struct _ST_READ_UPGRADE_WRITE_LOCK {
    SPINLOCK SLock;
    ULONG Readers;
    ULONG Writer;
    ULONG WrCount;
    ULONG Upgrader;
    ULONG UpgCount;
    LIST_ENTRY_ ReadersQueue;
    LIST_ENTRY_ WritersQueue;
    LIST_ENTRY_ UpgradersQueue;
    PLIST_ENTRY_ UpgradeToWriteWaiter;
    ULONG SpinCount;
    BOOLEAN IsReentrant;
    BOOLEAN UpgraderIsReader;
} ST_READ_UPGRADE_WRITE_LOCK, *PST_READ_UPGRADE_WRITE_LOCK;

//
// Read Upgrade Write Lock API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the read upgrade write lock.
//

STAPI
VOID
WINAPI
StReadUpgradeWriteLock_Init (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock,
    __in BOOL SupportsRecursion,
    __in ULONG SpinCount
    );

//
// Tries to enter the read lock immediately.
//

STAPI
BOOL
WINAPI
StReadUpgradeWriteLock_TryEnterRead (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock
    );

//
// Tries to enter the read lock, activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StReadUpgradeWriteLock_TryEnterReadEx (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER ALerter
    );

//
// Enters the read lock unconditionally.
//

STAPI
BOOL
WINAPI
StReadUpgradeWriteLock_EnterRead (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock
    );

//
// Tries to enter the lock in upgradeable read mode immediately.
//

STAPI
BOOL
WINAPI
StReadUpgradeWriteLock_TryEnterUpgradeableRead (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock
    );



//
// Tries to enter the lock in upgradeable read mode, activating
// the specified cancellers.
//

STAPI
ULONG
WINAPI
StReadUpgradeWriteLock_TryEnterUpgradeableReadEx (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER ALerter
    );

//
// Enters the lock in upgradeable read mode unconditionally.
//

STAPI
BOOL
WINAPI
StReadUpgradeWriteLock_EnterUpgradeableRead (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock
    );

//
// Tries to enter the write lock immediately.
//

STAPI
BOOL
WINAPI
StReadUpgradeWriteLock_TryEnterWrite (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock
    );

//
// Tries to enter the write lock, activating the specified
// cancellers.
//

STAPI
ULONG
WINAPI
StReadUpgradeWriteLock_TryEnterWriteEx (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER ALerter
    );

//
// Enters the write lock unconditionally.
//

STAPI
BOOL
WINAPI
StReadUpgradeWriteLock_EnterWrite (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock
    );

//
// Exits the read lock once once.
//

STAPI
BOOL
WINAPI
StReadUpgradeWriteLock_ExitRead (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock
    );

//
// Exits the upgradeable read lock once.
//

STAPI
BOOL
WINAPI
StReadUpgradeWriteLock_ExitUpgradeableRead (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock
    );

//
// Exits the write lock.
//

STAPI
BOOL
WINAPI
StReadUpgradeWriteLock_ExitWrite (
    __out PST_READ_UPGRADE_WRITE_LOCK Lock
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StReadUpgradeWriteLock {
    ST_READ_UPGRADE_WRITE_LOCK _ruwlock;
public:

    StReadUpgradeWriteLock(__in bool SupportsRecursion = false, __in ULONG SpinCount = 0) {
        StReadUpgradeWriteLock_Init(&_ruwlock, SupportsRecursion, SpinCount);
    }

    void Init(__in bool SupportsRecursion = false, __in ULONG SpinCount = 0) {
        StReadUpgradeWriteLock_Init(&_ruwlock, SupportsRecursion, SpinCount);
    }

    bool TryEnterRead() {
        return StReadUpgradeWriteLock_TryEnterRead(&_ruwlock) != FALSE;
    }

    ULONG TryEnterRead(__in ULONG Timeout, __inout_opt StAlerter *ALerter = 0) {
        return StReadUpgradeWriteLock_TryEnterReadEx(&_ruwlock, Timeout, (PST_ALERTER)ALerter);
    }

    bool EnterRead() {
        return StReadUpgradeWriteLock_EnterRead(&_ruwlock) != FALSE;
    }

    bool TryEnterUpgradeableRead() {
        return StReadUpgradeWriteLock_TryEnterUpgradeableRead(&_ruwlock) != FALSE;
    }

    ULONG TryEnterUpgradeableRead(__in ULONG Timeout, __inout_opt StAlerter *ALerter = 0) {
        return StReadUpgradeWriteLock_TryEnterUpgradeableReadEx(&_ruwlock, Timeout,
                                                                (PST_ALERTER)ALerter);
    }

    bool EnterUpgradeableRead() {
        return StReadUpgradeWriteLock_EnterUpgradeableRead(&_ruwlock) != FALSE;
    }

    bool TryEnterWrite() {
        return StReadUpgradeWriteLock_TryEnterWrite(&_ruwlock) != FALSE;
    }

    ULONG TryEnterWrite(__in ULONG Timeout, __inout_opt StAlerter *ALerter = 0) {
        return StReadUpgradeWriteLock_TryEnterWriteEx(&_ruwlock, Timeout, (PST_ALERTER)ALerter);
    }

    bool EnterWrite() {
        return StReadUpgradeWriteLock_EnterWrite(&_ruwlock) != FALSE;
    }

    bool ExitRead() {
        return StReadUpgradeWriteLock_ExitRead(&_ruwlock) != FALSE;
    }

    bool ExitUpgradeableRead() {
        return StReadUpgradeWriteLock_ExitUpgradeableRead(&_ruwlock) != FALSE;
    }

    bool ExitWrite() {
        return StReadUpgradeWriteLock_ExitWrite(&_ruwlock) != FALSE;
    }
};

#endif

/*++
 *
 * Barrier.
 *
 --*/

//
// The post-phase barrier action callback.
//

typedef VOID CALLBACK BARRIER_ACTION(PVOID Context);

//
// Barrier phase state (an internal type).
//

typedef struct _PHASE_STATE {
    volatile ULONG State;
    ST_NOTIFICATION_EVENT Event;
} PHASE_STATE, *PPHASE_STATE;

typedef struct _ST_BARRIER {
    volatile PPHASE_STATE PhaseState;
    ULONGLONG PhaseNumber;
    BARRIER_ACTION *Action;
    PVOID ActionContext;
    PHASE_STATE States[2];
} ST_BARRIER, *PST_BARRIER;

//
// Barrier API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the barrier.
//

STAPI
BOOL
WINAPI
StBarrier_Init (
    __out PST_BARRIER Barrier,
    __in USHORT Partners,
    __in BARRIER_ACTION *Action,
    __in PVOID ActionContext
    );

//
// Signals the barrier and then waits until the current phase
// completes, activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StBarrier_SignalAndWaitEx (
    __inout PST_BARRIER Barrier,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Signals the barrier and then waits unconditionally until
// the current phase completes.
//

STAPI
BOOL
WINAPI
StBarrier_SignalAndWait (
    __inout PST_BARRIER Barrier
    );

//
// Adds a partner.
//

STAPI
BOOL
WINAPI
StBarrier_AddPartner (
    __inout PST_BARRIER Barrier,
    __out_opt ULONGLONG *PhaseNumber
    );

//
// Adds the specified number of partners.
//

STAPI
BOOL
WINAPI
StBarrier_AddPartners (
    __inout PST_BARRIER Barrier,
    __in USHORT Count,
    __out_opt ULONGLONG *PhaseNumber
    );

//
// Removes a partner.
//

STAPI
BOOL
WINAPI
StBarrier_RemovePartner (
    __inout PST_BARRIER Barrier
    );

//
// Removes the specified number of partners.
//

STAPI
BOOL
WINAPI
StBarrier_RemovePartners (
    __inout PST_BARRIER Barrier,
    __in USHORT Count
    );

//
// Returns the current phase number of the barrier.
//

STAPI
ULONGLONG
WINAPI
StBarrier_GetPhaseNumber (
    __in PST_BARRIER Barrier
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StBarrier {
    ST_BARRIER _barrier;
public:

    StBarrier(__in USHORT Partners = 1, __in BARRIER_ACTION *Action = 0,
              __in PVOID ActionContext = 0) {
        StBarrier_Init(&_barrier, Partners, Action, ActionContext);
    }

    void Init(__in USHORT Partners = 1, __in BARRIER_ACTION *Action = 0,
              __in PVOID ActionContext = 0) {
        StBarrier_Init(&_barrier, Partners, Action, ActionContext);
    }

    ULONG SignalAndWait(__in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StBarrier_SignalAndWaitEx(&_barrier, Timeout, (PST_ALERTER)Alerter);
    }

    bool SignalAndWait() {
        return StBarrier_SignalAndWait(&_barrier) != FALSE;
    }

    bool AddPartners(__in USHORT Count, __out_opt ULONGLONG *PhaseNumber = 0) {	
        return StBarrier_AddPartners(&_barrier, Count, PhaseNumber) != FALSE;
    }

    bool AddPartner(__out_opt ULONGLONG *PhaseNumber = 0) {	
        return StBarrier_AddPartner(&_barrier, PhaseNumber) != FALSE;
    }

    bool RemovePartners(__in USHORT Count = 1) {
        return StBarrier_RemovePartners(&_barrier, Count) != FALSE;
    }

    bool RemovePartner() {
        return StBarrier_RemovePartner(&_barrier) != FALSE;
    }

    ULONGLONG GetPhaseNumber() {
        return StBarrier_GetPhaseNumber(&_barrier);
    }
};

#endif

/*++
 *
 * Condition Variable.
 *
 --*/

typedef struct ST_CONDITION_VARIABLE {
    struct _ILOCK_VTBL *LockVtbl;
    LIST_ENTRY_ WaitList;
} ST_CONDITION_VARIABLE, *PST_CONDITION_VARIABLE;

//
// Condition Variable  API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initialize the condition variable to be used with a non-fair lock.
//

STAPI
VOID
WINAPI
StConditionVariable_InitForLock (
  __out PST_CONDITION_VARIABLE Condition
    );

//
// Initialize the condition variable to be used with a reentrant
// non-fair lock.
//

STAPI
VOID
WINAPI
StConditionVariable_InitForReentrantLock (
  __out PST_CONDITION_VARIABLE Condition
    );

//
// Initialize the condition variable to be used with a fair lock.
//

STAPI
VOID
WINAPI
StConditionVariable_InitForFairLock (
  __out PST_CONDITION_VARIABLE Condition
    );

//
// Initialize the condition variable to be used with a reentrant fair lock.
//

STAPI
VOID
WINAPI
StConditionVariable_InitForReentrantFairLock (
  __out PST_CONDITION_VARIABLE Condition
    );

//
// Initialize the condition variable to be used with the write lock
// of a read write Lock.
//

STAPI
VOID
WINAPI
StConditionVariable_InitForReadWriteLock (
  __out PST_CONDITION_VARIABLE Condition
    );

//
// Initialize the condition variable to be used with the write lock
// of a reentrant read write Lock.
//

STAPI
VOID
WINAPI
StConditionVariable_InitForReentrantReadWriteLock (
  __out PST_CONDITION_VARIABLE Condition
    );

//
// Waits on the condition variable, activating the specified
// cancellers.
//

STAPI
ULONG
WINAPI
StConditionVariable_WaitEx (
  __inout PST_CONDITION_VARIABLE Condition,
  __inout PVOID Lock,
  __in ULONG Timeout,
  __inout PST_ALERTER Alerter
    );

//
// Waits uncontionally on the condition variable.
//

STAPI
BOOL
WINAPI
StConditionVariable_Wait (
  __inout PST_CONDITION_VARIABLE Condition,
  __inout PVOID Lock
    );

//
// Notifies a thread waiting on the condition variable.
//

STAPI
VOID
WINAPI
StConditionVariable_Notify (
    __inout PST_CONDITION_VARIABLE Condition
    );

//
// Notifies all threads waiting on the condition variable.
//
    
STAPI
VOID
WINAPI
StConditionVariable_NotifyAll (
  __inout PST_CONDITION_VARIABLE Condition
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StConditionVariable {
    ST_CONDITION_VARIABLE _condition;
public:

    StConditionVariable(StLock *Ignored) {
        StConditionVariable_InitForLock(&_condition);
    }

    StConditionVariable(StReentrantLock *Ignored = 0) {
        StConditionVariable_InitForReentrantLock(&_condition);
    }

    StConditionVariable(StFairLock *Ignored) {
        StConditionVariable_InitForFairLock(&_condition);
    }

    StConditionVariable(StReentrantFairLock *Ignored) {
        StConditionVariable_InitForReentrantFairLock(&_condition);
    }

    StConditionVariable(StReadWriteLock *Ignored) {
        StConditionVariable_InitForReadWriteLock(&_condition);
    }

    StConditionVariable(StReentrantReadWriteLock *Ignored) {
        StConditionVariable_InitForReentrantReadWriteLock(&_condition);
    }
    
    void Init(StLock *Ignored) {
        StConditionVariable_InitForLock(&_condition);
    }

    void Init(StReentrantLock *Ignored) {
        StConditionVariable_InitForReentrantLock(&_condition);
    }

    void Init(StFairLock *Ignored) {
        StConditionVariable_InitForFairLock(&_condition);
    }

    void Init(StReentrantFairLock *Ignored) {
        StConditionVariable_InitForReentrantFairLock(&_condition);
    }

    void Init(StReadWriteLock *Ignored) {
        StConditionVariable_InitForReadWriteLock(&_condition);
    }

    void Init(StReentrantReadWriteLock *Ignored) {
        StConditionVariable_InitForReentrantReadWriteLock(&_condition);
    }

    ULONG Wait(__inout PVOID Lock, __in ULONG Timeout, __inout StAlerter *Alerter = 0) {
        return StConditionVariable_WaitEx(&_condition, Lock, Timeout, (PST_ALERTER)Alerter);
    }

    bool Wait(__inout PVOID Lock) {
        return StConditionVariable_Wait(&_condition, Lock) != FALSE;
    }

    void Notify() {
        StConditionVariable_Notify(&_condition);
    }

    void NotifyAll() {
        StConditionVariable_NotifyAll(&_condition);
    }
};

#endif

/*++
 *
 * Rendezvous Channel.
 *
 --*/

typedef struct _ST_RENDEZVOUS_CHANNEL {
    SPINLOCK Lock;
    LIST_ENTRY_ SendWaitList;
    LIST_ENTRY_ ReceiveWaitList;
    LONG Flags;
} ST_RENDEZVOUS_CHANNEL, *PST_RENDEZVOUS_CHANNEL;

//
// Rendezvous channel API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the rendezvous channel.
//

STAPI
VOID
WINAPI
StRendezvousChannel_Init (
    __out PST_RENDEZVOUS_CHANNEL Channel,
    __in BOOL LifoReceive
    );

//
// Sends a message to the channel and waits until it is received.
//

STAPI
ULONG
WINAPI
StRendezvousChannel_SendAndWaitReplyEx (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __in PVOID Message,
    __out_opt PVOID *Response,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Sends a message to the channel and waits unconditionally until
// it is replied.
//

STAPI
BOOL
WINAPI
StRendezvousChannel_SendAndWaitReply (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __in PVOID Message,
    __out_opt PVOID *Response
    );

//
// Sends a message to the channel and waits until it is received,
// activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StRendezvousChannel_SendEx (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __in PVOID Message,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Sends a message to the channel and waits until unconditionally
// until it is received.
//

STAPI
BOOL
WINAPI
StRendezvousChannel_Send (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __in PVOID Message
    );

//
// Receive a message from the channel, activating the
// specified cancellers.
//

STAPI
ULONG
WINAPI
StRendezvousChannel_ReceiveEx (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __out PVOID *RvToken,
    __out PVOID *ReceivedMessage,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Receives unconditionally a message from the channel.
//

STAPI
BOOL
WINAPI
StRendezvousChannel_Receive (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __out PVOID *RvToken,
    __out PVOID *ReceivedMessage
    );

//
// Replies to message receive through a rendezvous channel.
//

STAPI
VOID
WINAPI
StRendezvousChannel_Reply (
    __in PVOID RvToken,
    __in PVOID ResponseMessage
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StRendezvousChannel {
    ST_RENDEZVOUS_CHANNEL _channel;
public:

    StRendezvousChannel(__in bool LifoReceive = false) {
        StRendezvousChannel_Init(&_channel, LifoReceive);
    }

    void Init(__in bool LifoReceive = false) {
        StRendezvousChannel_Init(&_channel, LifoReceive);
    }

    ULONG SendAndWaitReply(__in PVOID Message, __out_opt PVOID *Response, __in ULONG Timeout,
                           __inout_opt StAlerter * Alerter = 0) {
        return StRendezvousChannel_SendAndWaitReplyEx(&_channel, Message, Response, Timeout,
                                                      (PST_ALERTER)Alerter);
    }

    BOOL SendAndWaitReply(__in PVOID Message, __out_opt PVOID *Response) {
        return StRendezvousChannel_SendAndWaitReply(&_channel, Message, Response) != FALSE;
    }

    ULONG Send(__in PVOID Message, __in ULONG Timeout, __inout_opt StAlerter * Alerter = 0) {
        return StRendezvousChannel_SendEx(&_channel, Message, Timeout, (PST_ALERTER)Alerter);
    }

    bool Send(__in PVOID Message, __out_opt PVOID *Response) {
        return StRendezvousChannel_Send(&_channel, Message) != FALSE;
    }

    ULONG Receive(__out PVOID *RvToken, __out PVOID *ReceivedMessage, __in ULONG Timeout,
                  __inout_opt StAlerter *Alerter = 0) {
        return StRendezvousChannel_ReceiveEx(&_channel, RvToken, ReceivedMessage, Timeout,
                                             (PST_ALERTER)Alerter);
    }

    bool Receive(__out PVOID *RvToken, __out PVOID *ReceivedMessage) {
        return StRendezvousChannel_Receive(&_channel, RvToken, ReceivedMessage) != FALSE;
    }

    static void Reply(__in PVOID RvToken, __in PVOID ResponseMessage) {
        StRendezvousChannel_Reply(RvToken, ResponseMessage);
    }
};

#endif

/*++
 *
 * Registered take object (used with BlockingQueue).
 *
 --*/

typedef VOID CALLBACK ST_TAKE_CALLBACK (__in_opt PVOID Context, PVOID DataItem, __in ULONG WaitStatus);

//
// The registered take structure.
//

typedef struct _ST_REGISTERED_TAKE {
    volatile PST_PARKER State;
    struct _ST_BLOCKING_QUEUE *Queue;
    RAW_TIMER RawTimer;
    CB_PARKER CbParker;
    struct _WAIT_BLOCK WaitBlock;
    ST_TAKE_CALLBACK *UserCallback;
    PVOID UserCallbackContext;
    ULONG Timeout;
    ULONG ThreadId;
    volatile BOOLEAN ExecuteOnlyOnce;
} ST_REGISTERED_TAKE, *PST_REGISTERED_TAKE;


#ifdef __cplusplus
extern "C" {
#endif

//
// Unregisters the register take from the associated queue.
//

STAPI
BOOL
WINAPI
StRegisteredTake_Unregister (
    __inout PST_REGISTERED_TAKE RegisteredTake
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StRegisteredTake {
    ST_REGISTERED_TAKE _reg_take;
public:
    StRegisteredTake() {}

    BOOL Unregister() {
        return StRegisteredTake_Unregister(&_reg_take);
    }
};

#endif

/*++
 *
 * Blocking Queue interface.
 *
 --*/

typedef struct _ST_BLOCKING_QUEUE {
    struct _BLOCKING_QUEUE_VTBL *Vtbl;
} ST_BLOCKING_QUEUE, *PST_BLOCKING_QUEUE;

//
// Blocking Queue API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Tries to take a data item from a blocking queue.
//

STAPI
ULONG
WINAPI
StBlockingQueue_TakeEx (
    __in PVOID Queue,
    __out PVOID *Result,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Takes unconditionally a data item from a blocking queue.
//

STAPI
BOOL
WINAPI
StBlockingQueue_Take (
    __in PVOID Queue,
    __out PVOID *Result
    );

//
// Tries to take a data item from a set of blocking queues.
//

STAPI
ULONG
WINAPI
StBlockingQueue_TakeAnyEx (
    __in ULONG Offset,
    __in ULONG Count,
    __in ULONG Length,
    __in PVOID Queues[],
    __out PVOID *Result,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Takes unconditionally a data item from a set of blocking queues.
//

STAPI
ULONG
WINAPI
StBlockingQueue_TakeAny (
    __in ULONG Offset,
    __in ULONG Count,
    __in ULONG Length,
    __in PVOID Queues[],
    __out PVOID *Result
    );

//
// Registers the a wait with the Waitable.
//

STAPI
BOOL
WINAPI
StBlockingQueue_RegisterTake (
    __inout PVOID Queue,
    __out PST_REGISTERED_TAKE RegisteredTake,
    __in ST_TAKE_CALLBACK *UserCallback,
    __in_opt PVOID UserCallbackContext,
    __in ULONG Timeout,
    __in BOOL ExecuteOnlyOnce
    );


#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StBlockingQueue {
protected:
    StBlockingQueue() {}
public:

    ULONG Take(__out PVOID *Result, __in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StBlockingQueue_TakeEx(this, Result, Timeout, (PST_ALERTER)Alerter);
    }

    bool Take(__out PVOID *Result) {
        return StBlockingQueue_Take(this, Result) != FALSE;
    }

    static ULONG TakeAny(__in ULONG Offset, __in ULONG Count, __in ULONG Length,
                         __inout StBlockingQueue* Queues[], __out PVOID *Result,
                         __in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StBlockingQueue_TakeAnyEx(Offset, Count, Length, (PVOID *)Queues,
                                         Result, Timeout, (PST_ALERTER)Alerter);
    }

    static ULONG TakeAny(__in ULONG Offset, __in ULONG Count, __in ULONG Length,
                         __inout StBlockingQueue* Queues[], __out PVOID *Result) {
        return StBlockingQueue_TakeAny(Offset, Count, Length, (PVOID *)Queues, Result);
    }

    static bool RegisterTake(__inout StBlockingQueue *Queue, __out StRegisteredTake *RegisteredTake,
                             __in ST_TAKE_CALLBACK *UserCallback = 0, __in PVOID UserCallbackContext = 0,
                             __in ULONG Timeout = INFINITE, __in bool ExecuteOnlyOnce = false) {
        return StBlockingQueue_RegisterTake(Queue, (PST_REGISTERED_TAKE)RegisteredTake,
                                            UserCallback, UserCallbackContext,
                                            Timeout, ExecuteOnlyOnce) != FALSE;
    }
};

#endif

/*++
 *
 * Bounded Blocking Queue (array-based)
 *
 * NOTE: Implements the Blocking Queue interface.
 *
 --*/

typedef struct _ST_BOUNDED_BLOCKING_QUEUE {
    ST_BLOCKING_QUEUE BlockingQueue;
    SPINLOCK Lock;
    LIST_ENTRY_ WaitList;
    volatile ULONG Count;
    ULONG Head;
    ULONG Tail;
    PVOID *Items;
    ULONG Capacity;
    ULONG Flags;
} ST_BOUNDED_BLOCKING_QUEUE, *PST_BOUNDED_BLOCKING_QUEUE;

//
// Callback function called once or twice with an array of data items that
// are stored on the queue, when it is disposed.
//

typedef VOID CALLBACK ST_BOUNDED_BLOCKING_QUEUE_DTOR(PVOID *ItemArray, ULONG Count,
                                                     PVOID Context);

//
// Bounded Blocking Queue API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the bounded blocking queue.
//

STAPI
BOOL
WINAPI
StBoundedBlockingQueue_Init (
    __out PST_BOUNDED_BLOCKING_QUEUE Queue,
    __in ULONG Capacity,
    __in BOOL LifoTake
    );

//
// Adds a data item to the bounded blocking queue, activating the
// specified cancellers.
//

STAPI
ULONG
WINAPI
StBoundedBlockingQueue_AddEx (
    __inout PST_BOUNDED_BLOCKING_QUEUE Queue,
    __in PVOID Item,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Adds unconditionally a data item to the bounded blocking queue.
//


STAPI
BOOL
WINAPI
StBoundedBlockingQueue_Add (
    __inout PST_BOUNDED_BLOCKING_QUEUE Queue,
    __in PVOID Item
    );

//
// Tries to take a data item from the bounded blocking queue,
// activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StBoundedBlockingQueue_TakeEx (
    __in PST_BOUNDED_BLOCKING_QUEUE Queue,
    __out PVOID *Result,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Takes unconditionally a data item from the bounded blocking queue.
//

STAPI
ULONG
WINAPI
StBoundedBlockingQueue_Take (
    __in PST_BOUNDED_BLOCKING_QUEUE Queue,
    __out PVOID *Result
    );

//
// Disposes the bounded blocking queue, releasing the allocated resources.
//

STAPI
BOOL
WINAPI
StBoundedBlockingQueue_Dispose (
    __inout PST_BOUNDED_BLOCKING_QUEUE Queue,
    __in_opt ST_BOUNDED_BLOCKING_QUEUE_DTOR *Dtor,
    __in_opt PVOID Context
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StBoundedBlockingQueue : public StBlockingQueue {
    ST_BOUNDED_BLOCKING_QUEUE _queue;
public:

#define DEFAULT_CAPACITY	1024

    StBoundedBlockingQueue(__in ULONG Capacity = DEFAULT_CAPACITY, __in BOOL LifoTake = false) {
        StBoundedBlockingQueue_Init(&_queue, Capacity, LifoTake);
    }

    void Init(__in ULONG Capacity = DEFAULT_CAPACITY, __in BOOL LifoTake = false) {
        Dispose();
        StBoundedBlockingQueue_Init(&_queue, Capacity, LifoTake);
    }

    ULONG Add (__in PVOID Item, __in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StBoundedBlockingQueue_AddEx(&_queue, Item, Timeout, (PST_ALERTER)Alerter);
    }

    bool Add (__in PVOID Item) {
        return StBoundedBlockingQueue_Add(&_queue, Item) != FALSE;
    }

    ULONG Take(__out PVOID *Result, __in ULONG Timeout, __inout_opt StAlerter* Alerter = 0) {
        return StBoundedBlockingQueue_TakeEx(&_queue, Result, Timeout, (PST_ALERTER)Alerter);
    }

    ULONG Take(__out PVOID *Result) {
        return StBoundedBlockingQueue_Take(&_queue, Result);
    }

    bool Dispose(__in_opt ST_BOUNDED_BLOCKING_QUEUE_DTOR *Dtor = 0, __in_opt PVOID Context = 0) {
        return StBoundedBlockingQueue_Dispose(&_queue, Dtor, Context) != FALSE;
    }

    ~StBoundedBlockingQueue() {
        Dispose();
    }
};
#endif

/*++
 *
 * Linked Blocking Queue.
 *
 * NOTE: Implements the Blocking Queue interface.
 *
 --*/

typedef struct _ST_LINKED_BLOCKING_QUEUE {
    ST_BLOCKING_QUEUE BlockingQueue;
    SPINLOCK Lock;
    LIST_ENTRY_ WaitList;
    volatile PSLIST_ENTRY First;
    PSLIST_ENTRY Last;
    ULONG Flags;
} ST_LINKED_BLOCKING_QUEUE, *PST_LINKED_BLOCKING_QUEUE;

//
// The callback function called that is called with the list of data items
// that are stored in the queue when it is disposed.
//

typedef VOID CALLBACK ST_LINKED_BLOCKING_QUEUE_DTOR(PSLIST_ENTRY List, PVOID Context);

//
// Linked Blocking Queue API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
//	Initializes the queue.
//

STAPI
VOID
WINAPI
StLinkedBlockingQueue_Init (
    __out PST_LINKED_BLOCKING_QUEUE Queue,
    __in BOOL LifoTake
    );

//
// Adds a data item to the queue.
//

STAPI
BOOL
WINAPI
StLinkedBlockingQueue_Add (
    __inout PST_LINKED_BLOCKING_QUEUE Queue,
    __in PSLIST_ENTRY Item
    );

//
// Adds a list of data items to the queue.
//

STAPI
BOOL
WINAPI
StLinkedBlockingQueue_AddList (
    __inout PST_LINKED_BLOCKING_QUEUE Queue,
    __in PSLIST_ENTRY List
    );

//
// Tries to take a data item from the queue.
//

STAPI
ULONG
WINAPI
StLinkedBlockingQueue_TakeEx (
    __inout PST_LINKED_BLOCKING_QUEUE Queue,
    __out PSLIST_ENTRY *Result,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Takes unconditionally a data item from the queue.
//

STAPI
ULONG
WINAPI
StLinkedBlockingQueue_Take (
    __inout PST_LINKED_BLOCKING_QUEUE Queue,
    __out PSLIST_ENTRY *Result
    );

//
// Tries to take all data items present in the queue.
//

STAPI
ULONG
WINAPI
StLinkedBlockingQueue_TakeAllEx (
    __inout PST_LINKED_BLOCKING_QUEUE Queue,
    __out PSLIST_ENTRY *ListResult,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Takes all data items present in the queue.
//

STAPI
BOOL
WINAPI
StLinkedBlockingQueue_TakeAll (
    __in PST_LINKED_BLOCKING_QUEUE Queue,
    __out PSLIST_ENTRY *ListResult
    );

//
// Disposes the queue.
//

STAPI
BOOL
WINAPI
StLinkedBlockingQueue_Dispose (
    __inout PST_LINKED_BLOCKING_QUEUE Queue,
    __in_opt ST_LINKED_BLOCKING_QUEUE_DTOR *Dtor,
    __in_opt PVOID Context
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StLinkedBlockingQueue : public StBlockingQueue {
    ST_LINKED_BLOCKING_QUEUE _queue;
public:

    StLinkedBlockingQueue(__in bool LifoTake = false) {
        StLinkedBlockingQueue_Init(&_queue, LifoTake);
    }

    void Init(__in bool LifoTake = false) {
        StLinkedBlockingQueue_Init(&_queue, LifoTake);
    }

    bool Add(__in PSLIST_ENTRY Item) {
        return StLinkedBlockingQueue_Add(&_queue, Item) != FALSE;
    }

    bool AddList(__in PSLIST_ENTRY ItemList) {
        return StLinkedBlockingQueue_AddList(&_queue, ItemList) != FALSE;
    }

    ULONG Take(__out PSLIST_ENTRY *Result, __in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StLinkedBlockingQueue_TakeEx(&_queue, Result, Timeout, (PST_ALERTER)Alerter);
    }

    ULONG Take(__out PSLIST_ENTRY *Result) {
        return StLinkedBlockingQueue_Take(&_queue, Result);
    }

    ULONG TakeAll(__out PSLIST_ENTRY *ResultList, __in ULONG Timeout,
                  __inout_opt StAlerter *Alerter = 0) {
        return StLinkedBlockingQueue_TakeAllEx(&_queue, ResultList, Timeout,
                                              (PST_ALERTER)Alerter);
    }

    bool TakeAll(__out PSLIST_ENTRY *ResultList) {
        return StLinkedBlockingQueue_TakeAll(&_queue, ResultList) != FALSE;
    }

    bool Dispose(__in_opt ST_LINKED_BLOCKING_QUEUE_DTOR *Dtor = 0, __in_opt PVOID Context = 0) {
        return StLinkedBlockingQueue_Dispose(&_queue, Dtor, Context) != FALSE;
    }
};

#endif

/*++
 *
 * Stream Blocking Queue.
 *
 --*/

typedef struct _ST_STREAM_BLOCKING_QUEUE {
    SPINLOCK Lock;
    LIST_ENTRY_ WaitList;
    ULONG Count;
    PUCHAR Items;
    PUCHAR Limit;
    PUCHAR Tail;
    PUCHAR Head;
    ULONG Capacity;
    LONG SpinCycles;
} ST_STREAM_BLOCKING_QUEUE, *PST_STREAM_BLOCKING_QUEUE;

//
// The callback function that is called to process the data that is
// stored on the queue when it is disposed.
//

typedef VOID CALLBACK ST_STREAM_BLOCKING_QUEUE_DTOR(PUCHAR Buffer, ULONG Count,
                                                    PVOID Context);

//
// Stream Blocking Queue API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the queue.
//

STAPI
BOOL
WINAPI
StStreamBlockingQueue_Init (
    __out PST_STREAM_BLOCKING_QUEUE Queue,
    __in ULONG Capacity
    );

//
// Tries to writes the specified number of bytes to the queue,
// activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StStreamBlockingQueue_WriteEx (
    __inout PST_STREAM_BLOCKING_QUEUE Queue,
    __in PVOID Buffer,
    __in ULONG Count,
    __out_opt PULONG Transferred,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Writes unconditionally the specified number of bytes to the queue.
//

STAPI
BOOL
WINAPI
StStreamBlockingQueue_Write (
    __inout PST_STREAM_BLOCKING_QUEUE Queue,
    __in PVOID Buffer,
    __in ULONG Count,
    __out_opt PULONG Transferred
    );

//
// Tries to reads the specified number of bytes from the queue,
// activating the specified cancellers
//

STAPI
ULONG
WINAPI
StStreamBlockingQueue_ReadEx (
    __inout PST_STREAM_BLOCKING_QUEUE Queue,
    __in PVOID Buffer,
    __in ULONG Count,
    __out_opt PULONG Transferred,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Reads unconditionally the specified number of bytes from the queue.
//

STAPI
BOOL
WINAPI
StStreamBlockingQueue_Read (
    __inout PST_STREAM_BLOCKING_QUEUE Queue,
    __in PVOID Buffer,
    __in ULONG Count,
    __out_opt PULONG Transferred
    );

//
// Dispose the queue.
//

STAPI
BOOL
WINAPI
StStreamBlockingQueue_Dispose (
    __inout PST_STREAM_BLOCKING_QUEUE Queue,
    __in ST_STREAM_BLOCKING_QUEUE_DTOR *Dtor,
    __in PVOID Context
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StStreamBlockingQueue {
    ST_STREAM_BLOCKING_QUEUE _queue;
public:

#define DEFAULT_CAPACITY	1024

    StStreamBlockingQueue(__in ULONG Capacity = DEFAULT_CAPACITY) {
        StStreamBlockingQueue_Init(&_queue, Capacity);
    }

    void Init(__in ULONG Capacity = DEFAULT_CAPACITY) {
        Dispose();
        StStreamBlockingQueue_Init(&_queue, Capacity);
    }


    ULONG Write(__in PVOID Buffer, __in ULONG Count, __out_opt PULONG Transferred,
                __in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StStreamBlockingQueue_WriteEx(&_queue, Buffer, Count, Transferred,
                                             Timeout, (PST_ALERTER)Alerter);
    }

    bool Write(__in PVOID Buffer, __in ULONG Count, __out_opt PULONG Transferred) {
        return StStreamBlockingQueue_Write(&_queue, Buffer, Count, Transferred) != FALSE;
    }

    ULONG Read(__in PVOID Buffer, __in ULONG Count, __out_opt PULONG Transferred,
               __in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StStreamBlockingQueue_ReadEx(&_queue, Buffer, Count, Transferred,
                                            Timeout, (PST_ALERTER)Alerter);
    }

    bool Read(__in PVOID Buffer, __in ULONG Count, __out_opt PULONG Transferred) {
        return StStreamBlockingQueue_Read(&_queue, Buffer, Count, Transferred) != FALSE;
    }

    bool Dispose(__in ST_STREAM_BLOCKING_QUEUE_DTOR *Dtor = 0, __in PVOID Context = 0) {
        return StStreamBlockingQueue_Dispose(&_queue, Dtor, Context) != FALSE;
    }

    ~StStreamBlockingQueue() {
        Dispose();
    }
};

#endif

/*++
 *
 * Exchanger.
 *
 --*/

typedef struct _ST_EXCHANGER {
    struct _WAIT_BLOCK * volatile Slot;
    LONG SpinCount;
} ST_EXCHANGER, *PST_EXCHANGER;

//
// Exchanger API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the exchanger.
//

STAPI
VOID
WINAPI
StExchanger_Init (
    __out PST_EXCHANGER Exchanger,
    __in ULONG SpinCount
    );

//
// Tries to exchange a data item, activating the specified cancellers.
//

STAPI
ULONG
WINAPI
StExchanger_ExchangeEx (
    __inout PST_EXCHANGER Exchanger,
    __in PVOID OfferData,
    __out PVOID *RetrievedData,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    );

//
// Exchanges unconditionally a data item.
//

STAPI
BOOL
WINAPI
StExchanger_Exchange (
    __inout PST_EXCHANGER Exchanger,
    __in PVOID OfferData,
    __out PVOID *RetrievedData
    );


#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 +
 --*/

class StExchanger {
    ST_EXCHANGER _exchanger;
public:

    StExchanger(__in ULONG SpinCount = 0) {
        StExchanger_Init(&_exchanger, SpinCount);
    }

    void Init(__in ULONG SpinCount = 0) {
        StExchanger_Init(&_exchanger, SpinCount);
    }

    ULONG Exchange(__in PVOID OfferData, __out PVOID *RetrievedData,
                   __in ULONG Timeout, __inout_opt StAlerter *Alerter = 0) {
        return StExchanger_ExchangeEx(&_exchanger, OfferData, RetrievedData, Timeout,
                                      (PST_ALERTER)Alerter);
    }

    bool Exchange(__in PVOID OfferData, __out PVOID *RetrievedData) {
        return StExchanger_Exchange(&_exchanger, OfferData, RetrievedData) != FALSE;
    }
};

#endif

/*++
 *
 * Init Once Lock.
 *
 --*/

typedef struct _ST_INIT_ONCE_LOCK {
    volatile PST_PARKER State;
} ST_INIT_ONCE_LOCK, *PST_INIT_ONCE_LOCK;

//
// Init Once Lock API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the init once lock.
//

STAPI
VOID
WINAPI
StInitOnceLock_Init (
    __out PST_INIT_ONCE_LOCK Lock
    );

//
// Tries to init the associated target.
//

STAPI
BOOL
WINAPI
StInitOnceLock_TryInitTarget (
    __inout PST_INIT_ONCE_LOCK Lock,
    __in ULONG SpinCount
    );

//
// Signals that the target was initialized.
//

STAPI
VOID
WINAPI
StInitOnceLock_InitTargetCompleted (
    __inout PST_INIT_ONCE_LOCK Lock
    );

//
// Signals that the current initialization failed.
//

STAPI
VOID
WINAPI
StInitOnceLock_InitTargetFailed (
    __inout PST_INIT_ONCE_LOCK Lock
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StInitOnceLock {
    ST_INIT_ONCE_LOCK _lock;
public:

    StInitOnceLock() {
        StInitOnceLock_Init(&_lock);
    }

    void Init() {
        StInitOnceLock_Init(&_lock);
    }

    bool TryInitTarget(__in ULONG SpinCount = 0) {
        return StInitOnceLock_TryInitTarget(&_lock, SpinCount) != FALSE;
    }

    void InitTargetCompleted() {
        StInitOnceLock_InitTargetCompleted(&_lock);
    }

    void InitTargetFailed() {
        StInitOnceLock_InitTargetFailed(&_lock);
    }
};

#endif

/*++
 *
 * Init Once.
 *
 * NOTE: Implements the semantics of the Windows Init Once mechanism.
 *
 --*/

#ifndef INIT_ONCE_CHECK_ONLY

#define INIT_ONCE_CHECK_ONLY	(1UL << 0)
#define INIT_ONCE_ASYNC			(1UL << 1)
#define INIT_ONCE_INIT_FAILED	(1UL << 2)

#endif

typedef struct _ST_INIT_ONCE {
    ST_INIT_ONCE_LOCK Lock;
    volatile PVOID Target;
} ST_INIT_ONCE, *PST_INIT_ONCE;


//
// The target constructor function.
//

typedef BOOL (CALLBACK *PST_INIT_ONCE_CTOR)(PST_INIT_ONCE InitOnce, PVOID Parameter, PVOID* Context);

//
// Init Once API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the init once.
//

STAPI
VOID
WINAPI
StInitOnce_Init (
    __out PST_INIT_ONCE InitOnce
    );

//
// Inits once synchronouly.
//

STAPI
BOOL
WINAPI
StInitOnce_ExecuteOnce (
    __inout PST_INIT_ONCE InitOnce,
    __in PST_INIT_ONCE_CTOR Ctor,
    __inout_opt PVOID CtorParameter,
    __out PVOID *Context
    );

//
// Begins one-time initialization.
//

STAPI
BOOL
WINAPI
StInitOnce_BeginInit (
    __inout PST_INIT_ONCE InitOnce,
    __in ULONG Flags,
    __out BOOL *Pending,
    __out PVOID *Context
    );

//
// Completes the one-time initialization started with the 
// StInitOnce_BeginInit function.
//

STAPI
BOOL
WINAPI
StInitOnce_Complete (
    __inout PST_INIT_ONCE InitOnce,
    __in ULONG Flags,
    __in PVOID Context
    );

#ifdef __cplusplus
}

/*
 *
 * C++ wrapper.
 *
 --*/

class StInitOnce {
    ST_INIT_ONCE _initOnce;
public:

    StInitOnce() {
        StInitOnce_Init(&_initOnce);
    }

    void Init() {
        StInitOnce_Init(&_initOnce);
    }

    bool ExecuteOnce(__in PST_INIT_ONCE_CTOR Ctor, __inout_opt PVOID CtorParameter,
                     __out PVOID *Context) {
        return StInitOnce_ExecuteOnce(&_initOnce, Ctor, CtorParameter, Context) != FALSE;
    }
    
    bool BeginInit(__in ULONG Flags, __out BOOL *Pending, __out PVOID *Context) {
        return StInitOnce_BeginInit(&_initOnce, Flags, Pending, Context) != FALSE;
    }

    bool Complete(__in ULONG Flags, __in PVOID Context) {
        return StInitOnce_Complete(&_initOnce, Flags, Context) != FALSE;
    }

    PVOID GetTarget() {
        return _initOnce.Target;
    }
};

#endif

/*++
 *
 * Ensure Initialized API.
 *
 --*/

//
// The target constructor and destructor callback functions.
//

typedef PVOID (CALLBACK *PST_ENSURE_INITIALIZED_CTOR)(PVOID Context);
typedef VOID (CALLBACK *PST_ENSURE_INITIALIZED_DTOR)(PVOID Target, PVOID Context);

//
// Ensure Initialized API.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Ensures initialized asynchronous.
//

STAPI
PVOID
WINAPI
StEnsureInitialized_Asynchronous (
    __inout PVOID *Target,
    __in PST_ENSURE_INITIALIZED_CTOR Ctor,
    __in PST_ENSURE_INITIALIZED_DTOR Dtor,
    __in PVOID CtorDtorContext
    );

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
    );

#ifdef __cplusplus
}

/*++
 *
 * C++ wrapper.
 *
 --*/

class StEnsureInitialized {
public:

    static PVOID InitAsynchronous (__inout PVOID *Target, __in PST_ENSURE_INITIALIZED_CTOR Ctor,
                                   __in PST_ENSURE_INITIALIZED_DTOR Dtor,
                                   __in PVOID CtorDtorContext = 0) {
        return StEnsureInitialized_Asynchronous(Target, Ctor, Dtor, CtorDtorContext);
    }


    static PVOID InitSynchronous(__inout PVOID *Target, StInitOnceLock *Lock,
                                 __in PST_ENSURE_INITIALIZED_CTOR Ctor, __in PVOID CtorContext = 0,
                                 __in ULONG SpinCount = 0) {
        return StEnsureInitialized_Synchronous(Target, (PST_INIT_ONCE_LOCK)Lock, Ctor,
                                               CtorContext, SpinCount);
    }
};

#endif

/*++
 *
 * Timer
 *
 --*/

//
// The timer structure.
//

typedef struct _ST_TIMER {
    union {
        ST_NOTIFICATION_EVENT NotEvent;
        ST_SYNCHRONIZATION_EVENT SyncEvent;
    };
    volatile PST_PARKER State;
    RAW_TIMER RawTimer;
    CB_PARKER CbParker;
    ST_WAIT_OR_TIMER_CALLBACK *UserCallback;
    PVOID UserCallbackContext;
    ULONG ThreadId; 
    ULONG DueTime;
    ULONG Period;
    BOOLEAN SetWithDueTime;
} ST_TIMER, *PST_TIMER;


#ifdef __cplusplus
extern "C" {
#endif

//
// Initializes the timer.
//

STAPI
VOID
WINAPI
StTimer_Init (
    __out PST_TIMER Timer,
    __in BOOL NotificationTimer
    );

//
// Sets the timer.
//

STAPI
BOOL
WINAPI
StTimer_Set (
    __inout PST_TIMER Timer,
    __in ULONG DueTime,
    __in LONG Period,
    __in_opt ST_WAIT_OR_TIMER_CALLBACK *Callback,
    __in_opt PVOID Context
    );

//
// Cancels the timer.
//

STAPI
BOOL
WINAPI
StTimer_Cancel (
    __inout PST_TIMER Timer
    );

#ifdef __cplusplus
}

class StTimer : public StWaitable {
    ST_TIMER _timer;
public:

    StTimer(__in bool NotificationTimer = true) {
        StTimer_Init(&_timer, NotificationTimer);
    }

    bool Set(__in ULONG DueTime, __in LONG Period, __in_opt ST_WAIT_OR_TIMER_CALLBACK *UserCallback,
             __in_opt PVOID UserCallbackContext = 0) {
        return StTimer_Set(&_timer, DueTime, Period, UserCallback, UserCallbackContext) != FALSE;
    }

    bool Cancel() {
        return StTimer_Cancel(&_timer) != FALSE;
    }
};

#endif

/*++
 *
 * UMS scheduler API.
 *
 --*/

#ifdef __cplusplus
extern "C" {
#endif

//
// Returns true if the UMS scheduler is available.
//

STAPI
BOOL
WINAPI
StUmsScheduler_IsAvailable (
    );

//
// Returns the percentage of idle time das UMS scheduler threads.
//

STAPI
LONG
WINAPI
StUmsScheduler_GetIdlePercentage (
    );

#ifdef __cplusplus
}
#endif

#if (_WIN32_WINNT >= 0x0601 && defined(_WIN64))

/*++
 *
 * UMS worker threads API.
 *
 --*/

#ifdef __cplusplus
extern "C" {
#endif

//
// Creates a UMS worker thread.
//

STAPI
HANDLE
WINAPI
StUmsThread_Create (
    __in LPSECURITY_ATTRIBUTES ThreadAttributes,
    __in SIZE_T StackSize,
    __in LPTHREAD_START_ROUTINE EntryPoint,
    __in_opt LPVOID Parameter,
    __in ULONG CreationFlags,
    __out_opt PULONG ThreadId
    );

//
// Yields execution of the current thread.
//

STAPI
VOID
WINAPI
StThread_Yield (
    );

//
// Sets the priority of the current UMS thead.
//

STAPI
BOOL
WINAPI
StUmsThread_SetCurrentThreadPriority (
    __in LONG NewPriority
    );

//
// Returns the priority of the current UMS thead.
//

STAPI
LONG
WINAPI
StUmsThread_GetCurrentThreadPriority (
    );

//
// Allocates a park spot to block the current UMS worker thread.
//

STAPI
PVOID
WINAPI
StUmsThread_AllocParkSpot (
    );

//
// Frees a park spot previously allocated by the current UMS worker thread.
//

STAPI
VOID
WINAPI
StUmsThread_FreeParkSpot (
    __inout PVOID ParkSpot
    );

//
// Wait on the specified UMS worker thread´s park spot.
//

STAPI
BOOL
WINAPI
StUmsThread_WaitForParkSpot (
    __inout PVOID ParkSpot
    );

//
// Sets the specified UMS worker thread´s park spot.
//

STAPI
BOOL
WINAPI
StUmsThread_SetParkSpot (
    __inout PVOID ParkSpot
    );

#ifdef __cplusplus
}
#endif

#else

FORCEINLINE
HANDLE
StUmsThread_Create (
    __in LPSECURITY_ATTRIBUTES ThreadAttributes,
    __in SIZE_T StackSize,
    __in LPTHREAD_START_ROUTINE EntryPoint,
    __in_opt LPVOID Parameter,
    __in ULONG CreationFlags,
    __out_opt PULONG ThreadId
    )
{

    return CreateThread(ThreadAttributes, StackSize, EntryPoint, Parameter, CreationFlags, ThreadId);
}

//
// Yields execution of the current thread.
//

FORCEINLINE
VOID
StThread_Yield (
    )
{
    SwitchToThread();
}

//
// Sets the priority of the current UMS thead.
//

FORCEINLINE
BOOL
StUmsThread_SetCurrentThreadPriority (
    __in LONG NewPriority
    )
{
    return SetThreadPriority(GetCurrentThread(), NewPriority);
}

//
// Returns the priority of the current UMS thead.
//

FORCEINLINE
LONG
StUmsThread_GetCurrentThreadPriority (
    )
{
    return GetThreadPriority(GetCurrentThread());
}

#endif
