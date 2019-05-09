; command-line/options.test.reb

; --
[
    ;; Not implemented - throws start-up error
    find? cli-test ["--"] [] {Startup encountered an error!}
]

; --about
[
    cli-test-ok? [{--about}] [] [about: true] []
]

; --cgi | -c
[
    all? [
        cli-test-ok? [{--cgi}] [] data: [cgi: true] []
        cli-test-ok? [{-c}] [] data []
    ]
]

; --debug PARAM
[
    ;; at moment param is forced to-logic  ??
    cli-test-ok? [{--debug} {foo}] [] [debug: true] []
]

; --do PARAM
[
    #"2" == first cli-test/no-script ["--do" "print 1 + 1"] []
]

; --help | -?
[
    all? [
        find? cli-test/no-script ["--help"] [] data: {Command line usage:}
        find? cli-test/no-script ["-?"] [] data
    ]
]

; --import PARAM
;; see import.test.reb

; --resources PARAM
;; see resources.test.reb

; --quiet | -q
[
    all? [
        cli-test-ok? [{--quiet}] [] data: [quiet: true] []
        cli-test-ok? [{-q}] [] data []
    ]
]

; --script SCRIPT
;; see args.test.reb

; --secure PARAM | +s
[
    all? [
        ;; Only param that doesn't give an error
        cli-test-ok? ["--secure" "allow"] [] [secure: allow] []

        ;; this will error
        find? cli-test ["--secure" "foo"] [] {SECURE is disabled}

        ;; this will error
        find? cli-test ["+s"] [] {SECURE is disabled}
    ]
]

; --suppress PARAM
[
    all? [
        ;; suppress all
        data: cli-test ["--suppress" "*"] [] 
        data/body/options/suppress == [%rebol.reb %user.reb %console-skin.reb]

        ;; suppress just converts input to block - no checks on input
        cli-test-ok? ["--suppress" "foo bar baz"] [] [suppress: [foo bar baz]] []
    ]
]

; --verbose
[
    cli-test-ok? ["--verbose"] [] [verbose: true] []
]

; --version | -V | -v
[
    all? [
        find? cli-test/no-script ["--version"] [] data: to-string system/version
        find? cli-test/no-script ["-v"] [] data
        find? cli-test/no-script ["-V"] [] data
    ]
]

; -w
[
    cli-test-ok? ["-w"] [] [] []    ;; does nothing at moment!
]


;;
;; NB. At moment Rebol doesn't handle combined multiple switches
;;     except for some defined combos (see next 2 tests)

; -qs
[
    cli-test-ok? ["-qs"] [] [quiet: true secure: allow] []
]

; -cs
[
    cli-test-ok? ["-cs"] [] [cgi: true secure: allow] []
]

;; test some switches combined
[
    all? [
        cli-test-ok? ["-c" "-q" "-s"] [] [cgi: true quiet: true secure: allow] []
        cli-test-ok? [{--about} {--cgi} {--quiet}] [] [about: true cgi: true quiet: true] []
    ]
]

;; Test for invalid option switch ERROR
[
    all? [
        find? cli-test ["--xxx"] [] {Startup encountered an error!}
        find? cli-test ["-a"] [] {** Unknown command line option:}
    ]
]

;; Test for missing param ERROR (only trailing will ERROR)
[
    find? cli-test/no-script ["--script"] [] {** SCRIPT parameter missing}
]

;; NOT TESTED
;;  -t --trace

