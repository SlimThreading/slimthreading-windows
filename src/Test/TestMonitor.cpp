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

#include "stdafx.h"

//
// The lock interface
//

struct ILock {
    virtual void Lock() = 0;
    virtual void Unlock() = 0;
    virtual PVOID GetLock() = 0;
    virtual void InitializeConditionVariable(StConditionVariable *Condition) = 0;
};


//
// ILock implementation using a lock.
//

struct LOCK_LOCK : public ILock {

    StLock _lock;

    LOCK_LOCK() : _lock(200) {}

    void Lock() { _lock.Enter(); }

    void Unlock() { _lock.Exit(); }

    PVOID GetLock() { return &_lock; }

    void InitializeConditionVariable(StConditionVariable *Condition) {
        Condition->Init((StLock *)0);
    }
};

//
// ILock implementation using a reentrant lock.
//

struct CS_LOCK : public ILock {
    StReentrantLock _rlock;

    CS_LOCK() : _rlock(200) {}

    void Lock() { _rlock.Enter(); }

    void Unlock() { _rlock.Exit(); }

    PVOID GetLock() { return &_rlock; }

    void InitializeConditionVariable(StConditionVariable *Condition) {
        Condition->Init((StReentrantLock *)0);
    }
};

//
// ILock implementation using a fair lock.
//

struct FL_LOCK : public ILock {

    StFairLock _flock;

    FL_LOCK() : _flock(500) {}

    void Lock() { _flock.Enter(); }

    void Unlock() { _flock.Exit(); }

    PVOID GetLock() { return &_flock; }

    void InitializeConditionVariable(StConditionVariable *Condition) {
        Condition->Init((StFairLock *)0);
    }
};

//
// ILock implementation using a read write lock.
//

struct RWL_LOCK : public ILock {

    StReadWriteLock _rwlock;

    RWL_LOCK() : _rwlock(500) {}

    void Lock() { _rwlock.EnterWrite(); }

    void Unlock() { _rwlock.ExitWrite(); }

    PVOID GetLock() { return &_rwlock; }

    void InitializeConditionVariable(StConditionVariable *Condition) {
        Condition->Init((StReadWriteLock *)0);
    }
};

//
// Monitor-based blocking queue.
//

struct BlockingQueue {
    int capacity;
    int *buffer;
    int count;
    int iput;
    int itake;
    int state;
    ILock *ilock;
    StConditionVariable nonFull;
    StConditionVariable nonEmpty;
    
    BlockingQueue(ILock *ilock, int capacity) {
        this->ilock = ilock;
        this->capacity = capacity;
        buffer = new int[capacity];
        count = iput = itake = 0;
        ilock->InitializeConditionVariable(&nonFull);
        ilock->InitializeConditionVariable(&nonEmpty);
    }
                
    bool Offer(int data, int timeout) {
        ilock->Lock();
        if (count < capacity) {
            buffer[iput] = data;
            iput = ++iput % capacity;
            count++;
            nonEmpty.Notify();
            ilock->Unlock();
            return true;
        }

        ULONG lastTime = (timeout == INFINITE) ? GetTickCount() : 0;
        do {
            nonFull.Wait(ilock->GetLock(), timeout);

            if (count < capacity) {
                buffer[iput] = data;
                iput = ++iput % capacity;
                count++;
                nonEmpty.Notify();
                ilock->Unlock();
                return true;
            }			
            if (timeout != INFINITE) {
                ULONG now = GetTickCount();
                int elapsed = now - lastTime;
                if (timeout <= elapsed) {
                    break;
                }
                timeout -= elapsed;
                lastTime = now;
            }
        } while (true);
        ilock->Unlock();
        return false;
    }

    void Put(int data) {
        Offer(data, INFINITE);
    }

    int Poll(int timeout) {
        ilock->Lock();
        if (count > 0) {
            int data = buffer[itake];
            itake = ++itake % capacity;
            count--;
            nonFull.Notify();
            ilock->Unlock();
            return data;
        }		
        ULONG lastTime = (timeout == INFINITE) ? GetTickCount() : 0;
        do {
            nonEmpty.Wait(ilock->GetLock(), timeout);
            if (count > 0) {
                int data = buffer[itake];
                itake = ++itake % capacity;
                count--;
                nonFull.Notify();
                ilock->Unlock();
                return data;
            }			
            if (timeout != INFINITE) {
                ULONG now = GetTickCount();
                int elapsed = now - lastTime;
                if (timeout <= elapsed) {
                    break;
                }
                timeout -= elapsed;
                lastTime = now;
            }
        } while (true);
        ilock->Unlock();
        return -1;
    }

    int Take() {
        return Poll(INFINITE);
    }
};

//
// The several flavours of lock
//

CS_LOCK CsLock;
LOCK_LOCK LockLock;
FL_LOCK FLLock;
RWL_LOCK RwLock;

//
// The monitor based blocking queue.
//

static BlockingQueue Queue(&CsLock, 1);
//static BlockingQueue Queue(&LockLock, 1);
//static BlockingQueue Queue(&FLLock, 1);
//static BlockingQueue Queue(&RwLock, 1);
    
//
// The number of producer/consumer threads.
//

#define THREADS 	10

//
// The alerter and the count down event used to synchronize the shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(THREADS);

//
// The counters.
//

int Counts[THREADS];

//
// The consumer/producer thread.
//

ULONG
WINAPI
ConsumerProducer (
    __in PVOID arg
    )
{

#define S	50

    LONG Id = (int)arg;
    printf("+++c/p #%d started...\n", Id);
    ULONG Count = 0;
    ULONG Timeouts = 0;
    srand(GetCurrentThreadId());
    do {

        if ((rand() % 100) < S) {
            LONG Data;
            while ((Data = Queue.Poll(1)) == -1) {
                Timeouts++;
            }
            Queue.Put(Data);
            Count++;
        } else {
            SwitchToThread();
        }
        Counts[Id]++;
    } while (!Shutdown.IsSet());
    printf("+++c/p #%d exiting:  after [%d(%d%%)/%d]\n", Id, Counts[Id],
            (Count * 100)/Counts[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The test function.
//

VOID
RunMonitorTest(
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < THREADS; i++) {
        HANDLE ThreadHandle = CreateThread(NULL, 0, ConsumerProducer, (PVOID)(i), 0, NULL);
        CloseHandle(ThreadHandle);
    }

    Queue.Put(42);
    getchar();
    Shutdown.Set();
    Done.Wait();

    ULONGLONG Total = 0;
    for (int i = 0; i < THREADS; i++) {
        Total += Counts[i];
    }
    printf("+++Total c/p: %I64d\n", Total);
}
