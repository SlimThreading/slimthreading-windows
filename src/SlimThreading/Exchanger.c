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
// Waits until exchange the data item with another thread, the
// specified timeout expires or the wait is alerted.
//

ULONG
WINAPI
StExchanger_ExchangeEx (
    __inout PST_EXCHANGER Exchanger,
    __in PVOID OfferedData,
    __out PVOID *RetrievedData,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    WAIT_BLOCK WaitBlock;
    ST_PARKER Parker;
    PWAIT_BLOCK YourWaitBlock;
    ULONG WaitStatus;
    SPIN_WAIT Spin;

    //
    // Mark the wait block as non-initialized.
    //

    WaitBlock.Parker = NULL;

    do {
        if ((YourWaitBlock = Exchanger->Slot) != NULL) {
            
            if (CasPointer(&Exchanger->Slot, YourWaitBlock, NULL)) {
                if (TryLockParker(YourWaitBlock->Parker)) {
                    *RetrievedData = YourWaitBlock->Channel;
                    YourWaitBlock->Channel = OfferedData;
                    UnparkThread(YourWaitBlock->Parker, WAIT_SUCCESS);
                    return WAIT_SUCCESS;
                } else {

                    //
                    // The request was cancelled by timeout or alert.
                    // So, mark the wait block as unlinked.
                    //

                    YourWaitBlock->WaitListEntry.Flink = &YourWaitBlock->WaitListEntry;
                }
            }
        } else {

            //
            // Initialize the wait block if it isn't initialized.
            // Since this is the first loop iteration, check if a
            // null timeout was specified and return failure, if so.
            //
            
            if (WaitBlock.Parker == NULL) {
                if (Timeout == 0) {
                    return WAIT_TIMEOUT;
                }
                InitializeParkerAndWaitBlockEx(&WaitBlock, &Parker, 0, OfferedData, WaitAny, 0);
                WaitBlock.WaitListEntry.Flink = NULL;	// Ensure non-unlinked
            }
            if (CasPointer(&Exchanger->Slot, NULL, &WaitBlock)) {
                break;
            }
        }
    } while (TRUE);

    //
    // Park the current thread activating the specified cancellers.
    //

    WaitStatus = ParkThreadEx(&Parker, Exchanger->SpinCount, Timeout, Alerter);

    //
    // If someone exchanged a data item with us, retrived data and
    // return a successful wait status.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        *RetrievedData = WaitBlock.Channel;
        return WAIT_SUCCESS;
    }

    //
    // The exchange operation was cancelled due to timeout or alert.
    // If our wait block is still pointed by the Slot field, try
    // to unlink it. It this succeeds, we can return immediately.
    // 

    if (Exchanger->Slot == &WaitBlock && CasPointer(&Exchanger->Slot, &WaitBlock, NULL)) {
        return WaitStatus;
    }

    //
    // When we get here, we must spin until our wait block is marked
    // as unlinked by some other thread.
    //

    InitializeSpinWait(&Spin);
    while (WaitBlock.WaitListEntry.Flink !=  &WaitBlock.WaitListEntry) {
        SpinOnce(&Spin);
    }

    //
    // Return the appropriate failure wait status.
    //

    return WaitStatus;
}

//
// Initializes the specified exchanger.
//

VOID
WINAPI
StExchanger_Init (
    __out PST_EXCHANGER Exchanger,
    __in ULONG SpinCount
    )
{
    Exchanger->Slot = NULL;
    Exchanger->SpinCount = IsMultiProcessor() ? SpinCount : 0;
}
