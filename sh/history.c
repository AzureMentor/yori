/**
 * @file sh/history.c
 *
 * Yori shell command history
 *
 * Copyright (c) 2017-2018 Malcolm J. Smith
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "yori.h"

/**
 List of command history.
 */
YORI_LIST_ENTRY YoriShCommandHistory;

/**
 Number of elements in command history.
 */
DWORD YoriShCommandHistoryCount;

/**
 The maximum number of history items to record concurrently.
 */
DWORD YoriShCommandHistoryMax;

/**
 Add an entered command into the command history buffer.  On error it is
 silently discarded and not propagated into history.

 @param NewCmd Pointer to a Yori string corresponding to the new
        entry to add to history.
 */
BOOL
YoriShAddToHistory(
    __in PYORI_STRING NewCmd
    )
{
    DWORD LengthToAllocate;
    PYORI_HISTORY_ENTRY NewHistoryEntry;

    if (NewCmd->LengthInChars == 0) {
        return TRUE;
    }

    LengthToAllocate = sizeof(YORI_HISTORY_ENTRY);

    NewHistoryEntry = YoriLibMalloc(LengthToAllocate);
    if (NewHistoryEntry == NULL) {
        return FALSE;
    }

    YoriLibCloneString(&NewHistoryEntry->CmdLine, NewCmd);

    if (YoriShCommandHistory.Next == NULL) {
        YoriLibInitializeListHead(&YoriShCommandHistory);
    }

    YoriLibAppendList(&YoriShCommandHistory, &NewHistoryEntry->ListEntry);
    YoriShCommandHistoryCount++;
    while (YoriShCommandHistoryCount > YoriShCommandHistoryMax) {
        PYORI_LIST_ENTRY ListEntry;
        PYORI_HISTORY_ENTRY OldHistoryEntry;

        ListEntry = YoriLibGetNextListEntry(&YoriShCommandHistory, NULL);
        OldHistoryEntry = CONTAINING_RECORD(ListEntry, YORI_HISTORY_ENTRY, ListEntry);
        YoriLibRemoveListItem(ListEntry);
        YoriLibFreeStringContents(&OldHistoryEntry->CmdLine);
        YoriLibFree(OldHistoryEntry);
        YoriShCommandHistoryCount--;
    }

    return TRUE;
}

/**
 Free all command history.
 */
VOID
YoriShClearAllHistory()
{
    PYORI_LIST_ENTRY ListEntry = NULL;
    PYORI_HISTORY_ENTRY HistoryEntry;

    ListEntry = YoriLibGetNextListEntry(&YoriShCommandHistory, NULL);
    while (ListEntry != NULL) {
        HistoryEntry = CONTAINING_RECORD(ListEntry, YORI_HISTORY_ENTRY, ListEntry);
        ListEntry = YoriLibGetNextListEntry(&YoriShCommandHistory, ListEntry);
        YoriLibRemoveListItem(&HistoryEntry->ListEntry);
        YoriLibFreeStringContents(&HistoryEntry->CmdLine);
        YoriLibFree(HistoryEntry);
        YoriShCommandHistoryCount--;
    }
}

/**
 Load history from a file if the user has requested this behavior by
 setting YORIHISTFILE.  Configure the maximum amount of history to retain
 if the user has requested this behavior by setting YORIHISTSIZE.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriShLoadHistoryFromFile()
{
    DWORD EnvVarLength;
    YORI_STRING UserHistFileName;
    YORI_STRING FilePath;
    HANDLE FileHandle;
    PVOID LineContext = NULL;
    YORI_STRING LineString;

    //
    //  Default the history buffer size to something sane.
    //

    YoriShCommandHistoryMax = 250;

    //
    //  See if the user has other ideas.
    //

    EnvVarLength = YoriShGetEnvironmentVariable(_T("YORIHISTSIZE"), NULL, 0);
    if (EnvVarLength != 0) {
        YORI_STRING HistSizeString;
        DWORD CharsConsumed;
        LONGLONG HistSize;

        if (!YoriLibAllocateString(&HistSizeString, EnvVarLength)) {
            return FALSE;
        }

        HistSizeString.LengthInChars = YoriShGetEnvironmentVariable(_T("YORIHISTSIZE"), HistSizeString.StartOfString, HistSizeString.LengthAllocated);

        if (HistSizeString.LengthInChars == 0 || HistSizeString.LengthInChars >= HistSizeString.LengthAllocated) {
            YoriLibFreeStringContents(&UserHistFileName);
            return FALSE;
        }

        if (YoriLibStringToNumber(&HistSizeString, TRUE, &HistSize, &CharsConsumed) && CharsConsumed > 0) {
            YoriShCommandHistoryMax = (DWORD)HistSize;
        }

        YoriLibFreeStringContents(&HistSizeString);
    }

    //
    //  Check if there's a file to load saved history from.
    //
    
    EnvVarLength = YoriShGetEnvironmentVariable(_T("YORIHISTFILE"), NULL, 0);
    if (EnvVarLength == 0) {
        return TRUE;
    }

    if (!YoriLibAllocateString(&UserHistFileName, EnvVarLength)) {
        return FALSE;
    }

    UserHistFileName.LengthInChars = YoriShGetEnvironmentVariable(_T("YORIHISTFILE"), UserHistFileName.StartOfString, UserHistFileName.LengthAllocated);

    if (UserHistFileName.LengthInChars == 0 || UserHistFileName.LengthInChars >= UserHistFileName.LengthAllocated) {
        YoriLibFreeStringContents(&UserHistFileName);
        return FALSE;
    }

    if (!YoriLibUserStringToSingleFilePath(&UserHistFileName, TRUE, &FilePath)) {
        YoriLibFreeStringContents(&UserHistFileName);
        return FALSE;
    }

    YoriLibFreeStringContents(&UserHistFileName);

    FileHandle = CreateFile(FilePath.StartOfString,
                            GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);

    if (FileHandle == NULL || FileHandle == INVALID_HANDLE_VALUE) {
        DWORD LastError = GetLastError();
        LPTSTR ErrText = YoriLibGetWinErrorText(LastError);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("yori: open of %y failed: %s"), &FilePath, ErrText);
        YoriLibFreeWinErrorText(ErrText);
        YoriLibFreeStringContents(&FilePath);
        return FALSE;
    }

    YoriLibFreeStringContents(&FilePath);

    YoriLibInitEmptyString(&LineString);

    while (TRUE) {

        if (!YoriLibReadLineToString(&LineString, &LineContext, FileHandle)) {
            break;
        }

        //
        //  If we fail to add to history, stop.  If it is added to history,
        //  that string is now owned by the history buffer, so reinitialize
        //  between lines.  The free below is really just a dereference.
        //

        if (!YoriShAddToHistory(&LineString)) {
            break;
        }

        YoriLibFreeStringContents(&LineString);
        YoriLibInitEmptyString(&LineString);
    }

    YoriLibLineReadClose(LineContext);
    YoriLibFreeStringContents(&LineString);
    CloseHandle(FileHandle);
    return TRUE;
}

/**
 Write the current command history buffer to a file, if the user has requested
 this behavior by configuring the YORIHISTFILE environment variable.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriShSaveHistoryToFile()
{
    DWORD FileNameLength;
    YORI_STRING UserHistFileName;
    YORI_STRING FilePath;
    HANDLE FileHandle;
    PYORI_LIST_ENTRY ListEntry;
    PYORI_HISTORY_ENTRY HistoryEntry;
    
    FileNameLength = YoriShGetEnvironmentVariable(_T("YORIHISTFILE"), NULL, 0);
    if (FileNameLength == 0) {
        return TRUE;
    }

    if (!YoriLibAllocateString(&UserHistFileName, FileNameLength)) {
        return FALSE;
    }

    UserHistFileName.LengthInChars = YoriShGetEnvironmentVariable(_T("YORIHISTFILE"), UserHistFileName.StartOfString, UserHistFileName.LengthAllocated);

    if (UserHistFileName.LengthInChars == 0 || UserHistFileName.LengthInChars >= UserHistFileName.LengthAllocated) {
        YoriLibFreeStringContents(&UserHistFileName);
        return FALSE;
    }

    if (!YoriLibUserStringToSingleFilePath(&UserHistFileName, TRUE, &FilePath)) {
        YoriLibFreeStringContents(&UserHistFileName);
        return FALSE;
    }

    YoriLibFreeStringContents(&UserHistFileName);

    FileHandle = CreateFile(FilePath.StartOfString,
                            GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);

    if (FileHandle == NULL || FileHandle == INVALID_HANDLE_VALUE) {
        DWORD LastError = GetLastError();
        LPTSTR ErrText = YoriLibGetWinErrorText(LastError);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("yori: open of %y failed: %s"), &FilePath, ErrText);
        YoriLibFreeWinErrorText(ErrText);
        YoriLibFreeStringContents(&FilePath);
        return FALSE;
    }

    YoriLibFreeStringContents(&FilePath);

    //
    //  Search the list of history.
    //

    ListEntry = YoriLibGetNextListEntry(&YoriShCommandHistory, NULL);
    while (ListEntry != NULL) {
        HistoryEntry = CONTAINING_RECORD(ListEntry, YORI_HISTORY_ENTRY, ListEntry);

        YoriLibOutputToDevice(FileHandle, 0, _T("%y\n"), &HistoryEntry->CmdLine);

        ListEntry = YoriLibGetNextListEntry(&YoriShCommandHistory, ListEntry);
    }

    CloseHandle(FileHandle);
    return TRUE;
}

// vim:sw=4:ts=4:et:

