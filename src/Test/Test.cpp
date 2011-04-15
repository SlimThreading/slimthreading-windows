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
// Test files and functions
//

// TestAlerter.cpp

VOID RunAlerterTest();

// TestApc.cpp

VOID RunApcTest();

// TestAsynchronousIO.cpp

VOID RunAsynchronousIOTest();

// TestBarrier.cpp

VOID RunBarrierTest();

// TestBlockingQueueTakeAny.cpp

VOID RunBlockingQueueTakeAnyTest();

// TestBoundedBlockingQueue.cpp

VOID RunBoundedBlockingQueueTest();

// TestReentrantLock.cpp

VOID RunReentrantLockTest();
VOID RunWindowsCriticalSectionTest();

// TestCountDownEvent.cpp

VOID RunCountDownEventTest();

// TestExchanger.cpp

VOID RunExchangerTest();

// TestFuture.cpp

VOID RunWaitAllFutureTest();
VOID RunWaitAnyFutureTest();

// TestInitOnce.cpp

VOID RunInitOnceSynchronousTest();
VOID RunInitOnceInlineSynchronousTest();
VOID RunInitOnceAsynchronousTest();

// TestEnsureInitialized.cpp

VOID RunEnsureInitializedSynchronousTest();
VOID RunEnsureInitializedAsynchronousTest();

// TestLock.cpp

VOID RunLockTest();

// TestMonitor.cpp

VOID RunMonitorTest();

// TestFairLock.cpp

VOID RunFairLockTest();
VOID RunReentrantFairLockTest();

// TestReadWriteLock.cpp

VOID RunReadWriteLockTest();

// TestReentrantReadWriteLock.cpp

VOID RunReentrantReadWriteLockTest();
VOID RunMultipleReentrantReadWriteLockTest();

// TestRendezvousChannel.cpp

VOID RunRendezvousChannelTest();

// TestSemaphore.cpp

VOID RunSemaphoreTest();

// TestSignalAndWait.cpp

VOID RunSignalAndWaitTest();
VOID RunWinSignalAndWaitTest();

// TestStreamBlockingQueue.cpp

VOID RunStreamBlockingQueueTest();

//TestSynchronizationEvent.cpp

VOID RunStSynchronizationEventTest();

// TestLinkedBlockingQueue.cpp

VOID RunLinkedBlockingQueueTest();

// TestWaitAll.cpp

VOID RunWaitAllForFairLockTest();
VOID RunWaitAllForReadWriteLockTest();
VOID RunWaitAllForSynchronizationEventTest();
VOID RunWaitAllForNotificationEventTest();
VOID RunWaitAllForSemaphoreTest();

// TestWaitForMultlipeNotificationEvents.cpp

VOID RunWaitForMultipleNotificationEventsTest();

// TestWaitForMultlipeSynchronizationEvents.cpp

VOID RunWaitForMultipleSynchronizationEventsTest();

// TestWaitForMultlipeSemaphores.cpp

VOID RunWaitForMultipleSemaphoresTest();

VOID RunNotificationEventTest();

// TestTimer.cpp

VOID RunTimerTest();

//
// TestUmsScheduler.cpp
//

VOID RunUmsSchedulerTest();

//
// Logger.cpp
//

VOID RunLoggerTest();

//
// TestRegisteredWait.cpp
//

VOID RunRegisteredWaitTest();

//
// TestRegisteredTake.cpp
//

VOID RunRegisteredTakeTest();

//
// The main
//


int _tmain(int argc, _TCHAR* argv[]) {

    //RunAlerterTest();
    //RunApcTest();
    //RunAsynchronousIOTest();
    RunBarrierTest();
    //RunBlockingQueueTakeAnyTest();
    //RunBoundedBlockingQueueTest();
    //RunReentrantLockTest();
    //RunWindowsCriticalSectionTest();
    //RunCountDownEventTest();
    //RunExchangerTest();
    //RunWaitAllFutureTest();
    //RunWaitAnyFutureTest();
    //RunInitOnceSynchronousTest();
    //RunInitOnceInlineSynchronousTest();
    //RunInitOnceAsynchronousTest();
    //RunEnsureInitializedAsynchronousTest();
    //RunEnsureInitializedSynchronousTest();
    //RunLockTest();
    //RunMonitorTest();
    //RunNotificationEventTest();
    //RunFairLockTest();
    //RunReentrantFairLockTest();
    //RunReadWriteLockTest();
    //RunReentrantReadWriteLockTest();
    //RunMultipleReentrantReadWriteLockTest();
    //RunRendezvousChannelTest();
    //RunSemaphoreTest();
    //RunSignalAndWaitTest();
    //RunStreamBlockingQueueTest();
    //RunStSynchronizationEventTest();
    //RunLinkedBlockingQueueTest();
    //RunWaitAllForFairLockTest();
    //RunWaitAllForReadWriteLockTest();
    //RunWaitAllForSynchronizationEventTest();
    //RunWaitAllForNotificationEventTest();
    //RunWaitAllForSemaphoreTest();
    //RunWaitForMultipleNotificationEventsTest();
    //RunWaitForMultipleSynchronizationEventsTest();
    //RunWaitForMultipleSemaphoresTest();
    //RunTimerTest();
    //RunUmsSchedulerTest();
    //RunLoggerTest();
    //RunRegisteredWaitTest();
    //RunRegisteredTakeTest();

    // printf("---Idle: %d%%\n", StUmsScheduler_GetIdlePercentage());
    printf("hit <enter> to exit...");
    getchar();
    return 0;
}
