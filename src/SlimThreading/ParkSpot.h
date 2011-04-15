#pragma once
#include <windows.h>

/*++
 *
 * The Park Spot API.
 *
 --*/

//
// Internal functions use the fastcall call convention.
//


#ifndef FASTCALL
#define FASTCALL	__fastcall
#endif

//
// Allocates a park spot to block the current thread.
//

HANDLE
FASTCALL
AllocParkSpot (
	);

//
// Frees a previously allocated park spot.
//

VOID
FASTCALL
FreeParkSpot (
	__in HANDLE ParkSpotHandle
	);

//
// Waits on the specified park spot until it is set or the
// specified timeout expires.
//

ULONG
FASTCALL
WaitForParkSpot (
	__inout PST_PARKER Parker,
	__in HANDLE ParkSpotHandle,
	__in ULONG Timeout
	);

//
// Sets the specified park spot.
//

VOID
FASTCALL
SetParkSpot (
	__in HANDLE ParkSpot
	);
//
// Sets or clears the COM STA affinity for the current thread.
//

BOOLEAN
FASTCALL
SetStaAffinity (
	__in BOOLEAN StaAffinity
	);


/*++
 *
 * Timer List.
 *
 --*/


//
// Initializes the timer list.
//


BOOLEAN
FASTCALL
InitializeTimerList (
	);

//
// Disposes the timer list.
//

VOID
FASTCALL
DisposeTimerList (
	);
