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
#include "st.h"

//
// Logic to compute the number of processors assigned to the current process.
//

#define REFRESH_INTERVAL_MS	30000

static ULONG NextRefreshTime = 0;
static ULONG ProcessorCount = 1;

ULONG
FASTCALL
GetProcessorCount (
    )
{
    ULONG_PTR ProcessAffinity;
    ULONG_PTR SystemAffinity;
    SYSTEM_INFO SysInfo;
    ULONGLONG ProcessMask;
    ULONG Count;
    
    if (GetTickCount() <= NextRefreshTime) {
        return ProcessorCount;
    }
    
    //
    // Get the process and system affinity mask.
    //

    if (!GetProcessAffinityMask(GetCurrentProcess(), &ProcessAffinity, &SystemAffinity)) {
        return ProcessorCount;
    }
    
    //
    // Get the number of processors in the system.
    //

    GetSystemInfo(&SysInfo);

    //
    // Compute the number of processors currently in use by the current process.
    //

    ProcessMask = (~0ULL >> ((sizeof(ULONG_PTR) * 8) - SysInfo.dwNumberOfProcessors)) & ProcessAffinity;
    Count = 0;
    while (ProcessMask > 0) {
        Count++;
        ProcessMask &= ProcessMask - 1;
    }
    NextRefreshTime = GetTickCount() + REFRESH_INTERVAL_MS;
    ProcessorCount = Count;
    return Count;
}
