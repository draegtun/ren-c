//
//  File: %mod-tcc.c
//  Summary: {Implementation of "user natives" using an embedded C compiler}
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016 Atronix Engineering
// Copyright 2016-2019 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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
// A user native is an ACTION! whose body is not a Rebol block, but a textual
// string of C code.  It is compiled on the fly by TCC, using the libtcc API.
//
// https://github.com/metaeducation/tcc/blob/mob/libtcc.h
// https://github.com/metaeducation/tcc/blob/mob/tests/libtcc_test.c
//
// See the TCC extension's README.md for an overview of the extension.
//
// This file implements MAKE-NATIVE and a "low level" compile primitive called
// COMPILE*.
//

#include "sys-core.h"
#include "sys-ext.h"

#include "tmp-mod-tcc.h"

#include "libtcc.h"

#if defined(NEEDS_FAKE_STRTOLD)
    //
    // strtold() was added in C99.  Some older Android NDKs don't have it, but
    // TCC depends upon it.  It defines it as an extern in %tcc.h with
    // different return type than Android NDK's strtod, hence you can't
    // do `-Dstrtold-strtod` without getting a conflicting definition warning.
    //
    // This proxy definition can get past the linker error, and keeps the
    // workaround isolated to the TCC extension.
    //
    long double strtold (const char *nptr, char **endptr) {
        return strtod(nptr, endptr);
    }
#endif

#if defined(TCC_RELOCATE_AUTO)
    #define tcc_relocate_auto(s) \
        tcc_relocate((s), TCC_RELOCATE_AUTO)
#else
    // The tcc_relocate() API had an incompatible change in September 2012.
    // It added a parameter to allow you to provide a custom memory buffer.
    //
    // https://repo.or.cz/tinycc.git/commitdiff/ca38792df17fc5c8d2bb6757c512101610420f1e
    //
    #define tcc_relocate_auto(s) \
        tcc_relocate(s)

    // Use the missing TCC_RELOCATE_AUTO as a signal that the libtcc also
    // probably doesn't have tcc_set_options(), added in February 2013:
    //
    // https://repo.or.cz/tinycc.git?a=commit;h=05108a3b0a8eff70739b253b8995999b1861f9f2
    //
    void tcc_set_options(TCCState *s, const char *str) {
        UNUSED(s);
        UNUSED(str);

        rebJumps ("fail ["
            "{You are using OPTIONS in your COMPILE configuration.  But this}"
            "{tcc extension was built with an older libtcc that was assumed}"
            "{to not have tcc_set_options() (it lacked TCC_RELOCATE_AUTO).}"
            "{You'll need to rebuild the tcc extension with a newer lib.}"
        "]", rebEND);
    }
#endif

typedef int (*TCC_CSTR_API)(TCCState *, const char *);
int tcc_set_options_i(TCCState *s, const char *str)
    { tcc_set_options(s, str); return 0; } // make into a TCC_CSTR_API
int tcc_set_lib_path_i(TCCState *s, const char *path)
    { tcc_set_lib_path(s, path); return 0; } // make into a TCC_CSTR_API


// Native actions all have common structure for fields up to IDX_NATIVE_MAX
// in their ACT_DETAILS().  This lets the system know what context to do
// binding into while the native is running--for instance.  However, the
// details array can be longer and store more information specific to the
// dispatcher being used, these fields are used by "user natives"

#define IDX_TCC_NATIVE_LINKNAME \
    IDX_NATIVE_MAX // generated if the native doesn't specify

#define IDX_TCC_NATIVE_STATE \
    IDX_TCC_NATIVE_LINKNAME + 1 // will be a BLANK! until COMPILE happens

#define IDX_TCC_NATIVE_MAX \
    (IDX_TCC_NATIVE_STATE + 1)


// COMPILE replaces &Pending_Native_Dispatcher that user natives start with,
// so the dispatcher alone can't be usd to detect them.  ACTION_FLAG_XXX are
// in too short of a supply to give them their own flag.  Other natives put
// their source in ACT_DETAILS [0] and their context in ACT_DETAILS [1], so
// for the moment just assume if the source is text it's a user native.
//
bool Is_User_Native(REBACT *act) {
    if (NOT_ACTION_FLAG(act, IS_NATIVE))
        return false;

    REBARR *details = ACT_DETAILS(act);
    assert(ARR_LEN(details) >= 2); // ACTION_FLAG_NATIVE needs source+context
    return IS_TEXT(ARR_AT(details, IDX_NATIVE_BODY));
}


// This is the function registered to receive error messages during the
// compile.  The current logic just returns one error, but if more than one
// is given they could be batched up.
//
static void Error_Reporting_Hook(
    void *opaque,
    const char *msg_utf8
){
    // When `tcc_set_error_func()` is called, you can pass it a value that
    // it will pass back.  We pass EMPTY_BLOCK to test it (and explain it).
    // Note that since the compilation can be delayed after MAKE-NATIVE exits,
    // pointers to local variables should not be used here.
    //
    assert(cast(REBVAL*, opaque) == EMPTY_BLOCK);
    UNUSED(opaque);

    rebJumps ("fail [",
        "{TCC errors/warnings, '-w' to stop warnings:}", rebT(msg_utf8),
    "]", rebEND);
}


// This calls a TCC API that takes a string on an optional Rebol TEXT! value
// found in the config.
//
// Note the COMPILE usermode front end standardizes FILE! paths into TEXT!
// with FILE-TO-LOCAL, so that on Windows they'll have backslashes, etc.
//
//
static void Process_Text_Helper_Core(
    TCC_CSTR_API some_tcc_api,
    TCCState *state,
    const REBVAL *text,
    const char *label
){
    assert(IS_TEXT(text));

    char* utf8 = rebSpell(text, rebEND);
    int status = some_tcc_api(state, utf8);
    rebFree(utf8);

    if (status < 0) // !!! When does it do this vs. call Error_Reporting_Hook?
        rebJumps ("fail [",
            "{TCC}", rebT(label), "{rejected:}", text,
        "]", rebEND);
}
static void Process_Text_Helper(
    TCC_CSTR_API some_tcc_api,
    TCCState *state,
    const REBVAL *config,
    const char *label
){
    REBVAL *text = rebValue(
        "opt ensure [blank! text!] select", config, "as word!", rebT(label),
    rebEND);

    if (text) {
        Process_Text_Helper_Core(some_tcc_api, state, text, label);
        rebRelease(text);
    }
}


// The COMPILE usermode front end standardizes settings into blocks, if they
// are able to take more than one item in the general case.
// Any FILE! elements are converted with FILE-TO-LOCAL, so that on Windows
// they'll have backslashes, etc.  Factoring this out reduces redundancy.
//
static void Process_Block_Helper(
    TCC_CSTR_API some_tcc_api,
    TCCState *state,
    const REBVAL *config,
    const char *label
){
    REBVAL *block = rebValue(
        "ensure block! select", config, "as word!", rebT(label),
    rebEND);

    RELVAL *text;
    for (text = VAL_ARRAY_AT(block); NOT_END(text); ++text)
        Process_Text_Helper_Core(some_tcc_api, state, KNOWN(text), label);

    rebRelease(block);
}


// libtcc breaks ISO C++ by passing function pointers as void*.  This helper
// uses memcpy to circumvent, assuming they're the same size.
//
static void Add_Function_Symbol_Helper(
    TCCState *state,
    const char *symbol,
    CFUNC *cfunc_ptr // see CFUNC for why func and data pointers differ
){
    void *void_ptr;
    assert(sizeof(void_ptr) == sizeof(cfunc_ptr));
    memcpy(&void_ptr, &cfunc_ptr, sizeof(cfunc_ptr));

    if (tcc_add_symbol(state, symbol, void_ptr) < 0)
        rebJumps ("fail [",
            "{tcc_add_symbol failed for}", rebT(symbol),
        "]", rebEND);
}


// When a batch of natives or code are compiled into memory, that memory has
// to stick around as long as you expect a user native to be able to execute.
// So the GC has to keep the generated code alive as long as pointers exist.
// This is tracked by having each user native hold a reference to the memory
// blob via a HANDLE!.  When the last reference to the last native goes away,
// the GC will run this handle cleanup function.
//
static void cleanup(const REBVAL *val)
{
    TCCState *state = VAL_HANDLE_POINTER(TCCState, val);
    assert(state != nullptr);
    tcc_delete(state);
}


//
//  Pending_Native_Dispatcher: C
//
// The MAKE-NATIVE command doesn't actually compile the function directly.
// Instead the source code is held onto, so that several user natives can
// be compiled together by COMPILE.
//
// However, as a convenience, calling a pending user native will trigger a
// simple COMPILE for just that one function, using default options.
//
REB_R Pending_Native_Dispatcher(REBFRM *f) {
    REBACT *phase = FRM_PHASE(f);
    assert(ACT_DISPATCHER(phase) == &Pending_Native_Dispatcher);

    REBVAL *action = ACT_ARCHETYPE(phase); // this action's value

    // !!! We're calling COMPILE here via a textual binding.  However, the
    // pending native dispatcher's IDX_NATIVE_CONTEXT for binding lookup is
    // what's in effect.  And that's set up to look up its bindings in where
    // the user native's body will be looking them up (this is defaulting to
    // user context for now).
    //
    // That means if COMPILE is not exported to the user context (or wherever
    // the IDX_NATIVE_CONTEXT is set), this will fail.  Hence the COMPILE
    // native's implementation needs to be factored out into a reusable C
    // function that gets called here.  -or- some better way of getting at the
    // known correct COMPILE Rebol function has to be done (NAT_VALUE() is
    // not in extensions yet, and may not be, so no NAT_VALUE(compile).)
    //
    rebElide("compile [", action, "]", rebEND);
    //
    // ^-- !!! Today's COMPILE doesn't return a result on success (just fails
    // on errors), but if it changes to return one consider what to do.

    // Now that it's compiled, it should have replaced the dispatcher with a
    // function pointer that lives in the TCC_State.  Use REDO, and don't
    // bother re-checking the argument types.
    //
    assert(ACT_DISPATCHER(phase) != &Pending_Native_Dispatcher);
    return R_REDO_UNCHECKED;
}


//
//  export make-native: native [
//
//  {Create an ACTION! which is compiled from a C source STRING!}
//
//      return: "Function value, will be compiled on demand or by COMPILE"
//          [action!]
//      spec "Rebol parameter definitions (similar to FUNCTION's spec)"
//          [block!]
//      source "C source of the native implementation"
//          [text!]
//      /linkname "Provide a specific linker name (default is auto-generated)"
//          [text!]
//  ]
//
REBNATIVE(make_native)
{
    TCC_INCLUDE_PARAMS_OF_MAKE_NATIVE;

    REBVAL *source = ARG(source);

    REBACT *native = Make_Action(
        Make_Paramlist_Managed_May_Fail(ARG(spec), MKF_MASK_NONE),
        &Pending_Native_Dispatcher, // will be replaced e.g. by COMPILE
        nullptr, // no facade (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        IDX_TCC_NATIVE_MAX // details len [source module linkname tcc_state]
    );

    REBARR *details = ACT_DETAILS(native);

    if (Is_Series_Frozen(VAL_SERIES(source)))
        Move_Value(ARR_AT(details, IDX_NATIVE_BODY), source); // no copy
    else {
        Init_Text(
            ARR_AT(details, IDX_NATIVE_BODY),
            Copy_String_At(source)  // might change before COMPILE call
        );
    }

    // !!! Natives on the stack can specify where APIs like rebValue() should
    // look for bindings.  For the moment, set user natives to use the user
    // context...it could be a parameter of some kind (?)
    //
    Move_Value(
        ARR_AT(details, IDX_NATIVE_CONTEXT),
        Get_System(SYS_CONTEXTS, CTX_USER)
    );

    if (REF(linkname)) {
        REBVAL *linkname = ARG(linkname);

        if (Is_Series_Frozen(VAL_SERIES(linkname)))
            Move_Value(ARR_AT(details, IDX_TCC_NATIVE_LINKNAME), linkname);
        else {
            Init_Text(
                ARR_AT(details, IDX_TCC_NATIVE_LINKNAME),
                Copy_String_At(linkname)
            );
        }
    }
    else {
        // Auto-generate a linker name based on the numeric value of the
        // paramlist pointer.  Just "N_" followed by the hexadecimal value.

        REBARR *paramlist = ACT_PARAMLIST(native);  // unique for this action!
        intptr_t heapaddr = cast(intptr_t, paramlist);
        REBVAL *linkname = rebValue(
            "unspaced [{N_} as text! to-hex", rebI(heapaddr), "]",
        rebEND);

        Move_Value(ARR_AT(details, IDX_TCC_NATIVE_LINKNAME), linkname);
        rebRelease(linkname);
    }

    Init_Blank(ARR_AT(details, IDX_TCC_NATIVE_STATE)); // no TCC_State, yet...

    SET_ACTION_FLAG(native, IS_NATIVE);
    return Init_Action_Unbound(D_OUT, native);
}


//
//  compile*: native [
//
//  {INTERNAL USE ONLY: Expects arguments to be fully vetted by COMPILE}
//
//      return: "No return value, unless /INSPECT is used to see result"
//          [<opt> text!]
//      compilables [block!] "Should be just TEXT! and user native ACTION!s"
//      config [object!] "Vetted and simplified form of /OPTIONS block"
//      /inspect "Return the C source code as text, but don't compile it"
//      /librebol "Connect symbols to running EXE's libRebol (rebValue(), etc.)"
//  ]
//
REBNATIVE(compile_p)
{
    TCC_INCLUDE_PARAMS_OF_COMPILE_P;

    REBVAL *compilables = ARG(compilables);
    REBVAL *config = ARG(config);

    // The TCC extension creates a new ACTION! type and dispatcher, so it has
    // to use the "internal" API.  Since it does, it can take advantage of
    // using the mold buffer.  The buffer is a "hot" memory region that is
    // generally preallocated, and makes it unnecessary to say in advance how
    // large the buffer needs to be.  It then can pass the pointer to TCC
    // and discard the data without ever making a TEXT! (as it would need to
    // if it were a client of the "external" libRebol API).
    //
    // !!! Uses UTF-8...look into how well TCC supports UTF-8.
    //
    DECLARE_MOLD (mo);
    Push_Mold(mo);

    REBDSP dsp_orig = DSP;

    RELVAL *item;
    for (item = VAL_ARRAY_AT(compilables); NOT_END(item); ++item) {
        if (IS_ACTION(item)) {
            assert(Is_User_Native(VAL_ACTION(item)));

            // Remember this function, because we're going to need to come
            // back and fill in its dispatcher and TCC_State after the
            // compilation...
            //
            Move_Value(DS_PUSH(), KNOWN(item));

            REBARR *details = VAL_ACT_DETAILS(item);
            RELVAL *source = ARR_AT(details, IDX_NATIVE_BODY);
            RELVAL *linkname = ARR_AT(details, IDX_TCC_NATIVE_LINKNAME);

            // !!! REBFRM is not exported by libRebol, though it could be
            // opaquely...and there could be some very narrow routines for
            // interacting with it (such as picking arguments directly by
            // value).  But transformations would be needed for Rebol arg
            // names to make valid C, as with to-c-name...and that's not
            // something to expose to the average user.  Hence rebArg() gives
            // a solution that's more robust, albeit slower than picking
            // by index:
            //
            // https://forum.rebol.info/t/817
            //
            Append_Ascii(mo->series, "const REBVAL *");
            Append_String(mo->series, linkname, VAL_LEN_AT(linkname));
            Append_Ascii(mo->series, "(void *frame_)\n{");

            Append_String(mo->series, source, VAL_LEN_AT(source));

            Append_Ascii(mo->series, "}\n\n");
        }
        else if (IS_TEXT(item)) {
            //
            // A string passed to COMPILE in the list of things-to-compile
            // is treated as just a fragment of code.  This allows for writing
            // arbitrary C functions that aren't themselves user natives, but
            // can be called by multiple user natives.  Or defining macros or
            // constants.  The string will appear at the point in the compile
            // where it is given in the list.
            //
            Append_String(mo->series, item, VAL_LEN_AT(item));
            Append_Ascii(mo->series, "\n");
        }
        else {
            // COMPILE should have vetted the list to only TEXT! and ACTION!
            //
            fail ("COMPILE's input array must contain TEXT! and ACTION!s");
        }
    }

    // To help in debugging, it can be useful to see what is compiling (this
    // is similar in spirit to the -E option for preprocessing only)
    //
    if (REF(inspect)) {
        DS_DROP_TO(dsp_orig); // don't modify the collected user natives
        return Init_Text(D_OUT, Pop_Molded_String(mo));
    }

    // == Mold buffer now contains the combined source ==

    // The state is where the code for the TCC_OUTPUT_MEMORY natives will be
    // living.  It must be kept alive for as long as you expect the user
    // natives to be able to execute, as this is where their ACT_DISPATCHER()
    // pointers are located.  The GCC manages it via handle (see cleanup())
    //
    TCCState *state = tcc_new();
    if (not state)
        fail ("TCC failed to create a TCC context");

    void* opaque = cast(void*, EMPTY_BLOCK); // can parameterize the error...
    tcc_set_error_func(state, opaque, &Error_Reporting_Hook);

    // Sets options (same syntax as the TCC command line, minus commands like
    // displaying the version or showing the TCC tool's help)
    //
    Process_Block_Helper(tcc_set_options_i, state, config, "options");

    // Add include paths (same as `-I` in the options)
    //
    Process_Block_Helper(tcc_add_include_path, state, config, "include-path");

    bool debug = rebDid("ensure logic! select", config, "'debug", rebEND);
    if (debug)
        fail ("DEBUG not currently supported by the TCC extension");

    // !!! In the future, it would be nice to have an option to output to
    // a file on disk, so the Rebol TCC compile could be used to make EXEs.
    //
    if (tcc_set_output_type(state, TCC_OUTPUT_MEMORY) < 0)
        fail ("TCC failed to set output to memory");

    if (
        tcc_compile_string(
            state,
            cs_cast(BIN_AT(SER(mo->series), mo->offset))
        ) < 0
    ){
        rebJumps ("fail [",
            "{TCC failed to compile the code}", compilables,
        "]", rebEND);
    }

    Drop_Mold(mo);  // discard the combined source (no longer needed)

    // It is technically possible for ELF binaries to "--export-dynamic" (or
    // -rdynamic in CMake) and make executables embed symbols for functions
    // in them "like a DLL".  However, we would like to make API symbols for
    // Rebol available to the dynamically loaded code on all platforms, so
    // this uses `tcc_add_symbol()` to work the same way on Windows/Linux/OSX
    //
    // !!! Not only is it technically possible to export symbols dynamically,
    // the build configuration for Rebol as a lib seems to force it, at least
    // on linux.  If you add a prototype like:
    //
    //    int Probe_Core_Debug(const REBVAL *v, char* file, int line);
    //
    // ...and then try calling it from your user native, it finds the internal
    // symbol.  Messing with -fvisibility="hidden" and other switches doesn't
    // seem to change this.  (If you define your own Probe_Core_Debug() in the
    // user native C file as a text blob in the compile, that overrides it.)
    //
    // On Windows it doesn't do this, but on the other hand it doesn't seem
    // *able* to do it.  It can only see tcc_add_symbol() exported symbols.
    //
    if (REF(librebol)) {
        /* #include "tmp-librebol-table.inc" */
        Add_Function_Symbol_Helper(state, "rebValue", cast(CFUNC*, &RL_rebValue));
    }

    // Add library paths (same as using `-L` in the options)
    //
    Process_Block_Helper(tcc_add_library_path, state, config, "library-path");

    // Add individual library files (same as using -l in the options, e.g.
    // the actual file is "libxxx.a" but you'd pass just `xxx` here)
    //
    // !!! Does this work for fully specified file paths as well?
    //
    Process_Block_Helper(tcc_add_library, state, config, "library");

    // Sets CONFIG_TCCDIR at runtime of the built code.
    //
    // !!! Since libtcc1.a is statically linked, it's not clear what benefit
    // this would have.  Is it so the built executable itself can use libtcc,
    // and find includes when it does compilation??
    //
    Process_Text_Helper(tcc_set_lib_path_i, state, config, "runtime-path");

    if (tcc_relocate_auto(state) < 0)
        fail ("TCC failed to relocate the code");

    DECLARE_LOCAL (handle);
    Init_Handle_Cdata_Managed(
        handle,
        state, // "data" pointer
        1,  // unused length (can't be 0, reserved for CFUNC)
        cleanup // called upon GC
    );

    // With compilation complete, find the matching linker names and get
    // their function pointers to substitute in for the dispatcher.
    //
    while (DSP != dsp_orig) {
        REBVAL *native = DS_TOP;
        assert(IS_ACTION(native) and Is_User_Native(VAL_ACTION(native)));

        REBARR *details = VAL_ACT_DETAILS(native);
        REBVAL *linkname = KNOWN(ARR_AT(details, IDX_TCC_NATIVE_LINKNAME));

        char *name_utf8 = rebSpell("ensure text!", linkname, rebEND);
        void *sym = tcc_get_symbol(state, name_utf8);
        rebFree(name_utf8);

        if (not sym)
            rebJumps ("fail [",
                "{TCC failed to find symbol:}", linkname,
            "]", rebEND);

        // Circumvent ISO C++ forbidding cast between function/data pointers
        //
        REBNAT c_func;
        assert(sizeof(c_func) == sizeof(void*));
        memcpy(&c_func, &sym, sizeof(c_func));

        ACT_DISPATCHER(VAL_ACTION(native)) = c_func;
        Move_Value(ARR_AT(details, IDX_TCC_NATIVE_STATE), handle);

        DS_DROP();
    }

    return nullptr;
}
