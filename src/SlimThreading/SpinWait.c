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
// Yield frequency.
//

#define YIELD_FREQUENCY	4000


//
// Spins yielding the processor for the specified number
// of iterations.
//

VOID
FASTCALL
SpinWait (
    __in LONG Iterations
    )
{
    LONG Index = 0;
    do {
        YieldProcessor();
        Index = *((volatile LONG *)(&Index)) + 1;
    } while (Index < Iterations);
}

//
// Spins once.
//

VOID
FASTCALL
SpinOnce (
    __inout PSPIN_WAIT Spinner
    )
{
    LONG Count;

    Count = ++Spinner->Count & ~(1 << 31);
    if (IsMultiProcessor()) {
        LONG Remainder = Count % YIELD_FREQUENCY;
        if (Remainder > 0) {
            SpinWait((LONG)(1.0f + (Remainder * 0.032f)));
        } else {
            StThread_Yield();
        }
    } else {
        StThread_Yield();
    }
}
