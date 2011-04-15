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

/*++
 *
 * This program demonstrates the use of linked blocking queues to implement
 * a logger.
 * The writes to the log file is performed by low priority worker threads.
 * The LogMessage() function must be as fast as possible and can discard
 * the message to log if specified a null timeout and the maximum buffer
 * capacity was reached.
 * In order to miminize the cost of dynamic memory allocation, the buffers
 * used to store the messages to log are recycled, unsing also a linked
 * blocking queue.
 * The shutdown of the the logger is orderly. This means, that it is assured
 * that all messages were added to the logger were written to the log file.
 * The shutdown of the logger is implemented using an alerter and the dispose
 * semantics of the linked blocking queues.
 * 
 --*/
 
#include "stdafx.h"


//
// Returns the number of processors on the system.
//
    
static
ULONG
GetProcessorCount(
    )
{
    SYSTEM_INFO Info;
    
    GetSystemInfo(&Info);
    return Info.dwNumberOfProcessors;
}

//
// The buffer that holds each log message.
//

#define MAX_LOG_SIZE	79

typedef struct _LOG_BUFFER {
    SLIST_ENTRY Link;
    ULONG Count;
    char Contents[MAX_LOG_SIZE + 1];
} LOG_BUFFER, *PLOG_BUFFER;

//
// The type that implements the logger.
//

struct LOGGER {
    HANDLE LogFileHandle;					// Log file
    StLinkedBlockingQueue FreeQueue;		// Free buffer queue
    StLinkedBlockingQueue LogQueue;			// Log queue
    StAlerter IsShutingDown;				// Shutdown alerter
    StCountDownEvent LoggerThreadsExited;	// Exit count down event 
    LONG MaxCapacity;						// Maximum capacity 
    volatile LONG AvailableCapacity;		// Available capacity
    volatile LONG Logged;
    
#define StartOfLogMessage	"*** START OF LOG ***\n"
#define EndOfLogMessage		"*** END OF LOG ***\n"
        
    //
    // Returns a free buffer or null within the specified time.
    //
    
    PLOG_BUFFER GetBuffer(ULONG Timeout) {
        PSLIST_ENTRY BufferItem;
        if (FreeQueue.Take(&BufferItem, 0) == WAIT_SUCCESS) {
            return CONTAINING_RECORD(BufferItem, LOG_BUFFER, Link);
        }
        if (AvailableCapacity > 0 && InterlockedDecrement(&AvailableCapacity) >= 0) {
            return (PLOG_BUFFER)malloc(sizeof(LOG_BUFFER));
        }

        //
        // If a non-null timeout was specified, wait for the specified
        // time until a buffer is available.
        // However, if the queue is disposed due to the logger shutdown,
        // we return null.
        //
        
        if (Timeout != 0 && FreeQueue.Take(&BufferItem, Timeout) == WAIT_SUCCESS) {
            return CONTAINING_RECORD(BufferItem, LOG_BUFFER, Link);
        }
        return NULL;
    }

    //
    // Adds a mensage to the log queue.
    //
    
    bool AddLogMessage(ULONG Timeout, char *Format, ...) {
        va_list Args;
        va_start(Args, Format);
        
        //
        // Check if the logger is still active and validate message size.
        //
        
        if (IsShutingDown.IsSet() || _vscprintf(Format, Args) > MAX_LOG_SIZE) {
            return false;
        }
        
        PLOG_BUFFER Buffer;
        if ((Buffer = GetBuffer(Timeout)) == NULL) {
            return false;
        }
        
        //
        // Compose the message and add it to the logger queue.
        //
        
        Buffer->Count = vsprintf_s(Buffer->Contents, MAX_LOG_SIZE, Format, Args);
        if (LogQueue.Add(&Buffer->Link)) {
            return true;
        }

        //
        // The queue was meanwhile disposed. So, free the allocated
        // buffer and return failure.
        //
        
        free(Buffer);
        return false;
    }
    
    //
    // Write a list of messages to the log file and recycles or
    // frees the underlying buffers as appropriate.
    //
    
    void WriteListToLogFile(PSLIST_ENTRY List, BOOL Recycle) {
        PSLIST_ENTRY Entry;
        
        Entry = List;
        do {
            ULONG Written;
            PLOG_BUFFER LogBuffer = CONTAINING_RECORD(Entry, LOG_BUFFER, Link);
            if (!WriteFile(LogFileHandle, LogBuffer->Contents, LogBuffer->Count,
                           &Written, NULL)) {
                assert(!"***write failed");
            }
            InterlockedIncrement(&Logged);
            if (Recycle) {
                Entry = Entry->Next;
            } else {
                PSLIST_ENTRY Next = Entry->Next;
                free(LogBuffer);
                Entry = Next;
            }
        } while (Entry != NULL);
        if (Recycle) {
            FreeQueue.AddList(List);
        }
    }
    
    //
    // Entry point of the the logger threads.
    //

    static ULONG WINAPI LoggerThreadEntry(PVOID Argument) {

        //
        // Lower the current thread priority and execute the thread's body.
        //
        
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        ((LOGGER *)Argument)->LoggerThreadBody();
        return 0;
    }

    //
    // Callback called when the logger queue is disposed.
    //
    
    static void CALLBACK LogDisposeCallback(PSLIST_ENTRY List, PVOID Context) {
        ((LOGGER *)Context)->WriteListToLogFile(List, FALSE);
    }

    //
    // Logger thread body.
    //
    
    void LoggerThreadBody() {
        PSLIST_ENTRY List;
        ULONG WaitStatus;
        
        do {

            //
            // Get all available log messages.
            //

            WaitStatus = LogQueue.TakeAll(&List, INFINITE, &IsShutingDown);
            if (WaitStatus == WAIT_SUCCESS) {

                //
                // Write the gotten messages to the log file.
                //

                WriteListToLogFile(List, TRUE);
            } else {
                
                //
                // The thread was alerted or the log queue was disposed.
                // So, if the thread was alerted dispose the log queue.
                // Anyway, the current thread exits.
                //
            
                assert(WaitStatus == WAIT_ALERTED || WaitStatus == WAIT_DISPOSED);
                if (WaitStatus == WAIT_ALERTED) {
                    LogQueue.Dispose(LogDisposeCallback, this);
                }
                break;
            }
        } while (true);
        
        //
        // Signal the count down event that synchronizes the logger
        // threads' termination.
        //
        
        LoggerThreadsExited.Signal();
    }

    //
    // Callback called when the free queue is disposed.
    //
    
    static void CALLBACK FreeDisposeCallback(PSLIST_ENTRY List, PVOID Ignored) {
        PSLIST_ENTRY Next;
        do {
            Next = List->Next;
            free(CONTAINING_RECORD(List, LOG_BUFFER, Link));
        } while ((List = Next) != NULL);
    }
    
    //
    // Shuts down the logger.
    //
    
    bool Shutdown() {

        //
        // Set the shutdown alerter.
        //

        if (IsShutingDown.Set()) {

            //
            // Wait until all the logger threads terminate.
            //

            LoggerThreadsExited.Wait();

            //
            // Dispose the free queue, freeing the buffers.
            //

            FreeQueue.Dispose(FreeDisposeCallback);

            //
            // Write the log tail message and close the log file.
            //

            ULONG Written;
            if (!WriteFile(LogFileHandle, EndOfLogMessage,
                          (ULONG)strlen(EndOfLogMessage), &Written, NULL)) {
                assert(!"***write failed");
            }
            printf("--- Logger(Allocated buffers = %d, Logged messages = %d)\n",
                   (AvailableCapacity <= 0) ? MaxCapacity : MaxCapacity - AvailableCapacity, Logged);
                   
            CloseHandle(LogFileHandle);
            return true;
        } else {

            //
            // The alerter was already set. So, some other thread
            // is conducting the logger shutdown.
            //

            return false;
        }
    }

    //
    // The destructor.
    //
    
    ~LOGGER() {
        Shutdown();
    }
    
    //
    // The constructor.
    //
    
    LOGGER(HANDLE LogHandle, ULONG Capacity, ULONG Processors)
            : LoggerThreadsExited(Processors)  {
        
        LogFileHandle = LogHandle;
        MaxCapacity = AvailableCapacity = Capacity;
        Logged = 0;
        ULONG Written;
        if (!WriteFile(LogFileHandle, StartOfLogMessage,
                       (ULONG)strlen(StartOfLogMessage), &Written, NULL)) {
            assert(!"*** write failed");
        }
        
        //
        // Create as many logger threads as the number of processors.
        //
        
        for (ULONG i = 0; i < Processors; i++) {
            HANDLE LoggerHandle = CreateThread(NULL, 0, LoggerThreadEntry, this,
                                               0, NULL);
            assert(LoggerHandle != NULL);
            CloseHandle(LoggerHandle);
        }
    }	
};

//
// The number of logger clients.
//

#define LOGGER_CLIENTS	20

//
// The context passed by the primary thread to the logger
// client threads.
//

typedef struct LOGGER_CLIENT_CTX {
    ULONG Id;
    LOGGER *Logger;
    ULONG Logged;
    ULONG Discarded;
    StAlerter *Shutdown;
    StCountDownEvent *Done;
} LOGGER_CLIENT_CTX, *PLOGGER_CLIENT_CTX;

//
// The logger client threads.
//

static
ULONG
WINAPI
LoggerClient (
    __in PVOID Argument
    )
{
    PLOGGER_CLIENT_CTX Context = (PLOGGER_CLIENT_CTX)Argument;
    ULONG LogIndex = 1;
    
    printf("+++ client #%d started...\n", Context->Id);
    srand(((ULONG)(&LogIndex) >> 12) & ((1 << 16) - 1));
    Context->Logged = Context->Discarded = 0;
    do {
        if ((rand() % 100) >= 90) {
            if (StParker::Sleep((rand() % 50) + 1, Context->Shutdown) == WAIT_ALERTED) {
                break;
            }
        }
        if (Context->Logger->AddLogMessage(((Context->Id & 1) == 0) ? INFINITE : 0,
                            "-->client #%d: message[%d]\n", Context->Id, LogIndex)) {
            Context->Logged++;
        } else {
            Context->Discarded++;
        }
        
        //
        // Display something in order to show progress.
        //
        
        if ((++LogIndex % 250) == 0) {
            printf("-%d", Context->Id);
        }
    } while (true);	
    printf("+++ client #%d exiting: [%d/%d]\n", Context->Id, Context->Logged, Context->Discarded);
    Context->Done->Signal();
    return 0;
}
    
//
// The test controller function.
//

VOID
RunLoggerTest (
    )
{
    //
    // Create the log file.
    //

    HANDLE LogFileHandle = CreateFileA("d:\\tmp\\logger.dat", GENERIC_WRITE, 0, NULL,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (LogFileHandle == NULL) {
        printf("***can't create the log file, error: 0x%08x\n", GetLastError());
        return;
    }
    
    LOGGER_CLIENT_CTX Contexts[LOGGER_CLIENTS];

    //
    // Initialize the alerter and the count down event used to
    // synchronize the test shutdown.
    //

    StAlerter Shutdown;
    StCountDownEvent Done(LOGGER_CLIENTS);
    
    //
    // Initialize the logger.
    //
    
    LOGGER Logger(LogFileHandle, 128, GetProcessorCount());
    
    //
    // Create the configured number of logger client threads.
    //
    
    for (int i = 0; i < LOGGER_CLIENTS; i++) {
        PLOGGER_CLIENT_CTX Context = &Contexts[i];
        Context->Id = i;
        Context->Logger = &Logger;
        Context->Shutdown = &Shutdown;
        Context->Done = &Done;
        HANDLE ClientHandle = CreateThread(NULL, 0, LoggerClient, Context, 0, NULL);
        assert(ClientHandle != NULL);
        CloseHandle(ClientHandle);
    }
    
    //
    // Wait <enter> to terminate the test.
    //
    
    printf("+++ hit <enter> to exit...\n");
    getchar();
    
    //
    // Set the alerter and wait until all client threads terminate.
    //
    
    Shutdown.Set();
    Done.Wait();
    Logger.Shutdown();
    
    //
    // Compute and display results.
    //
    
    ULONGLONG TotalLogged = 0, TotalDiscarded = 0;
    for (int i = 0; i < LOGGER_CLIENTS; i++) {
        TotalLogged += Contexts[i].Logged;
        TotalDiscarded += Contexts[i].Discarded;
    }
    printf("+++ Logged = %I64d, Discarded = %I64d\n", TotalLogged, TotalDiscarded);
}
