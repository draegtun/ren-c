; resources.test.reb

;; user-test folder
;;
;; test for %user.reb
;; Unable to test for %rebol.reb or %console-skin.reb

[
    find? cli-test/resources [] [] %NOT-HERE/
        {** RESOURCES directory not found}
]

[
    data: cli-test/resources ["--suppress" "*"] [] %user-test/
    empty? data/body/options/loaded
]

[
    data: cli-test/resources ["--suppress" "%user.reb"] [] %user-test/
    empty? data/body/options/loaded
]

[
    data: cli-test/resources [] [] %user-test/
    all? [
        1 == length-of data/body/options/loaded
        find? map-each n data/body/options/loaded [last split-path n] %user.reb
        find? data/header {running %user.reb}
    ]
]

; check bare HOME used in RESOURCES
[
    data: make string! 0
    call/wait/output [
        %../make/r3 
        "--suppress" "*"
        "--do" "print [system/options/home | system/options/resources]"
    ] data

    any? [
        ;data == newline    ;; means HOME wasn't set or found
        wrap [
            data: split data newline
            (to-file data/1) == (first split-path to-file data/2)
        ]
    ]
]

;; TBD - need set-env HOME test - perhaps best in separate prog so won't pollute tests

