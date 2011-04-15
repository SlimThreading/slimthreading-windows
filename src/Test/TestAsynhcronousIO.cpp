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
// ASYNC_RESULT
//

struct ASYNC_RESULT {
    OVERLAPPED NativeOverlapped;
    StNotificationEvent Completed;
    ULONG ErrorCode;
    ULONG BytesTransferred;

    void Initialize() {
        memset(this, 0, sizeof(*this));
    }

    ASYNC_RESULT() : Completed(false, 0) {
        Initialize();
    }

    void Seek(ULONG Offset) {
        NativeOverlapped.Offset = Offset;
    }

    LPOVERLAPPED Pack() {
        return &NativeOverlapped;
    }	
    
    static ASYNC_RESULT *Unpack(OVERLAPPED *Overlapped) {
        return CONTAINING_RECORD(Overlapped, ASYNC_RESULT, NativeOverlapped);
    }

    void OnCompletion(ULONG Status, ULONG Information) {
        ErrorCode = Status;
        BytesTransferred = Information;
        Completed.Set();
    }

    ULONG EndIo(PULONG Information) {
        Completed.Wait();
        *Information = BytesTransferred;
        return ErrorCode;
    }
};

#define BUFFER_SIZE		(8 * 1024)
UCHAR Buffer[BUFFER_SIZE];


VOID
CALLBACK
ReadIoCompletion (	
    __in ULONG ErrorCode,
    __in ULONG NumberOfBytesTransferred,
    __in LPOVERLAPPED Overlapped
    )
{
    ASYNC_RESULT * AsyncRes = ASYNC_RESULT::Unpack(Overlapped);
    AsyncRes->OnCompletion(ErrorCode, NumberOfBytesTransferred);
    if (NumberOfBytesTransferred != 0) {
        ULONG BlockNumber = AsyncRes->NativeOverlapped.Offset / BUFFER_SIZE;
        printf("-%04d", BlockNumber);
    }
    StParker_Sleep(10);
}

VOID
RunAsynchronousIOTest (
    )
{
    HANDLE FileHandle;
    ULONG BytesRead;
    ULONG Error;
    ASYNC_RESULT AsyncResult;
    ULONG TotalRead;

    FileHandle = CreateFileA( "c:\\windows\\System32\\ntoskrnl.exe", 
                              FILE_READ_DATA,
                              FILE_SHARE_READ,
                              NULL,
                              OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                              NULL);

    if (FileHandle == INVALID_HANDLE_VALUE) {
        printf("***CreateFile failed, error: %d\n", GetLastError());
        return;
    }

    //StParker_EnterStaApartment();
    TotalRead = 0;
    do {

        AsyncResult.Initialize();
        AsyncResult.Seek(TotalRead); 
        if (!ReadFileEx( FileHandle,
                         Buffer,
                         BUFFER_SIZE,
                         AsyncResult.Pack(),
                         ReadIoCompletion)) {
            if (GetLastError() == ERROR_HANDLE_EOF)
                break;
        }
        Error = AsyncResult.EndIo(&BytesRead);
        if (Error != ERROR_SUCCESS) {
            if (Error != ERROR_HANDLE_EOF) {
                printf("ReadFileEx rrror: %d\n", Error);
            }
            break;
        }
        TotalRead += BytesRead;
    } while (true);

    CloseHandle(FileHandle);
    printf("\n+++read %d bytes\n", TotalRead);
}
