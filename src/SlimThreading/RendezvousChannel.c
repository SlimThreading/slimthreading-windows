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
// Types of requests used with wait blocks.
//

#define SEND_AND_WAIT_REPLY		1
#define SEND_ONLY				2
#define RECEIVE					3

//
// Acquire and release the channel's lock.
//

static
FORCEINLINE
VOID
AcquireChannelLock (
    __inout PST_RENDEZVOUS_CHANNEL Channel
    )
{
    AcquireSpinLock(&Channel->Lock);
}

static
FORCEINLINE
VOID
ReleaseChannelLock (
    __inout PST_RENDEZVOUS_CHANNEL Channel
    )
{
    ReleaseSpinLock(&Channel->Lock);
}

//
// Unlinks the specified entry from the channel's queue.
//

static
FORCEINLINE
VOID
UnlinkEntryList (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __inout PLIST_ENTRY_ Entry
    )
{
    if (Entry->Flink != Entry) {
        AcquireChannelLock(Channel);
        if (Entry->Flink != Entry) {
            RemoveEntryList(Entry);
        }
        ReleaseChannelLock(Channel);
    }
}

//
// Sends a message through the rendezvous channel and waits until
// the message is received, the specified timeout expires or the
// specified alerter. If specified, after the message is received
// the sender thread waits unconditionally until reply.
//

ULONG
WINAPI
StRendezvousChannel_SendAndWaitReplyEx (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __in PVOID Message,
    __out_opt PVOID *Response,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    PWAIT_BLOCK RecvWaitBlock;
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    ULONG WaitStatus;
    BOOLEAN EnableCancel;

    //
    // Very often, this function blocks the current thread, so
    // we initialize the wait block before acquire the channel's lock.
    //

    InitializeWaitBlockEx(&WaitBlock, &Parker,
                         (Response == NULL) ? SEND_ONLY : SEND_AND_WAIT_REPLY,
                          Message, WaitAny, WAIT_SUCCESS);
    ListHead = &Channel->ReceiveWaitList;
    EnableCancel = TRUE;
    AcquireChannelLock(Channel);

    //
    // If there is a waiting receiver thread, deliver the message
    // directly to it.
    //
    
    if ((Entry = ListHead->Flink) != ListHead) {
        do {
            RemoveEntryList(Entry);
            RecvWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            if (TryLockParker(RecvWaitBlock->Parker)) {

                //
                // Release the channel's lock and pass the message to the
                // waiting receiver thread.
                //
                
                ReleaseChannelLock(Channel);

                //
                // If we don't expect a reply, pass the message to the
                // receiver thread, unparked it with a distinguished
                // wait status (i.e. WAIT_OBJECT_0 + 1) and return
                // success.
                //

                if (Response == NULL) {
                    RecvWaitBlock->Channel = Message;
                    UnparkThread(RecvWaitBlock->Parker, WAIT_OBJECT_0 + 1);
                    return WAIT_SUCCESS;
                } else {

                    //
                    // We expect a reply; so, pass our locked wait block to the
                    // receiver thread and wait until the reply.
                    //

                    InitializeParker(&Parker, 0);
                    RecvWaitBlock->Channel = &WaitBlock;
                    UnparkThread(RecvWaitBlock->Parker, WAIT_OBJECT_0);
                    EnableCancel = FALSE;
                    goto ParkThread;
                }
            } else {

                //
                // The request was cancelled due to timeout or alert,
                // so mark the wait block as unlinked.
                //

                Entry->Flink = Entry;
            }
        } while ((Entry = ListHead->Flink) != ListHead);
    }

    //
    // There is no receiver thread waiting; so, if a null timeout was
    // specified, release the channel's lock and return failure.
    //

    if (Timeout == 0) {
        ReleaseChannelLock(Channel);
        return WAIT_TIMEOUT;
    }

    //
    // Insert the wait block in the port sender's wait list and
    // release the port's lock.
    //

    InitializeParker(&Parker, 1);
    InsertTailList(&Channel->SendWaitList, &WaitBlock.WaitListEntry);
    ReleaseChannelLock(Channel);

ParkThread:

    //
    // Park the current thread activating the specified cancellers if
    // the message wasn't still received; otherwise, wait unconditionally.
    //

    WaitStatus = EnableCancel ? ParkThreadEx(&Parker, 0, Timeout, Alerter)
                              : ParkThreadEx(&Parker, 0, INFINITE, NULL);

    //
    // If we received the reply, retrieve it and return success.
    //

    if (WaitStatus == WAIT_SUCCESS) {
        if (Response != NULL) {
            *Response = (PVOID)WaitBlock.Channel;
        }
        return WAIT_SUCCESS;
    }

    //
    // The send operation was cancelled due to timeout or alert;
    // so, remove the wait block from the channel sender's wait list
    // and return the appropriate failure status.
    //

    UnlinkEntryList(Channel, &WaitBlock.WaitListEntry);
    return WaitStatus;
}

//
// Sends a message through the rendezvous channel and waits unconditionally
// until it is replied.
//

BOOL
WINAPI
StRendezvousChannel_SendAndWaitReply (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __in PVOID Message,
    __out_opt PVOID *Response
    )
{
    return StRendezvousChannel_SendAndWaitReplyEx(Channel, Message, Response,
                                                  INFINITE, NULL) == WAIT_SUCCESS;
}

//
// Sends a message through the rendezvous channel and waits until it
// is received, activating the specified cancellers.
//

ULONG
WINAPI
StRendezvousChannel_SendEx (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __in PVOID Message,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    return  StRendezvousChannel_SendAndWaitReplyEx(Channel, Message, NULL, Timeout, Alerter);
}

//
// Sends a message through the rendezvous channel and waits unconditionally
// until it is received.
//

BOOL
WINAPI
StRendezvousChannel_Send (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __in PVOID Message
    )
{
    return  StRendezvousChannel_SendAndWaitReplyEx(Channel, Message, NULL,
                                                   INFINITE, NULL) == WAIT_SUCCESS;
}

//
// Waits until receive a message from the rendezvous channel, activating
// the specified cancellers.
//

ULONG
WINAPI
StRendezvousChannel_ReceiveEx (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __out PVOID *RvToken,
    __out PVOID *ReceivedMessage,
    __in ULONG Timeout,
    __inout_opt PST_ALERTER Alerter
    )
{
    ST_PARKER Parker;
    WAIT_BLOCK WaitBlock;
    PWAIT_BLOCK SenderWaitBlock;
    PLIST_ENTRY_ ListHead;
    PLIST_ENTRY_ Entry;
    ULONG WaitStatus;

    //
    // Initialize the local variable and acquire the channel's lock.
    //

    ListHead = &Channel->SendWaitList;
    AcquireChannelLock(Channel);

    //
    // If there are waiting senders, try to get a message from one of them.
    //

    if ((Entry = ListHead->Flink) != ListHead) {
        do {
            RemoveEntryList(Entry);
            SenderWaitBlock = CONTAINING_RECORD(Entry, WAIT_BLOCK, WaitListEntry);
            if (TryLockParker(SenderWaitBlock->Parker)) {
                ReleaseChannelLock(Channel);

                //
                // Get the sent message. Then, if the sender does not expect
                // a response, unpark it immediately. Otherwise, the sender will
                // by unparked later by the ReplyToRendezvousMessage function.
                //

                *ReceivedMessage = (PVOID)SenderWaitBlock->Channel;
                if (SenderWaitBlock->WaitType == SEND_ONLY) {
                    UnparkThread(SenderWaitBlock->Parker, WAIT_SUCCESS);
                    *RvToken = NULL;
                } else {
                    *RvToken = SenderWaitBlock;
                }
                return WAIT_SUCCESS;
            } else {

                //
                // The receive operation was cancelled due to timeout
                // or alert, so mark the wait block as unlinked.
                //

                Entry->Flink = Entry;
            }
        } while ((Entry = ListHead->Flink) != ListHead);
    }

    //
    // No sender is waiting; so, if a null timeout was specified,
    // release the channel's lock and return failure.
    //
    
    if (Timeout == 0) {
        ReleaseChannelLock(Channel);
        return WAIT_TIMEOUT;
    }
    
    //
    // Initialize a wait block, insert it on the port's receiver
    // wait list according to the configured service discipline and
    // release the channel's lock.
    //

    ListHead = &Channel->ReceiveWaitList;
    InitializeParkerAndWaitBlock(&WaitBlock, &Parker, RECEIVE, WaitAny, WAIT_SUCCESS);
    if ((Channel->Flags & LIFO_WAIT_LIST) != 0) {
        InsertHeadList(ListHead, &WaitBlock.WaitListEntry);
    } else {
        InsertTailList(ListHead, &WaitBlock.WaitListEntry);
    }
    ReleaseChannelLock(Channel);

    //
    // Park the current thread, activating the specified cancellers.
    //

    WaitStatus = ParkThreadEx(&Parker, 0, Timeout, Alerter);

    if (WaitStatus == WAIT_OBJECT_0) {

        //
        // The sender thread expects a reply.
        // The sender's wait block is passed through the Channel field
        // of our wait block and the message is passed through the
        // Channel field of the sender's wait block. In this case, 
        // We return the sender wait block's address as rendezvous
        // token.
        //

        SenderWaitBlock = (PWAIT_BLOCK)WaitBlock.Channel;
        *ReceivedMessage = SenderWaitBlock->Channel;
        *RvToken = SenderWaitBlock;
        return WAIT_SUCCESS;
    }

    if (WaitStatus == (WAIT_OBJECT_0 + 1)) {

        //
        // The sender thread doesn't expect a reply.
        // Get the sent message is passed through the Channel field
        // of our wait block and return a NULL rendezvous token.
        //
        
        *ReceivedMessage = WaitBlock.Channel;
        *RvToken = NULL;
        return WAIT_SUCCESS;
    }

    //
    // The receive was cancelled due to timeout or alert;
    // so, unlink the wait block from the port receivers'
    // wait list and return the appropriate failure status.
    //

    UnlinkEntryList(Channel, &WaitBlock.WaitListEntry);
    return WaitStatus;
}

//
// Waits unconditionally until receive a message from the rendezvous channel.
//

BOOL
WINAPI
StRendezvousChannel_Receive (
    __inout PST_RENDEZVOUS_CHANNEL Channel,
    __out PVOID *RvToken,
    __out PVOID *ReceivedMessage
    )
{
    return StRendezvousChannel_ReceiveEx(Channel, RvToken, ReceivedMessage,
                                         INFINITE, NULL) == WAIT_SUCCESS;
}

//
// Replies to the message identified by the specified
// rendezvous token.
//

VOID
WINAPI
StRendezvousChannel_Reply (
    __in PVOID RvToken,
    __in PVOID ResponseMessage
    )
{
    PWAIT_BLOCK RecvWaitBlock;

    //
    // The rendezvous token is the sender's wait block.
    //

    RecvWaitBlock = (PWAIT_BLOCK)RvToken;

    //
    // Pass the response message and unpark the sender thread.
    //

    RecvWaitBlock->Channel = ResponseMessage;
    UnparkThread(RecvWaitBlock->Parker, WAIT_SUCCESS);
}


//
// Initializes the rendezvous channel.
//

VOID
WINAPI
StRendezvousChannel_Init (
    __out PST_RENDEZVOUS_CHANNEL Channel,
    __in BOOL LifoService
    )
{
    InitializeSpinLock(&Channel->Lock, SHORT_CRITICAL_SECTION_SPINS);
    InitializeListHead(&Channel->SendWaitList);
    InitializeListHead(&Channel->ReceiveWaitList);
    Channel->Flags = LifoService ? LIFO_WAIT_LIST : 0;
}
