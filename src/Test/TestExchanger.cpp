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

#define EXCHANGERS		20

//
// The exchanger.
//

static StExchanger Exchanger(100);

//
// The alerter and the count down event used for shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(EXCHANGERS);


//
// Exchange counters.
//

static ULONG Exchanges[EXCHANGERS];

//
// Exchanger thread
//

static
ULONG
WINAPI
ExchangerThread (
    __in PVOID arg
    )
{

    ULONG Id = (LONG)arg;
    ULONG Timeouts = 0;
    PVOID YourId;
    ULONG WaitStatus;

    printf("+++ xchg #%d started...\n", Id);
    do {
        WaitStatus = Exchanger.Exchange((PVOID)Id, &YourId, 1, &Shutdown);
        if (WaitStatus == WAIT_OBJECT_0) {
            if ((ULONG)(YourId) != Id) {
                if ((Exchanges[Id]++ % 100000) == 0) {
                    printf("-%d", Id);	
                }
            } else {
                printf("*** xchg #%d: wrong exchange, got: %d\n",
                        Id, (ULONG)YourId);
            }
        } else if (WaitStatus == WAIT_ALERTED) {
            break;
        } else {
            Timeouts++;
        }
    } while (!Shutdown.IsSet());
    printf("+++ xchg #%d exits: [%d/%d]\n", Id, Exchanges[Id], Timeouts);
    Done.Signal();
    return 0;
}

VOID
RunExchangerTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < EXCHANGERS; i++) {
        HANDLE Exchanger = CreateThread(NULL, 0, ExchangerThread, (PVOID)(i), 0, NULL);
        //HANDLE Exchanger = StUmsThread_Create(NULL, 0, ExchangerThread, (PVOID)(i), 0, NULL);
        CloseHandle(Exchanger);
    }

    getchar();
    Shutdown.Set();

    //
    // Wait until all exchanger thread exit.
    //

    Done.Wait();

    ULONGLONG TotalExchanges = 0;
    for (int i = 0; i < EXCHANGERS; i++) {
        TotalExchanges += Exchanges[i];
    }
    printf("+++ total exchanges: %I64d\n", TotalExchanges);
}
