/**
 * @file lib/cmdline.c
 *
 * Converts argc/argv command lines back into strings
 *
 * Copyright (c) 2017 Malcolm J. Smith
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

#include "yoripch.h"
#include "yorilib.h"

/**
 Returns TRUE if the character should be treated as indicating a command
 line option.

 @param Char The character to check.

 @return TRUE if the character indicates a command line option, FALSE if
         if it does not.
 */
BOOL
YoriLibIsCommandLineOptionChar(
    __in TCHAR Char
    )
{
    if (Char == '/' || Char == '-') {
        return TRUE;
    }
    return FALSE;
}

/**
 Returns TRUE if the string commences with a character indicating a command
 line option, and returns the remainder of the string.

 @param String The string to check.

 @param Arg On successful completion, if the string is an option, this Arg
        contains the option string.

 @return TRUE if the character indicates a command line option, FALSE if
         if it does not.
 */
BOOL
YoriLibIsCommandLineOption(
    __in PYORI_STRING String,
    __out PYORI_STRING Arg
    )
{
    if (String->LengthInChars < 1) {
        return FALSE;
    }
    if (YoriLibIsCommandLineOptionChar(String->StartOfString[0])) {
        YoriLibInitEmptyString(Arg);
        Arg->StartOfString = &String->StartOfString[1];
        Arg->LengthInChars = String->LengthInChars - 1;
        return TRUE;
    }
    return FALSE;
}


/**
 Check if an argument contains spaces and now requires quoting.

 @param Arg The argument to check.

 @return TRUE if quoting is required, FALSE if not.
 */
BOOL
YoriLibCheckIfArgNeedsQuotes(
    __in PYORI_STRING Arg
    )
{
    BOOL HasWhiteSpace;
    DWORD i;

    HasWhiteSpace = FALSE;
    for (i = 0; i < Arg->LengthInChars; i++) {
        if (Arg->StartOfString[i] == ' ') {
            HasWhiteSpace = TRUE;
            break;
        }
    }

    if (HasWhiteSpace) {
        return TRUE;
    }

    return FALSE;
}

/**
 This routine creates a command line string from a series of argc/argv style
 arguments described with yori strings.  The caller is expected to free the
 result with @ref YoriLibDereference.

 @param ArgC The number of arguments in the argument array.

 @param ArgV An array of YORI_STRINGs constituting the argument array.

 @param EncloseInQuotes If the argument contains a space, enclose it in quotes.
        If FALSE, return purely space delimited arguments.

 @param CmdLine On successful completion, updated to point to a newly
        allocated string containing the entire command line.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibBuildCmdlineFromArgcArgv(
    __in DWORD ArgC,
    __in YORI_STRING ArgV[],
    __in BOOLEAN EncloseInQuotes,
    __out PYORI_STRING CmdLine
    )
{
    DWORD count;
    PYORI_STRING ThisArg;
    DWORD i;
    BOOL Quoted;

    YoriLibInitEmptyString(CmdLine);

    for (count = 0; count < ArgC; count++) {
        CmdLine->LengthAllocated += 3 + ArgV[count].LengthInChars;
    }

    CmdLine->LengthAllocated += 2;

    if (!YoriLibAllocateString(CmdLine, CmdLine->LengthAllocated)) {
        return FALSE;
    }

    for (count = 0; count < ArgC; count++) {
        ThisArg = &ArgV[count];

        if (count != 0) {
            CmdLine->StartOfString[CmdLine->LengthInChars] = ' ';
            CmdLine->LengthInChars++;
        }

        if (EncloseInQuotes) {
            Quoted = YoriLibCheckIfArgNeedsQuotes(ThisArg);

            if (Quoted) {
                CmdLine->StartOfString[CmdLine->LengthInChars] = '"';
                CmdLine->LengthInChars++;
            }
        } else {
            Quoted = FALSE;
        }

        for (i = 0; i < ThisArg->LengthInChars; i++) {
            CmdLine->StartOfString[CmdLine->LengthInChars + i] = ThisArg->StartOfString[i];
        }
        CmdLine->LengthInChars += i;

        if (EncloseInQuotes) {
            if (Quoted) {
                CmdLine->StartOfString[CmdLine->LengthInChars] = '"';
                CmdLine->LengthInChars++;
            }
        }
    }

    CmdLine->StartOfString[CmdLine->LengthInChars] = '\0';

    return TRUE;
}

/**
 Expand any $ delimited variables by processing the input string and calling
 a callback function for every variable found, allowing the callback to
 populate the output with the correct value.

 @param String The input string, which may contain variables to expand.

 @param MatchChar The character to use to delimit the variable being expanded.

 @param PreserveEscapes If TRUE, escape characters (^) are preserved in the
        output; if FALSE, they are removed from the output.

 @param Function The callback function to invoke when variables are found.

 @param Context A caller provided context to pass to the callback function.

 @param ExpandedString A string allocated by this function containing the
        expanded result.  The caller should free this when it is no longer
        needed with @ref YoriLibFree .

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibExpandCommandVariables(
    __in PYORI_STRING String,
    __in TCHAR MatchChar,
    __in BOOLEAN PreserveEscapes,
    __in PYORILIB_VARIABLE_EXPAND_FN Function,
    __in PVOID Context,
    __inout PYORI_STRING ExpandedString
    )
{
    DWORD DestIndex;
    DWORD Index;
    DWORD FinalIndex;
    BOOL Processed;
    YORI_STRING CmdString;
    YORI_STRING DestString;
    DWORD LengthNeeded;
    DWORD IgnoreUntil;

    if (ExpandedString->LengthAllocated < 256) {
        YoriLibFreeStringContents(ExpandedString);
        if (!YoriLibAllocateString(ExpandedString, 256)) {
            return FALSE;
        }
    }
    DestIndex = 0;
    IgnoreUntil = 0;

    for (Index = 0; Index < String->LengthInChars; Index++) {
        Processed = FALSE;

        if (Index >= IgnoreUntil && YoriLibIsEscapeChar(String->StartOfString[Index])) {
            IgnoreUntil = Index + 2;
            if (!PreserveEscapes) {
                continue;
            }
        }

        if (Index >= IgnoreUntil && String->StartOfString[Index] == MatchChar) {
            FinalIndex = Index + 1;
            while (FinalIndex < String->LengthInChars && String->StartOfString[FinalIndex] != MatchChar) {
                FinalIndex++;
            }

            YoriLibInitEmptyString(&CmdString);
            CmdString.StartOfString = &String->StartOfString[Index + 1];
            CmdString.LengthAllocated = 
            CmdString.LengthInChars = FinalIndex - Index - 1;

            while (TRUE) {
                YoriLibInitEmptyString(&DestString);
                DestString.StartOfString = &ExpandedString->StartOfString[DestIndex];
                DestString.LengthAllocated = ExpandedString->LengthAllocated - DestIndex - 1;

                LengthNeeded = Function(&DestString, &CmdString, Context);

                if (LengthNeeded <= (ExpandedString->LengthAllocated - DestIndex - 1)) {
                    Processed = TRUE;
                    DestIndex += LengthNeeded;
                    Index = FinalIndex;
                    break;
                } else {
                    ExpandedString->LengthInChars = DestIndex;
                    if (!YoriLibReallocateString(ExpandedString, ExpandedString->LengthAllocated * 4)) {
                        YoriLibFreeStringContents(ExpandedString);
                        return FALSE;
                    }
                }
            }
        }

        if (!Processed) {
            ExpandedString->StartOfString[DestIndex] = String->StartOfString[Index];
            DestIndex++;
        }

        if (DestIndex + 1 >= ExpandedString->LengthAllocated) {
            ExpandedString->LengthInChars = DestIndex;
            if (!YoriLibReallocateString(ExpandedString, ExpandedString->LengthAllocated * 4)) {
                YoriLibFreeStringContents(ExpandedString);
                return FALSE;
            }
        }
    }

    ExpandedString->LengthInChars = DestIndex;
    ExpandedString->StartOfString[DestIndex] = '\0';

    return TRUE;
}

// vim:sw=4:ts=4:et:
