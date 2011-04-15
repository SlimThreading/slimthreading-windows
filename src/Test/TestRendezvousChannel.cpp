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
// The number of threads.
//

#define SEND_ONLY_CLIENTS				10
#define SEND_AND_WAIT_REPLY_CLIENTS		10
#define CLIENTS							(SEND_ONLY_CLIENTS + SEND_AND_WAIT_REPLY_CLIENTS)
#define SERVERS							10

//
// The rendezvous channel.
//

static StRendezvousChannel Channel(true);

//
// The alerter and the count down latch used to synchronize the shutdown.
//

static StAlerter Shutdown;
static StCountDownEvent Done(CLIENTS + SERVERS);

//
// The counters.
//

static ULONG Calls[CLIENTS];
static ULONG Services[SERVERS];

//
// The service request message.
//

typedef struct _REQUEST {
    LONG Argument;
    LONG Result;
} REQUEST, *PREQUEST;

//
// The send and wait reply client thread.
//

static
ULONG
WINAPI
SendAndWaitReplyClient (
    __in PVOID arg
    )
{

    LONG Id = (LONG)arg;
    REQUEST Request;
    PREQUEST Response;
    ULONG Timeouts = 0;
    ULONG Number = 0;

    printf("+++ swr client #%d started...\n", Id);
    do {
        Request.Argument = Number;
        
        ULONG WaitStatus = Channel.SendAndWaitReply((PVOID)&Request, (PVOID *)&Response,
                                                     1, &Shutdown);
        if (WaitStatus == WAIT_SUCCESS) {
            if (Response == &Request && Request.Result == (Number + 1)) {
                if ((++Calls[Id + SEND_ONLY_CLIENTS] % 10000) == 0) {
                    printf("-sw%d", Id);
                }
            } else {
                printf("***error: swr client #%d got a wrong response\n", Id); 
            }
        } else if (WaitStatus == WAIT_ALERTED) {
            break;
        } else {
            Timeouts++;
        }
    } while (true);
    printf("+++ swr client #%d exits: [%d/%d]\n", Id, Calls[Id + SEND_ONLY_CLIENTS], Timeouts);
    Done.Signal();
    return 0;
}

//
// The send only client thread.
//

static
ULONG
WINAPI
SendOnlyClient (
    __in PVOID arg
    )
{

    LONG Id = (LONG)arg;
    PREQUEST Request;
    ULONG Timeouts = 0;
    ULONG Number = 0;

    printf("+++ so client #%d started...\n", Id);
    do {
        Request = (PREQUEST)malloc(sizeof(REQUEST));
        Request->Argument = Id;
        ULONG WaitStatus = Channel.Send((PVOID)Request, 1, &Shutdown);
        if (WaitStatus == WAIT_OBJECT_0) {
            if ((++Calls[Id] % 10000) == 0) {
                printf("-so%d", Id);
            }
        } else if (WaitStatus == WAIT_ALERTED) {
            break;
        } else {
            Timeouts++;
        }
    } while (true);
    printf("+++ so client #%d exits: [%d/%d]\n", Id, Calls[Id], Timeouts);
    Done.Signal();
    return 0;
}

//
// The server thread
//

static
ULONG
WINAPI
Server (
    __in PVOID arg
    )
{

    LONG Id = (LONG)arg;
    PREQUEST Request;
    PVOID Token;
    ULONG Timeouts = 0;

    printf("+++ server #%d started...\n", Id);
    do {
        ULONG WaitStatus = Channel.Receive(&Token, (PVOID *)&Request, 1, &Shutdown);
        if (WaitStatus == WAIT_SUCCESS) {

            if (Token == NULL) {
                assert(Request->Argument >= 0 && Request->Argument < SEND_ONLY_CLIENTS);
                free(Request);
            } else {
                Request->Result = Request->Argument + 1;
                StRendezvousChannel::Reply(Token, Request);
            }
            if ((++Services[Id] % 10000) == 0) {
                printf("-s%d", Id);
            }
        } else if (WaitStatus == WAIT_ALERTED) {
            break;
        } else {
            Timeouts++;
        }
        //Sleep(1);
    } while (!Shutdown.IsSet());
    printf("+++ server #%d exits: [%d/%d]\n", Id, Services[Id], Timeouts);
    Done.Signal();
    return 0;
}

VOID
RunRendezvousChannelTest (
    )
{
    SetThreadPriority(GetCurrentThread, THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < SERVERS; i++) {
        HANDLE Thread = CreateThread(NULL, 0, Server, (PVOID)(i), 0, NULL);
        CloseHandle(Thread);
    }
    for (int i = 0; i < CLIENTS; i++) {
        HANDLE Thread; 
        if (i < SEND_ONLY_CLIENTS) {
            Thread = CreateThread(NULL, 0, SendOnlyClient, (PVOID)(i), 0, NULL);
        } else {
            Thread = CreateThread(NULL, 0, SendAndWaitReplyClient, (PVOID)(i - SEND_ONLY_CLIENTS),
                         0, NULL);
        }
        CloseHandle(Thread);
    }

    printf("+++ hit <enter> to terminate the test...");
    getchar();
    Shutdown.Set();
    Done.Wait();

    ULONGLONG TotalCalls = 0;
    for (int i = 0; i < CLIENTS; i++) {
        TotalCalls += Calls[i];
    }

    ULONGLONG TotalServices = 0;
    for (int i = 0; i < SERVERS; i++) {
        TotalServices += Services[i];
    }

    printf("+++Total: calls = %I64d, services = %I64d\n", TotalCalls, TotalServices); 
}
