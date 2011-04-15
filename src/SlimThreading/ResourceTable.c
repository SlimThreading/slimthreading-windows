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
// Adds a new entry to the resource table.
//

PRESOURCE_TABLE_ENTRY
FASTCALL
AddEntryToResourceTable (
    __inout PRESOURCE_TABLE Table,
    __in PVOID Resource
    )
{
    PRESOURCE_TABLE_ENTRY Entry;
    PRESOURCE_TABLE_ENTRY NewEntries;
    ULONG NextFree;
    ULONG NewSize;

    //
    // If the current resource table table is full or its size
    // is still zero, expand it doubling its size or allocate
    // a chunk of table entries.
    //

    if ((NextFree = Table->NextFree) == Table->Size) {

        //
        // Compute the new table size.
        //

        if (NextFree == 0) {
            NewSize = RESOURCE_TABLE_INITIAL_SIZE;
        } else {
            NewSize = NextFree << 1;
        }

        //
        // Allocate memory to hold the table entries.
        //

        NewEntries = (PRESOURCE_TABLE_ENTRY)HeapAlloc(GetProcessHeap(), 0, 
                                                      NewSize * sizeof(RESOURCE_TABLE_ENTRY));
        //
        // If we can't allocate memory, return failure.
        //

        if (NewEntries == NULL) {
            return NULL;
        }

        //
        // If appropriate, copy the contents of the old table to the
        // new table.
        //

        if (Table->Entries != NULL) {
            CopyMemory(NewEntries, Table->Entries, NextFree * sizeof(RESOURCE_TABLE_ENTRY));
            HeapFree(GetProcessHeap(), 0, Table->Entries);
        }

        //
        // Update the entries pointer, the table size since that
        // the *NextFree* field is already correct.
        //
        
        Table->Entries = NewEntries;
        Table->Size = NewSize;
    }

    //
    // Allocate a new entry, initialize it and return success.
    //
    
    Entry = Table->Entries + Table->NextFree++;
    Entry->Resource = Resource;
    Entry->RefCount = 1;
    return Entry;
}

//
// Free the resource table entry.
//

VOID
FASTCALL
_FreeResourceTableEntry (
    __inout PRESOURCE_TABLE Table,
    __in PRESOURCE_TABLE_ENTRY Entry
    )
{
    ULONG Index;
    ULONG LastIndex;

    _ASSERTE(Table->NextFree != 0);

    Index = (ULONG)(Table->Entries - Entry);			
    LastIndex = Table->NextFree - 1;
    if (Index != LastIndex) {

        //
        // Copy the last entry of the table to the now freed one.
        // By doing so, we never have free entries in the middle of
        // the resource tables.
        //

        *Entry = Table->Entries[LastIndex];
    }
    Table->NextFree = LastIndex;
}
