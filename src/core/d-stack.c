//
// Rebol 3 Language Interpreter and Run-time Environment
// "Ren-C" branch @ https://github.com/metaeducation/ren-c
//
//=////////////////////////////////////////////////////////////////////////=//
//
//  Summary: Debug Stack Reflection and Querying
//  File: %d-stack.h
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2016 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
//=////////////////////////////////////////////////////////////////////////=//
//
// This file contains interactive debugging support for examining and
// interacting with the stack.
//
// !!! Interactive debugging is a work in progress, and comments are in the
// functions below.
//

#include "sys-core.h"


//
//  Collapsify_Array: C
//
// This will replace "long" nested blocks with collapsed versions with
// ellipses to show they have been cut off.  It does not change the arrays
// in question, but replaces them with copies.
//
void Collapsify_Array(REBARR *array, REBCNT limit)
{
    REBVAL *item = ARR_HEAD(array);
    for (; NOT_END(item); ++item) {
        if (ANY_ARRAY(item) && VAL_LEN_AT(item) > limit) {
            REBARR *copy = Copy_Array_At_Max_Shallow(
                VAL_ARRAY(item),
                VAL_INDEX(item),
                limit + 1
            );

            Val_Init_Word(ARR_AT(copy, limit), REB_WORD, SYM_ELLIPSIS);

            Collapsify_Array(
                copy,
                limit
            );

            Val_Init_Array_Index(item, VAL_TYPE(item), copy, 0); // at 0 now
            assert(IS_SPECIFIC(item));
            assert(!GET_VAL_FLAG(item, VALUE_FLAG_LINE)); // should be cleared
        }
    }
}


//
//  Make_Where_For_Frame: C
//
// Each call frame maintains the array it is executing in, the current index
// in that array, and the index of where the current expression started.
// This can be deduced into a segment of code to display in the debug views
// to indicate roughly "what's running" at that stack level.
//
// Unfortunately, Rebol doesn't formalize this very well.  There is no lock
// on segments of blocks during their evaluation, and it's possible for
// self-modifying code to scramble the blocks being executed.  The DO
// evaluator is robust in terms of not *crashing*, but the semantics may well
// suprise users.
//
// !!! Should blocks on the stack be locked from modification, at least by
// default unless a special setting for self-modifying code unlocks it?
//
// So long as WHERE information is unreliable, this has to check that
// `expr_index` (where the evaluation started) and `index` (where the
// evaluation thinks it currently is) aren't out of bounds here.  We could
// be giving back positions now unrelated to the call...but shouldn't crash!
//
REBARR *Make_Where_For_Frame(struct Reb_Frame *frame)
{
    REBCNT start;
    REBCNT end;

    REBARR *where;
    REBOOL pending;

    if (FRM_IS_VALIST(frame)) {
        const REBOOL truncated = TRUE;
        Reify_Va_To_Array_In_Frame(frame, truncated);
    }

    // WARNING: MIN is a C macro and repeats its arguments.
    //
    start = MIN(ARR_LEN(FRM_ARRAY(frame)), cast(REBCNT, frame->expr_index));
    end = MIN(ARR_LEN(FRM_ARRAY(frame)), FRM_INDEX(frame));

    assert(end >= start);
    assert(frame->mode != CALL_MODE_GUARD_ARRAY_ONLY);
    pending = NOT(frame->mode == CALL_MODE_FUNCTION);

    // Do a shallow copy so that the WHERE information only includes
    // the range of the array being executed up to the point of
    // currently relevant evaluation, not all the way to the tail
    // of the block (where future potential evaluation would be)
    {
        REBCNT n = 0;

        REBCNT len =
            1 // fake function word (compensates for prefetch)
            + (end - start) // data from expr_index to the current index
            + (pending ? 1 : 0); // if it's pending we put "..." to show that

        where = Make_Array(len);

        // !!! Due to "prefetch" the expr_index will be *past* the invocation
        // of the function.  So this is a lie, as a placeholder for what a
        // real debug mode would need to actually save the data to show.
        // If the execution were a path or anything other than a word, this
        // will lose it.
        //
        Val_Init_Word(ARR_AT(where, n), REB_WORD, FRM_LABEL(frame));
        ++n;

        for (n = 1; n < len; ++n)
            *ARR_AT(where, n) = *ARR_AT(FRM_ARRAY(frame), start + n - 1);

        SET_ARRAY_LEN(where, len);
        TERM_ARRAY(where);

        Collapsify_Array(where, 3);
    }

    // Making a shallow copy offers another advantage, that it's
    // possible to get rid of the newline marker on the first element,
    // that would visually disrupt the backtrace for no reason.
    //
    if (end - start > 0)
        CLEAR_VAL_FLAG(ARR_HEAD(where), VALUE_FLAG_LINE);

    // We add an ellipsis to a pending frame to make it a little bit
    // clearer what is going on.  If someone sees a where that looks
    // like just `* [print]` the asterisk alone doesn't quite send
    // home the message that print is not running and it is
    // argument fulfillment that is why it's not "on the stack"
    // yet, so `* [print ...]` is an attempt to say that better.
    //
    // !!! This is in-band, which can be mixed up with literal usage
    // of ellipsis.  Could there be a better "out-of-band" conveyance?
    // Might the system use colorization in a value option bit.
    //
    if (pending)
        Val_Init_Word(Alloc_Tail_Array(where), REB_WORD, SYM_ELLIPSIS);

    return where;
}


//
//  where-of: native [
//
//  "Get execution point summary for a function call (if still on stack)"
//
//      level [frame! function! integer! blank!]
//  ]
//
REBNATIVE(where_of)
//
// !!! This routine should probably be used to get the information for the
// where of an error, which should likely be out-of-band.
{
    PARAM(1, level);

    struct Reb_Frame *frame = Frame_For_Stack_Level(NULL, ARG(level), TRUE);
    if (frame == NULL)
        fail (Error_Invalid_Arg(ARG(level)));

    Val_Init_Block(D_OUT, Make_Where_For_Frame(frame));
    return R_OUT;
}


//
//  label-of: native [
//
//  "Get word label used to invoke a function call (if still on stack)"
//
//      level [frame! function! integer!]
//  ]
//
REBNATIVE(label_of)
{
    PARAM(1, level);

    struct Reb_Frame *frame = Frame_For_Stack_Level(NULL, ARG(level), TRUE);

    // Make it slightly easier by returning a NONE! instead of giving an
    // error for a frame that isn't on the stack.
    //
    // !!! Should a function that was invoked by something other than a WORD!
    // return something like TRUE instead of a fake symbol?
    //
    if (frame == NULL)
        return R_BLANK;

    Val_Init_Word(D_OUT, REB_WORD, FRM_LABEL(frame));
    return R_OUT;
}


//
//  function-of: native [
//
//  "Get the ANY-FUNCTION! for a stack level or frame"
//
//      level [frame! integer!]
//  ]
//
REBNATIVE(function_of)
{
    PARAM(1, level);

    REBVAL *level = ARG(level);

    if (IS_FRAME(level)) {
        //
        // If a FRAME!, then the keylist *should* be the function params,
        // which should be coercible to a function even when the call is
        // no longer on the stack.
        //
        REBCTX *context = VAL_CONTEXT(level);
        *D_OUT = *FUNC_VALUE(CTX_FRAME_FUNC(context));
    }
    else {
        struct Reb_Frame *frame = Frame_For_Stack_Level(NULL, level, TRUE);
        if (!frame)
            fail (Error_Invalid_Arg(level));

        *D_OUT = *FUNC_VALUE(frame->func);
    }

    return R_OUT;
}


//
//  backtrace-index: native [
//
//  "Get the index of a given frame or function as BACKTRACE shows it"
//
//      level [function! frame!]
//          {The function or frame to get an index for (NONE! if not running)}
//  ]
//
REBNATIVE(backtrace_index)
{
    PARAM(1, level);

    REBCNT number;

    if (NULL != Frame_For_Stack_Level(&number, ARG(level), TRUE)) {
        SET_INTEGER(D_OUT, number);
        return R_OUT;
    }

    return R_BLANK;
}


//
//  backtrace: native [
//
//  "Backtrace to find a specific FRAME!, or other queried property."
//
//      level [| blank! integer! function! |]
//          "Stack level to return frame for (blank to list)"
//      /limit
//          "Limit the length of the backtrace"
//      frames [blank! integer!]
//          "Max number of frames (pending and active), blank for no limit"
//      /brief
//          "Do not list depths, just function labels on one line"
//      /only ;-- should this be /QUIET or similar?
//          "Return backtrace data without printing it to the console"
//  ]
//
REBNATIVE(backtrace)
{
    PARAM(1, level);
    REFINE(2, limit);
    PARAM(3, frames);
    REFINE(4, brief);
    REFINE(5, only);

    REBCNT number; // stack level number in the loop (no pending frames)

    REBCNT row; // row we're on (includes pending frames and maybe ellipsis)
    REBCNT max_rows; // The "frames" from /LIMIT, plus one (for ellipsis)

    REBCNT index; // backwards-counting index for slots in backtrace array

    REBOOL first = TRUE; // special check of first frame for "breakpoint 0"

    REBVAL *level = ARG(level); // void if at <end>

    // Note: Running this code path is *intentionally* redundant with
    // Frame_For_Stack_Level, as a way of keeping the numbers listed in a
    // backtrace lined up with what that routine returns.  This isn't a very
    // performance-critical routine, so it's good to have the doublecheck.
    //
    REBOOL get_frame = NOT(IS_VOID(level) || IS_BLANK(level));

    REBARR *backtrace;
    struct Reb_Frame *frame;

    Check_Security(SYM_DEBUG, POL_READ, 0);

    if (get_frame && (REF(limit) || REF(brief))) {
        //
        // /LIMIT assumes that you are returning a list of backtrace items,
        // while specifying a level gives one.  They are mutually exclusive.
        //
        fail (Error(RE_BAD_REFINES));
    }

    if (REF(limit)) {
        if (IS_BLANK(ARG(frames)))
            max_rows = MAX_U32; // NONE is no limit--as many frames as possible
        else {
            if (VAL_INT32(ARG(frames)) < 0)
                fail (Error_Invalid_Arg(ARG(frames)));
            max_rows = VAL_INT32(ARG(frames)) + 1; // + 1 for ellipsis
        }
    }
    else
        max_rows = 20; // On an 80x25 terminal leaves room to type afterward

    if (get_frame) {
        //
        // See notes on handling of breakpoint below for why 0 is accepted.
        //
        if (IS_INTEGER(level) && VAL_INT32(level) < 0)
            fail (Error_Invalid_Arg(level));
    }
    else {
        // We're going to build our backtrace in reverse.  This is done so
        // that the most recent stack frames are at the bottom, that way
        // they don't scroll off the top.  But this is a little harder to
        // get right, so get a count of how big it will be first.
        //
        // !!! This could also be done by over-allocating and then setting
        // the series bias, though that reaches beneath the series layer
        // and makes assumptions about the implementation.  And this isn't
        // *that* complicated, considering.
        //
        index = 0;
        row = 0;
        for (frame = FS_TOP->prior; frame != NULL; frame = FRM_PRIOR(frame)) {
            if (frame->mode == CALL_MODE_GUARD_ARRAY_ONLY) continue;

            // index and property, unless /BRIEF in which case it will just
            // be the property.
            //
            ++index;
            if (!REF(brief))
                ++index;

            ++row;

            if (row >= max_rows) {
                //
                // Past our depth, so this entry is an ellipsis.  Notice that
                // the base case of `/LIMIT 0` produces max_rows of 1, which
                // means you will get just an ellipsis row.
                //
                break;
            }
        }

        backtrace = Make_Array(index);
        SET_ARRAY_LEN(backtrace, index);
        TERM_ARRAY(backtrace);
    }

    row = 0;
    number = 0;
    for (frame = FS_TOP->prior; frame != NULL; frame = frame->prior) {
        REBCNT len;
        REBVAL *temp;
        REBOOL pending;

        // Only consider invoked or pending functions in the backtrace.
        //
        // !!! The pending functions aren't actually being "called" yet,
        // their frames are in a partial state of construction.  However it
        // gives a fuller picture to see them in the backtrace.  It may
        // be interesting to see GROUP! stack levels that are being
        // executed as well (as they are something like DO).
        //
        if (frame->mode == CALL_MODE_GUARD_ARRAY_ONLY)
            continue;

        if (frame->mode == CALL_MODE_FUNCTION) {
            pending = FALSE;

            if (
                first
                && IS_FUNCTION_AND(FUNC_VALUE(frame->func), FUNC_CLASS_NATIVE)
                && (
                    FUNC_CODE(frame->func) == &N_pause
                    || FUNC_CODE(frame->func) == &N_breakpoint
                )
            ) {
                // Omitting breakpoints from the list entirely presents a
                // skewed picture of what's going on.  But giving them
                // "index 1" means that inspecting the frame you're actually
                // interested in (the one where you put the breakpoint) bumps
                // to 2, which feels unnatural.
                //
                // Compromise by not incrementing the stack numbering for
                // this case, leaving a leading breakpoint frame at index 0.
            }
            else
                ++number;
        }
        else
            pending = TRUE;

        first = FALSE;

        ++row;

    #if !defined(NDEBUG)
        //
        // Try and keep the numbering in sync with query used by host to get
        // function frames to do binding in the REPL with.
        //
        if (!pending) {
            REBCNT temp_num;
            REBVAL temp_val;
            SET_INTEGER(&temp_val, number);

            if (
                Frame_For_Stack_Level(&temp_num, &temp_val, TRUE) != frame
                || temp_num != number
            ) {
                Debug_Fmt(
                    "%d != Frame_For_Stack_Level %d", number, temp_num
                );
                assert(FALSE);
            }
        }
    #endif

        if (get_frame) {
            if (IS_INTEGER(level)) {
                if (number != cast(REBCNT, VAL_INT32(level))) // is positive
                    continue;
            }
            else {
                assert(IS_FUNCTION(level));
                if (frame->func != VAL_FUNC(level))
                    continue;
            }
        }
        else {
            if (row >= max_rows) {
                //
                // If there's more stack levels to be shown than we were asked
                // to show, then put an `+ ...` in the list and break.
                //
                temp = ARR_AT(backtrace, --index);
                Val_Init_Word(temp, REB_WORD, SYM_PLUS);
                if (!REF(brief)) {
                    //
                    // In the non-/ONLY backtrace, the pairing of the ellipsis
                    // with a plus is used in order to keep the "record size"
                    // of the list at an even 2.  Asterisk might have been
                    // used but that is taken for "pending frames".
                    //
                    // !!! Review arbitrary symbolic choices.
                    //
                    temp = ARR_AT(backtrace, --index);
                    Val_Init_Word(temp, REB_WORD, SYM_ASTERISK);
                    SET_VAL_FLAG(temp, VALUE_FLAG_LINE); // put on own line
                }
                break;
            }
        }

        if (get_frame) {
            //
            // If we were fetching a single stack level, then our result will
            // be a FRAME! (which can be queried for further properties via
            // `where-of`, `label-of`, `function-of`, etc.)
            //
            Val_Init_Context(
                D_OUT,
                REB_FRAME,
                Context_For_Frame_May_Reify(frame, NULL, FALSE)
            );
            return R_OUT;
        }

        // The /ONLY case is bare bones and just gives a block of the label
        // symbols (at this point in time).
        //
        // !!! Should /BRIEF omit pending frames?  Should it have a less
        // "loaded" name for the refinement?
        //
        temp = ARR_AT(backtrace, --index);
        if (REF(brief)) {
            Val_Init_Word(temp, REB_WORD, FRM_LABEL(frame));
            continue;
        }

        Val_Init_Block(temp, Make_Where_For_Frame(frame));

        // If building a backtrace, we just keep accumulating results as long
        // as there are stack levels left and the limit hasn't been hit.

        // The integer identifying the stack level (used to refer to it
        // in other debugging commands).  Since we're going in reverse, we
        // add it after the props so it will show up before, and give it
        // the newline break marker.
        //
        temp = ARR_AT(backtrace, --index);
        if (pending) {
            //
            // You cannot (or should not) switch to inspect a pending frame,
            // as it is partially constructed.  It gets a "*" in the list
            // instead of a number.
            //
            // !!! This may be too restrictive; though it is true you can't
            // resume/from or exit/from a pending frame (due to the index
            // not knowing how many values it would have consumed if a
            // call were to complete), inspecting the existing args could
            // be okay.  Disallowing it offers more flexibility in the
            // dealings with the arguments, however (for instance: not having
            // to initialize not-yet-filled args could be one thing).
            //
            Val_Init_Word(temp, REB_WORD, SYM_ASTERISK);
        }
        else
            SET_INTEGER(temp, number);

        SET_VAL_FLAG(temp, VALUE_FLAG_LINE);
    }

    // If we ran out of stack levels before finding the single one requested
    // via /AT, return a NONE!
    //
    // !!! Would it be better to give an error?
    //
    if (get_frame)
        return R_BLANK;

    // Return accumulated backtrace otherwise.  The reverse filling process
    // should have exactly used up all the index slots, leaving index at 0.
    //
    assert(index == 0);
    Val_Init_Block(D_OUT, backtrace);
    if (REF(only))
        return R_OUT;

    // If they didn't use /ONLY we assume they want it printed out.
    //
    // TRUE = mold
    //
    Print_Value(D_OUT, 0, TRUE);
    return R_VOID;
}


//
//  Frame_For_Stack_Level: C
//
// Level can be a void, an INTEGER!, an ANY-FUNCTION!, or a FRAME!.  If
// level is void then it means give whatever the first call found is.
//
// Returns NULL if the given level number does not correspond to a running
// function on the stack.
//
// Can optionally give back the index number of the stack level (counting
// where the most recently pushed stack level is the lowest #)
//
// !!! Unfortunate repetition of logic inside of BACKTRACE.  Assertions
// are used to try and keep them in sync, by noticing during backtrace
// if the stack level numbers being handed out don't line up with what
// would be given back by this routine.  But it would be nice to find a way
// to unify the logic for omitting things like breakpoint frames, or either
// considering pending frames or not.
//
struct Reb_Frame *Frame_For_Stack_Level(
    REBCNT *number_out,
    const REBVAL *level,
    REBOOL skip_current
) {
    struct Reb_Frame *frame = FS_TOP;
    REBOOL first = TRUE;
    REBINT num = 0;

    if (IS_INTEGER(level)) {
        if (VAL_INT32(level) < 0) {
            //
            // !!! fail() here, or just return NULL?
            //
            return NULL;
        }
    }

    // We may need to skip some number of frames, if there have been stack
    // levels added since the numeric reference point that "level" was
    // supposed to refer to has changed.  For now that's only allowed to
    // be one level, because it's rather fuzzy which stack levels to
    // omit otherwise (pending? parens?)
    //
    if (skip_current)
        frame = frame->prior;

    for (; frame != NULL; frame = frame->prior) {
        if (frame->mode != CALL_MODE_FUNCTION) {
            //
            // Don't consider pending calls, or GROUP!, or any non-invoked
            // function as a candidate to target.
            //
            // !!! The inability to target a GROUP! by number is an artifact
            // of implementation, in that there's no hook in Do_Core() at
            // the point of group evaluation to process the return.  The
            // matter is different with a pending function call, because its
            // arguments are only partially processed--hence something
            // like a RESUME/AT or an EXIT/FROM would not know which array
            // index to pick up running from.
            //
            continue;
        }

        if (first) {
            if (
                IS_FUNCTION_AND(FUNC_VALUE(frame->func), FUNC_CLASS_NATIVE)
                && (
                    FUNC_CODE(frame->func) == &N_pause
                    || FUNC_CODE(frame->func) == N_breakpoint
                )
            ) {
                // this is considered the "0".  Return it only if 0 was requested
                // specifically (you don't "count down to it");
                //
                if (IS_INTEGER(level) && num == VAL_INT32(level))
                    goto return_maybe_set_number_out;
                else {
                    first = FALSE;
                    continue;
                }
            }
            else {
                ++num; // bump up from 0
            }
        }

        if (IS_INTEGER(level) && num == VAL_INT32(level))
            goto return_maybe_set_number_out;

        first = FALSE;

        if (frame->mode != CALL_MODE_FUNCTION) {
            //
            // Pending frames don't get numbered
            //
            continue;
        }

        if (IS_VOID(level) || IS_BLANK(level)) {
            //
            // Take first actual frame if void or blank
            //
            goto return_maybe_set_number_out;
        }
        else if (IS_INTEGER(level)) {
            ++num;
            if (num == VAL_INT32(level))
                goto return_maybe_set_number_out;
        }
        else if (IS_FRAME(level)) {
            if (
                (frame->flags & DO_FLAG_FRAME_CONTEXT)
                && frame->data.context == VAL_CONTEXT(level)
            ) {
                goto return_maybe_set_number_out;
            }
        }
        else {
            assert(IS_FUNCTION(level));
            if (VAL_FUNC(level) == frame->func)
                goto return_maybe_set_number_out;
        }
    }

    // Didn't find it...
    //
    return NULL;

return_maybe_set_number_out:
    if (number_out)
        *number_out = num;
    return frame;
}


//
//  running?: native [
//
//  "Returns TRUE if a FRAME! is on the stack and executing (arguments done)."
//
//      frame [frame!]
//  ]
//
REBNATIVE(running_q)
{
    PARAM(1, frame);

    REBCTX *frame_ctx = VAL_CONTEXT(ARG(frame));
    struct Reb_Frame *frame;

    if (GET_CTX_FLAG(frame_ctx, CONTEXT_FLAG_STACK))
        if (!GET_CTX_FLAG(frame_ctx, SERIES_FLAG_ACCESSIBLE))
            return R_FALSE;

    frame = CTX_FRAME(frame_ctx);

    if (frame->mode == CALL_MODE_FUNCTION)
        return R_TRUE;

    return R_FALSE;
}


//
//  pending?: native [
//
//  "Returns TRUE if a FRAME! is on the stack, but is gathering arguments."
//
//      frame [frame!]
//  ]
//
REBNATIVE(pending_q)
{
    PARAM(1, frame);

    REBCTX *frame_ctx = VAL_CONTEXT(ARG(frame));
    struct Reb_Frame *frame;

    if (GET_CTX_FLAG(frame_ctx, CONTEXT_FLAG_STACK))
        if (!GET_CTX_FLAG(frame_ctx, SERIES_FLAG_ACCESSIBLE))
            return R_FALSE;

    frame = CTX_FRAME(frame_ctx);

    if (
        frame->mode == CALL_MODE_ARGS
        || frame->mode == CALL_MODE_REFINEMENT_PICKUP
    ) {
        return R_TRUE;
    }

    return R_FALSE;
}
