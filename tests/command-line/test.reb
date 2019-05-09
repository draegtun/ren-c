cli-test: function [
    options [block!]
    args [block!]
    /no-script
    /no-suppress
][
    data: make string! 0
    script: %cli.reb
    call/wait/output compose [
        %../../make/r3 
        "--resources" %./
        (options) 
        (unless no-script [script]) 
        (args)
    ] data

    if parse? data [
        copy header: thru "!!!start!!!" 
        newline
        copy body: to "!!!end!!!"
        copy footer: to end
    ][
        return compose/only [
            header: (header)
            body:   (load body)
            footer: (footer)
        ]
    ]

    ;; couldnt parse so should be an error.  So just return string (and not object)
    data
]

;; base
data-copy: cli-test [] []

;; 1
data-test: copy/deep data-copy
data-test/body/options/about: true
data: cli-test ["--about"] []
probe (mold data) == (mold data-test)

;; 2
data-test: copy/deep data-copy
;data-test/body/options/about: true
;data-test/body/options/cgi: true
;data-test/body/options/quiet: true
data-test/body/options: to-block append object data-test/body/options [about: true cgi: true quiet: true]
probe (mold cli-test ["--about" "--cgi" "--quiet"] []) == (mold data-test)

tests: [
    --about     _   [about: true]
    --quiet     _   [quiet: true]
    --suppress "*"  [suppress: [%rebol.reb %user.reb %console-skin.reb]]
    [--help -?] _   {Command line usage}
    -cs         _   [cgi: true secure: allow]
]


;; 3
data-test: copy/deep data-copy
data-test/body/options: to-block append object data-test/body/options [cgi: true secure: allow]
probe (mold cli-test ["-cs"] []) == (mold data-test)


cli-rule: [
    set switches [word! | block!]
    set param  [string! | blank!]
    set options [string! | block!]
    ;set script [block!]
]

test-data: make block! 0

probe parse tests [
    some cli-rule
    (
        ;data: copy/deep data-copy
        ;data/body/options: to-block append object data/body/options options
        ;probe (mold cli-test compose [(to-string switches)] []) == (mold data)
        append/only test-data compose/deep [
            data: copy/deep data-copy
            data/body/options: to-block append object data/body/options [(options)]
            strict-equal? mold cli-test [(to-string switches) []] mold data
        ]
    )
]

probe test-data
