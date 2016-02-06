REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Make primary boot files"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "Carl Sassenrath"
    Version: 2.100.0
    Needs: 2.100.100
    Purpose: {
        A lot of the REBOL system is built by REBOL, and this program
        does most of the serious work. It generates most of the C include
        files required to compile REBOL.
    }
]

print "--- Make Boot : System Embedded Script ---"

do %common.r

do %form-header.r

do %systems.r
config: config-system/guess system/options/args

write-if: func [file data] [
    if data <> attempt [read file][
        print ["UPDATE:" file]
        write file data
    ]
]

;-- SETUP --------------------------------------------------------------

change-dir %../boot/
;dir: %../core/temp/  ; temporary definition
inc: %../include/
src: %../core/

version: load %version.r
version/4: config/id/2
version/5: config/id/3

;-- Title string put into boot.h file checksum:
Title:
{REBOL
Copyright 2012 REBOL Technologies
REBOL is a trademark of REBOL Technologies
Licensed under the Apache License, Version 2.0
}

sections: [
    boot-types
    boot-words
    boot-root
    boot-task
    boot-strings
    boot-booters
    boot-actions
    boot-natives
    boot-ops
    boot-typespecs
    boot-errors
    boot-sysobj
    boot-base
    boot-sys
    boot-mezz
    boot-protocols
;   boot-script
]

include-protocols: false      ; include protocols in build

; Args passed: platform, product
;
; !!! Heed /script/args so you could say e.g. `do/args %make-boot.r [0.3.01]`
; Note however that current leaning is that scripts called by the invoked
; process will not have access to the "outer" args, hence there will be only
; one "args" to be looked at in the long run.  This is an attempt to still
; be able to bootstrap under the conditions of the A111 rebol.com R3-Alpha
; as well as function either from the command line or the REPL.
;
unless args: any [
    if string? :system/script/args [
        either block? load system/script/args [
            load system/script/args
        ][
            reduce [load system/script/args]
        ]
    ]
    :system/script/args

    ; This is the only piece that should be necessary if not dealing w/legacy
    system/options/args
] [
    fail "No platform specified."
]

if args/1 = ">" [args: ["Win32" "VIEW-PRO"]] ; for debugging only
product: to-word any [args/2  "core"]

platform-data: context [type: 'windows]
build: context [features: [help-strings]]

;-- Fetch platform specifications:
;init-build-objects/platform platform
;platform-data: platforms/:platform
;build: platform-data/builds/:product

;-- UTILITIES ----------------------------------------------------------

up-word: func [w] [
    w: uppercase form w
    for-each [f t] [
        #"-" #"_"
    ][replace/all w f t]
    w
]

;-- Emit Function
out: make string! 100000
emit: func [data] [repend out data]

emit-enum: func [word] [emit [tab to-c-name word "," newline]]

emit-line: func [prefix word cmt /var /define /code /decl /up1 /local str][

    str: to-c-name word

    if word = 0 [prefix: ""]

    if not any [code decl] [
        either var [uppercase/part str 1] [uppercase str]
    ]

    if up1 [uppercase/part str 1]

    str: case [
        define [rejoin [prefix str]]
        code   [rejoin ["    " prefix str cmt]]
        decl   [rejoin [prefix str cmt]]
        true   [rejoin ["    " prefix str ","]]
    ]
    if any [code decl] [cmt: none]
    if cmt [
        append str space
        case [
            define [repend str cmt]
            cmt [repend str ["// " cmt]]
        ]
    ]
    append str newline
    append out str
]

emit-head: func [title [string!] file [file!]] [
    clear out
    emit form-header/gen title file %make-boot.r
]

emit-end: func [/easy] [
    if not easy [remove find/last out #","]
    append out {^};^/}
]

remove-tests: func [d] [
    while [d: find d #test][remove/part d 2]
]

;----------------------------------------------------------------------------
;
; Evaltypes.h - Evaluation Dispatch Maps
;
;----------------------------------------------------------------------------

boot-types: load %types.r


emit-head "Evaluation Maps" %evaltypes.h


emit {

/***********************************************************************
**
*/  const REBACT Value_Dispatch[REB_MAX_0] =
/*
**      The ACTION dispatch function for each datatype.
**
***********************************************************************/
^{
}

for-each-record-NO-RETURN type boot-types [
    if group? type/class [type/class: first type/class]

    emit-line/var "T_" type/class type/name
]
emit-end


emit {

//
// ET_XXX: "Eval Type"
//
// The REB_XXX types are not sequential, but skip by 4 in order to have the
// low 2 bits clear on all values in the enumeration.  This means faster
// extraction and comparison without needing to bit-shift, but it also
// means that a `switch` statement can't be optimized into a jump table--
// which generally requires contiguous values:
//
// http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables
//
// By having a table that can quickly convert an `enum Reb_Kind` into a
// small integer suitable for a switch statement in the evaluator, the
// optimization can be leveraged.  The special value of "0" is picked for
// no evaluation behavior, so the table can have a second use as the quick
// implementation behind the ANY_EVAL macro.  All non-zero values then can
// mean "has some behavior in the evaluator".
//
enum ^{
    ET_NONE = 0,
}

for-each-record-NO-RETURN type boot-types [
    if group? type/class [
        emit-line "ET_" type/name ""
    ]
]
emit-end


emit {

/***********************************************************************
**
*/  const REBUPT Eval_Table[REB_MAX] =
/*
** This table is used to bypass a Do_Core evaluation for certain types.  So
** if you have `foo [x] [y]`, the DO_NEXT_MAY_THROW macro checks the table
** and realizes that both [x] and [y] are blocks and have no evaluator
** behavior, so it set the output value to [x] without calling Do_Core.
**
** There is a catch, because infix operators suggest the need to use the
** dispatch for cases like `foo [x] + [y]`.  So the only way to do the
** optimization is if *both* the next value and the one after it is inert.
**
** Vague empirical tests of release builds show the gains are somewhere
** around 6%-ish on real code, which is enough to be worth doing.  It does
** make debugging a little complicated, so it is disabled in the debug
** build...but only on Linux so that any debug builds run on other platforms
** can help raise alerts on the problem.
**
** Though there are only 64 basic "kinds" of types, this spaces them out in
** an array of 256 to avoid needing to do shifting on the type bits.  The
** REB_XXX types are actually offset by 2 bits due to the special usage of
** low bits in the header, and to avoid needing to shift on every test or
** use of a type they are left as 00 which makes the type constants larger
** but speeds comparisons and usage.
**
***********************************************************************/
^{
}

for-each-record-NO-RETURN type boot-types [
    either group? type/class [
        emit-line "" rejoin ["ET_" uppercase to-string type/name] ""
    ][
        emit-line "" "ET_NONE" ""
    ]
    loop 3 [emit-line "" "0xAE" "available"]
]
emit-end


emit {



extern const REBPEF Path_Dispatch[REB_MAX_0];

/***********************************************************************
**
*/  const REBPEF Path_Dispatch[REB_MAX_0] =
/*
**      The path evaluator function for each datatype.
**
***********************************************************************/
^{
}

for-each-record-NO-RETURN type boot-types [
    if group? type/class [type/class: first type/class]

    emit-line/var "PD_" switch/default type/path [
        * [type/class]
        - [0]
    ][type/path] type/name
]
emit-end

write inc/tmp-evaltypes.inc out


;----------------------------------------------------------------------------
;
; Maketypes.h - Dispatchers for Make (used by construct)
;
;----------------------------------------------------------------------------

emit-head "Datatype Makers" %maketypes.h
emit newline

types-used: []

for-each-record-NO-RETURN type boot-types [
    if group? type/class [type/class: first type/class]

    if all [
        type/make = '*
        word? type/class
        not find types-used type/class
    ][
        ; using -Wredundant-decls it seems these prototypes are already
        ; taken care of by make-headers.r, no need to re-emit
        comment [
            emit-line/up1/decl
                "extern REBOOL MT_" type/class "(REBVAL *, REBVAL *, REBCNT);"
        ]
        append types-used type/class
    ]
]

emit {

/***********************************************************************
**
*/  const MAKE_FUNC Make_Dispatch[REB_MAX_0] =
/*
**      Specifies the make method used for each datatype.
**
***********************************************************************/
^{
}

for-each-record-NO-RETURN type boot-types [
    if group? type/class [type/class: first type/class]

    either type/make = '* [
        emit-line/var "MT_" type/class type/name
    ][
        emit-line "" "0" type/name
    ]
]

emit-end

write inc/tmp-maketypes.h out

;----------------------------------------------------------------------------
;
; Comptypes.h - compare functions
;
;----------------------------------------------------------------------------

emit-head "Datatype Comparison Functions" %comptypes.h
emit newline

types-used: []

for-each-record-NO-RETURN type boot-types [
    if group? type/class [type/class: first type/class]

    if all [
        word? type/class
        not find types-used type/class
    ][
        ; using -Wredundant-decls it seems these prototypes are already
        ; taken care of by make-headers.r, no need to re-emit
        comment [
            emit-line/up1/decl
                "extern REBINT CT_" type/class "(REBVAL *, REBVAL *, REBINT);"
        ]
        append types-used type/class
    ]
]

emit {
/***********************************************************************
**
*/  const REBCTF Compare_Types[REB_MAX_0] =
/*
**      Type comparision functions.
**
***********************************************************************/
^{
}

for-each-record-NO-RETURN type boot-types [
    if group? type/class [type/class: first type/class]

    emit-line/var "CT_" type/class type/name
]
emit-end

write inc/tmp-comptypes.h out


;----------------------------------------------------------------------------
;
; Moldtypes.h - Dispatchers for Mold and Form
;
;----------------------------------------------------------------------------

;emit-head "Mold Dispatchers"
;
;emit {
;/***********************************************************************
;**
;*/ const MOLD_FUNC Mold_Dispatch[REB_MAX_0] =
;/*
;**     The MOLD dispatch function for each datatype.
;**
;***********************************************************************/
;^{
;}
;
;for-each-record-NO-RETURN type boot-types [
;   if group? type/class [type/class: first type/class]
;
;   f: "Mold_"
;   switch/default type/mold [
;       * [t: type/class]
;       + [t: type/name]
;       - [t: 0]
;   ][t: uppercase/part form type/mold 1]
;   emit [tab "case " uppercase join "REB_" type/name ":" tab "\\" t]
;   emit newline
;   ;emit-line/var f t type/name
;]
;emit-end
;
;emit {
;/***********************************************************************
;**
;*/ const MOLD_FUNC Form_Dispatch[REB_MAX_0] =
;/*
;**     The FORM dispatch function for each datatype.
;**
;***********************************************************************/
;^{
;}
;for-each-record-NO-RETURN type boot-types [
;   if group? type/class [type/class: first type/class]
;   f: "Mold_"
;   switch/default type/form [
;       *  [t: type/class]
;       f* [t: type/class f: "Form_"]
;       +  [t: type/name]
;       f+ [t: type/name f: "Form_"]
;       -  [t: 0]
;   ][t: uppercase/part form type/mold 1]
;   emit [tab "case " uppercase join "REB_" type/name ":" tab "\\" t]
;   emit newline
;   ;emit-line/var f t type/name
;]
;emit-end
;
;write inc/tmp-moldtypes.h out

;----------------------------------------------------------------------------
;
; Bootdefs.h - Boot include file
;
;----------------------------------------------------------------------------

emit-head "Datatype Definitions" %reb-types.h

emit [
{
/***********************************************************************
**
*/  enum Reb_Kind
/*
**      Internal datatype numbers. These change. Do not export.
**
***********************************************************************/
^{
}
]

datatypes: []
n: 0
bits: does [n * 4]

for-each-record-NO-RETURN type boot-types [
    append datatypes type/name

    ; Shift in two lowest bits as 1, both must be true in the lowest byte of
    ; a value header for it to be a valid REBVAL (that isn't an end marker).
    ; The other 6 bits are the type.  So we shift
    ;
    emit-line "REB_" form reduce [type/name {=} bits] bits

    n: n + 1
]

emit-line "REB_" form reduce ["MAX" {=} bits] bits

emit {    REB_ENUM_NO_DANGLING_COMMA // placeholder so REB_MAX can end in comma
^};

// For zero based indexing into arrays without concern for the low bits
//
#define REB_MAX_0 (REB_MAX / 4)
}

emit {
/***********************************************************************
**
**  REBOL Type Check Macros
**
***********************************************************************/
}

new-types: []
n: 0
for-each-record-NO-RETURN type boot-types [
    append new-types to-word join type/name "!"

    if n = 0 [
        ; Do not check for VAL_TYPE() == REB_TRASH.  Trying to get the type of
        ; something that is trash should cause an assert...hence
        ; IS_TRASH_DEBUG() is a macro that does not go through VAL_TYPE()
        ;
        n: n + 1
        continue
    ]

    str: uppercase form type/name
    replace/all str #"-" #"_"
    def: join {#define IS_} [str "(v)" space]
    emit [def "LOGICAL(VAL_TYPE(v)==REB_" str ")" newline]

    n: n + 1
]

; These macros are not automatically generated, though perhaps some of them
; should be.  They are here because all the type concerns tie together...
; if they are renumbered then tests using > or < might start failing.
;
; !!! Consider ways of making this more robust.
;
emit {
#define IS_SET(v) \
    LOGICAL(VAL_TYPE(v) > REB_UNSET)

#define IS_SCALAR(v) \
    LOGICAL(VAL_TYPE(v) <= REB_DATE)

#define ANY_SERIES(v) \
    LOGICAL(VAL_TYPE(v) >= REB_BINARY && VAL_TYPE(v) <= REB_LIT_PATH)

#define ANY_STRING(v) \
    LOGICAL(VAL_TYPE(v) >= REB_STRING && VAL_TYPE(v) <= REB_TAG)

#define ANY_BINSTR(v) \
    LOGICAL(VAL_TYPE(v) >= REB_BINARY && VAL_TYPE(v) <= REB_TAG)

#define ANY_ARRAY(v) \
    LOGICAL(VAL_TYPE(v) >= REB_BLOCK && VAL_TYPE(v) <= REB_LIT_PATH)

#define ANY_WORD(v) \
    LOGICAL(VAL_TYPE(v) >= REB_WORD && VAL_TYPE(v) <= REB_ISSUE)

#define ANY_PATH(v) \
    LOGICAL(VAL_TYPE(v) >= REB_PATH && VAL_TYPE(v) <= REB_LIT_PATH)

#define ANY_EVAL_BLOCK(v) \
    LOGICAL(VAL_TYPE(v) >= REB_BLOCK  && VAL_TYPE(v) <= REB_GROUP)

#define ANY_CONTEXT(v) \
    LOGICAL(VAL_TYPE(v) >= REB_OBJECT && VAL_TYPE(v) <= REB_PORT)

// If the type has evaluator behavior (vs. just passing through).  So like
// WORD!, GROUP!, FUNCTION! (as opposed to BLOCK!, INTEGER!, OBJECT!).
// The types are not arranged in an order that makes a super fast test easy
// (though perhaps someday it could be tweaked so that all the evaluated types
// had a certain bit set?) hence use a small fixed table.
//
// Note that this table has 256 entries, of which only those corresponding
// to having the two lowest bits zero are set.  This is to avoid needing
// shifting to check if a value is evaluable.  The other storage could be
// used for properties of the type +1, +2, +3 ... at the cost of a bit of
// math but reusing the values.  Any integer property could be stored for
// the evaluables so long as non-evaluables are 0 in this list.
//
extern const REBUPT Eval_Table[REB_MAX];

#define ANY_EVAL(v) LOGICAL(Eval_Table[VAL_TYPE(v)])
}


emit {
/***********************************************************************
**
**  REBOL Typeset Defines
**
***********************************************************************/

#define TS_NOTHING \
    (FLAGIT_KIND(REB_UNSET) | FLAGIT_KIND(REB_NONE))

// ANY-SOMETHING! is the base "all bits" typeset that just does not include
// UNSET! or NONE!.  TRASH! is a purely internal type, but is removed anyway.
//
#define TS_SOMETHING \
    ((FLAGIT_KIND(REB_MAX) - 1) /* all typeset bits */ \
    - TS_NOTHING - FLAGIT_KIND(REB_TRASH))

// ANY-VALUE! is slightly more lenient in accepting NONE!, but still does not
// count UNSET! (this is distinct from R3-Alpha's ANY-TYPE!, which is steered
// clear from for reasons including that it looks a lot like ANY-DATATYPE!)
//
#define TS_VALUE (TS_SOMETHING | FLAGIT_KIND(REB_NONE))

}

typeset-sets: []

for-each-record-NO-RETURN type boot-types [
    for-each ts compose [(type/typesets)] [
        spot: any [
            select typeset-sets ts
            first back insert tail typeset-sets reduce [ts copy []]
        ]
        append spot type/name
    ]
]
remove/part typeset-sets 2 ; the - markers

for-each [ts types] typeset-sets [
    emit ["#define TS_" up-word ts " ("]
    for-each t types [
        emit ["FLAGIT_KIND(REB_" up-word t ")|"] ; last | removed
    ]
    append remove back tail out ")^/"
]

write-if inc/reb-types.h out

;----------------------------------------------------------------------------
;
; Extension Related Tables
;
;----------------------------------------------------------------------------

ext-types: load %types-ext.r
rxt-record: [type offset size]

; Generate type table with necessary gaps
rxt-types: []
n: 0
for-each :rxt-record ext-types [
    if integer? offset [
        insert/dup tail rxt-types 0 offset - n
        n: offset
    ]
    append rxt-types type
    n: n + 1
]


emit-head "Extension Types (Isolators)" %ext-types.h

emit [
{
enum REBOL_Ext_Types
^{
}
]
n: 0
for-each :rxt-record ext-types [
    either integer? offset [
        emit-line "RXT_" rejoin [type " = " offset] n
    ][
        emit-line "RXT_" type n
    ]
    n: n + 1
]
emit {    RXT_MAX
^};
}

write inc/ext-types.h out ; part of Host-Kit distro

emit-head "Extension Type Equates" %tmp-exttypes.h
emit {

#ifdef __cplusplus
extern "C" ^{
#endif

extern const REBRXT Reb_To_RXT[REB_MAX_0];

/***********************************************************************
**
*/  const REBRXT Reb_To_RXT[REB_MAX_0] =
/*
***********************************************************************/
^{
}

for-each-record-NO-RETURN type boot-types [
    either find ext-types type/name [
        emit-line "RXT_" type/name type/name
    ][
        emit-line "" 0 type/name
    ]
]
emit-end

emit {
/***********************************************************************
**
*/  const enum Reb_Kind RXT_To_Reb[RXT_MAX] =
/*
***********************************************************************/
^{
}

n: 0
for-each type rxt-types [
    either word? type [emit-line "REB_" type n][
        emit-line "" "REB_TRASH" n
    ]
    n: n + 1
]
emit-end

emit {
/***********************************************************************
**
*/  const REBCNT RXT_Eval_Class[RXT_MAX] =
/*
***********************************************************************/
^{
}

n: 0
for-each type rxt-types [
    either all [
        word? type
        rec: find ext-types type
    ][
        emit-line "RXX_" rec/3 rec/1
    ][
        emit-line "" 0 n
    ]
    n: n + 1
]
emit-end

emit {
#define RXT_ALLOWED_TYPES (}
for-each type next rxt-types [
    if word? type [
        emit replace join "((u64)" uppercase rejoin ["1<<(REB_" type ">>2)) \^/"] #"-" #"_"
        emit "|"
    ]
]
remove back tail out
emit ")^/"

emit {
#ifdef __cplusplus
^}
#endif
}
write inc/tmp-exttypes.h out


;----------------------------------------------------------------------------
;
; Bootdefs.h - Boot include file
;
;----------------------------------------------------------------------------

emit-head "Boot Definitions" %bootdefs.h

emit [
{
#define REBOL_VER } version/1 {
#define REBOL_REV } version/2 {
#define REBOL_UPD } version/3 {
#define REBOL_SYS } version/4 {
#define REBOL_VAR } version/5 {
}
]

;-- Generate Lower-Level String Table ----------------------------------------

emit {
/***********************************************************************
**
**  REBOL Boot Strings
**
**      These are special strings required during boot and other
**      operations. Putting them here hides them from exe hackers.
**      These are all string offsets within a single string.
**
***********************************************************************/
}

boot-strings: load %strings.r
boot-errors: load %errors.r ;-- used also to make %tmp-errnums.r below

code: ""
n: 0
for-each str boot-strings [
    either set-word? :str [
        emit-line/define "#define RS_" to word! str n
    ][
        n: n + 1
        append code str
        append code null
    ]
]

; Some errors occur before we have the boot process is far enough along to
; have the error messages loaded in as Rebol strings in the boot block's
; CAT_ERRORS structure.  Adding their strings to %errors.r and keeping them
; in sync with a "pre-boot" copy in %strings.r was a manual process.  This
; makes format strings from the STRING! or BLOCK! in %errors.r automatically.
;
for-each [cat msgs] boot-errors [
    unless cat = quote Internal: [continue]

    emit-line/define "#define RS_" 'ERROR n
    for-each [word val] skip msgs 4 [
        n: n + 1
        case [
            string? val [append code val]
            block? val [
                ; %strings.r strings use printf-like convention, %d tells
                ; FORM to treat the vararg REBVAL* as an integer.
                ;
                append code rejoin map-each item val [
                    either get-word? item [{ %v }] [item]
                ]
            ]
            true [fail {Non-STRING! non-BLOCK! as %errors.r value}]
        ]
        append code null
    ]
]

emit ["#define RS_MAX" tab n lf]
emit ["#define RS_SIZE" tab length out lf]
boot-strings: to-binary code

;-- Generate Canonical Words (must follow datatypes above!) ------------------

emit {
/***********************************************************************
**
*/  enum REBOL_Symbols
/*
**      REBOL static canonical words (symbols) used with the code.
**
***********************************************************************/
^{
    SYM_0 = 0,
}

n: 1
for-each-record-NO-RETURN type boot-types [
    emit-line "SYM_" join type/name "_type" n
    n: n + 1
]

boot-words: load %words.r

replace boot-words '*port-modes* load %modes.r

for-each word boot-words [
    emit-line "SYM_" word reform [n "-" word]
    n: n + 1
]
emit-end

;-- Generate Action Constants ------------------------------------------------

emit {
/***********************************************************************
**
*/  enum REBOL_Actions
/*
**      REBOL datatype action numbers.
**
**      Note the correspondence to action numbers that are less than REB_MAX
**      as the query for the datatype with that enum value.  (INTEGER? etc.)
**
***********************************************************************/
^{
}

boot-actions: load %tmp-actions.r
n: 2 ;-- actions start at 2, for the type checks, skipping TRASH? and END?
emit-line "A_" "0 = 0" "Unused (would be A_TRASH_Q)"
for-each word boot-actions [
    if set-word? :word [
        emit-line "A_" to word! :word n
        n: n + 1
    ]
]
emit [tab "A_MAX_ACTION" lf "};"]
emit {

#define IS_BINARY_ACT(a) ((a) <= A_XOR_T)
}
print [n "actions"]

write inc/tmp-bootdefs.h out

;----------------------------------------------------------------------------
;
; Sysobj.h - System Object Selectors
;
;----------------------------------------------------------------------------

emit-head "System Object" %sysobj.h
emit newline

at-value: func ['field] [next find boot-sysobj to-set-word field]

boot-sysobj: load %sysobj.r
change at-value version version
when: now
when: when - when/zone
when/zone: 0:00
change at-value build when
change at-value product to lit-word! product


plats: load %platforms.r

change/only at-value platform reduce [
    any [select plats version/4 "Unknown"]
    any [select third any [find/skip plats version/4 3 []] version/5 ""]
]

ob: context boot-sysobj

make-obj-defs: func [obj prefix depth /selfless /local f] [
    uppercase prefix
    emit ["enum " prefix "object {" newline]

    either selfless [
        ;
        ; Make sure *next* value starts at 1.  Keys/vars in contexts start
        ; at 1, and if there's no "userspace" self in the 1 slot, the first
        ; key has to be...so we make `SYS_CTX_0 = 0` (for instance)
        ;
        emit-line prefix "0 = 0" none
    ][
        ; The internal generator currently puts SELF at the start of new
        ; objects in key slot 1, by default.  Eventually MAKE OBJECT! will
        ; have nothing to do with adding SELF, and it will be entirely a
        ; by-product of generators.
        ;
        emit-line prefix "SELF = 1" none
    ]

    for-each field words-of obj [
        emit-line prefix field none
    ]
    emit [tab uppercase join prefix "MAX^/"]
    emit "};^/^/"

    if depth > 1 [
        for-each field words-of obj [
            f: join prefix [field #"_"]
            replace/all f "-" "_"
            all [
                field <> 'standard
                object? get in obj field
                make-obj-defs obj/:field f depth - 1
            ]
        ]
    ]
]

make-obj-defs ob "SYS_" 1
make-obj-defs ob/catalog "CAT_" 4
make-obj-defs ob/contexts "CTX_" 4
make-obj-defs ob/standard "STD_" 4
make-obj-defs ob/state "STATE_" 4
;make-obj-defs ob/network "NET_" 4
make-obj-defs ob/ports "PORTS_" 4
make-obj-defs ob/options "OPTIONS_" 4
;make-obj-defs ob/intrinsic "INTRINSIC_" 4
make-obj-defs ob/locale "LOCALE_" 4
make-obj-defs ob/view "VIEW_" 4

write inc/tmp-sysobj.h out

;----------------------------------------------------------------------------

emit-head "Dialects" %reb-dialect.h
emit {
enum REBOL_dialect_error {
    REB_DIALECT_END = 0,    // End of dialect block
    REB_DIALECT_MISSING,    // Requested dialect is missing or not valid
    REB_DIALECT_NO_CMD,     // Command needed before the arguments
    REB_DIALECT_BAD_SPEC,   // Dialect spec is not valid
    REB_DIALECT_BAD_ARG,    // The argument type does not match the dialect
    REB_DIALECT_EXTRA_ARG   // There are more args than the command needs
};

}
make-obj-defs ob/dialects "DIALECTS_" 4

emit {#define DIALECT_LIT_CMD 0x1000
}

write inc/reb-dialect.h out


;----------------------------------------------------------------------------
;
; Event Types
;
;----------------------------------------------------------------------------

emit-head "Event Types" %reb-evtypes.h
emit newline

emit ["enum event_types {" newline]
for-each field ob/view/event-types [
    emit-line "EVT_" field none
]
emit [tab "EVT_MAX^/"]
emit "};^/^/"

emit ["enum event_keys {" newline]
emit-line "EVK_" "NONE" none
for-each field ob/view/event-keys [
    emit-line "EVK_" field none
]
emit [tab "EVK_MAX^/"]
emit "};^/^/"

write inc/reb-evtypes.h out


;----------------------------------------------------------------------------
;
; Error Constants
;
;----------------------------------------------------------------------------

;-- Error Structure ----------------------------------------------------------

emit-head "Error Structure and Constants" %errnums.h

emit {
#ifdef VAL_TYPE
/***********************************************************************
**
*/  typedef struct REBOL_Error_Vars
/*
***********************************************************************/
^{
}
; Generate ERROR object and append it to bootdefs.h:
emit-line/code "REBVAL " 'self ";"
for-each word words-of ob/standard/error [
    if word = 'near [word: 'nearest] ; prevents C problem
    emit-line/code "REBVAL " word ";"
]
emit {^} ERROR_VARS;
#endif
}

emit {
/***********************************************************************
**
*/  enum REBOL_Errors
/*
***********************************************************************/
^{
}

id-list: make block! 200

for-each [category info] boot-errors [
    unless all [
        (quote code:) == info/1
        integer? info/2
        (quote type:) == info/3
        string? info/4
    ][
        fail ["%errors.r" category "not [code: INTEGER! type: STRING! ...]"]
    ]

    code: info/2

    new-section: true
    for-each [key val] skip info 4 [
        unless set-word? key [
            fail ["Non SET-WORD! key in %errors.r:" key]
        ]

        id: to-word key
        if find id-list id [
            fail ["DUPLICATE id in %errors.r:" id]
        ]

        append id-list id

        ; all-caps and dashes to underscores for C naming
        identifier: replace/all (uppercase to-string id) "-" "_"

        either new-section [
            emit-line "RE_" reform [identifier "=" code] reform [code mold val]
            new-section: false
        ][
            emit-line "RE_" identifier reform [code mold val]
        ]

        code: code + 1
    ]
    emit-line "RE_" join to word! category "_max" none
    emit newline
]

emit-end

emit {
#define RE_USER 1000 // Hardcoded, update in %make-boot.r

#define RE_INTERNAL_FIRST RE_MISC // GENERATED! update in %make-boot.r
#define RE_MAX RE_COMMAND_MAX // GENERATED! update in %make-boot.r
}

write inc/tmp-errnums.h out

;-------------------------------------------------------------------------

emit-head "Port Modes" %port-modes.h

data: load %modes.r

emit {
enum port_modes ^{
}

for-each word data [
    emit-enum word
]
emit-end

write inc/tmp-portmodes.h out

;----------------------------------------------------------------------------
;
; Load Boot Mezzanine Functions - Base, Sys, and Plus
;
;----------------------------------------------------------------------------

;-- Add other MEZZ functions:
mezz-files: load %../mezz/boot-files.r ; base lib, sys, mezz

;append boot-mezz+ none ?? why was this needed?

for-each section [boot-base boot-sys boot-mezz] [
    set section make block! 200
    for-each file first mezz-files [
        append get section load join %../mezz/ file
    ]
    remove-tests get section

    ;-- Expectation is that section does not return result; GROUP! makes unset
    append get section [()]

    mezz-files: next mezz-files
]

boot-protocols: make block! 20
for-each file first mezz-files [
    m: load/all join %../mezz/ file ; not REBOL word
    append/only append/only boot-protocols m/2 skip m 2
]

emit-head "Sys Context" %sysctx.h
sctx: construct boot-sys

; !!! The SYS_CTX has no SELF...it is not produced by the ordinary gathering
; constructor, but uses Alloc_Context() directly.  Rather than try and force
; it to have a SELF, having some objects that don't helps pave the way
; to the userspace choice of self-vs-no-self (as with func's <no-return>)
;
make-obj-defs/selfless sctx "SYS_CTX_" 1

write inc/tmp-sysctx.h out


;----------------------------------------------------------------------------
;
; b-boot.c - Boot data file
;
;----------------------------------------------------------------------------

;-- Build b-boot.c output file -------------------------------------------------


emit-head "Natives and Bootstrap" %b-boot.c
emit {
#include "sys-core.h"

}

externs: make string! 2000
boot-booters: load %booters.r
boot-natives: load %tmp-natives.r

nats: append copy boot-booters boot-natives
nat-count: 0

for-each val nats [
    if set-word? val [
        nat-count: nat-count + 1
    ]
]

print [nat-count "natives"]

emit [newline {const REBNAT Native_Funcs[} nat-count {] = ^{
}]
for-each val nats [
    if set-word? val [
        emit-line/code "N_" to word! val ","
    ]
    ;nat-count: nat-count + 1
]
emit-end
emit newline

;-- Embedded REBOL Tests:
;where: find boot/script 'tests
;if where [
;   remove where
;   for-each file sort load %../tests/ [
;       test: load join %../tests/ file
;       if test/1 <> 'skip-test [
;           where: insert where test
;       ]
;   ]
;]

;-- Build typespecs block (in same order as datatypes table):
boot-typespecs: make block! 100
specs: load %typespec.r
for-each type datatypes [
    append/only boot-typespecs select specs type
]

;-- Create main code section (compressed):
boot-types: new-types
boot-root: load %root.r
boot-task: load %task.r
boot-ops: load %ops.r
;boot-script: load %script.r

write %boot-code.r mold reduce sections
data: mold/flat reduce sections
insert data reduce ["; Copyright (C) REBOL Technologies " now newline]
insert tail data make char! 0 ; scanner requires zero termination

comp-data: compress data: to-binary data

emit [
{
// Native_Specs contains data which is the DEFLATE-algorithm-compressed
// representation of the textual function specs for Rebol's native
// routines.  Though DEFLATE includes the compressed size in the payload,
// NAT_UNCOMPRESSED_SIZE is also defined to be used as a sanity check
// on the decompression process.
}
newline
]

emit ["const REBYTE Native_Specs[NAT_COMPRESSED_SIZE] = {" newline]

;-- Convert UTF-8 binary to C-encoded string:
emit binary-to-c comp-data
emit-end/easy

write src/b-boot.c out

;-- Output stats:
print [
    "Compressed" length data "to" length comp-data "bytes:"
    to-integer ((length comp-data) / (length data) * 100)
    "percent of original"
]

;-- Create platform string:
;platform: to-string platform
;lowercase platform
;if platform-data/type = 'windows [ ; Why?? Not sure.
;   product: to-string product
;   lowercase product
;   replace/all product "-" ""
;]
;;dir: to-file rejoin [%../to- platform "/" product "/temp/"]

;----------------------------------------------------------------------------
;
; Boot.h - Boot header file
;
;----------------------------------------------------------------------------

emit-head "Bootstrap Structure and Root Module" %boot.h

emit [
{
#define MAX_NATS      } nat-count {
#define NAT_UNCOMPRESSED_SIZE } length data {
#define NAT_COMPRESSED_SIZE } length comp-data {
#define CHECK_TITLE   } checksum to binary! title {

extern const REBYTE Native_Specs[];
extern const REBNAT Native_Funcs[];

typedef struct REBOL_Boot_Block ^{
}
]

for-each word sections [
    word: form word
    remove/part word 5 ; boot_
    emit-line/code "REBVAL " word ";"
]
emit "} BOOT_BLK;"

;-------------------

emit [
{

//**** ROOT Context (Root Module):

typedef struct REBOL_Root_Vars ^{
    REBVAL rootvar; // [0] reserved for the context itself
}
]

for-each word boot-root [
    emit-line/code "REBVAL " word ";"
]
emit ["} ROOT_VARS;" lf lf]

n: 1
for-each word boot-root [
    emit-line/define "#define ROOT_" word join "(&Root_Vars->" [lowercase replace/all form word #"-" #"_" ")"]
    n: n + 1
]
emit ["#define ROOT_MAX " n lf]

;-------------------

emit [
{

//**** Task Context

typedef struct REBOL_Task_Context ^{
    REBVAL rootvar; // [0] reserved for the context itself
}
]

for-each word boot-task [
    emit-line/code "REBVAL " word ";"
]
emit ["} TASK_VARS;" lf lf]

n: 1
for-each word boot-task [
    emit-line/define "#define TASK_" word join "(&Task_Vars->" [lowercase replace/all form word #"-" #"_" ")"]
    n: n + 1
]
emit ["#define TASK_MAX " n lf]

write inc/tmp-boot.h out
;print ask "-DONE-"
;wait .3
print "   "
