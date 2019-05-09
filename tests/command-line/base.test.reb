; command-line/base.test.reb

;
; Test Rebol command-line options
;
; uses CALL/OUTPUT for testing (see %cli.reb)
;


[
    cli-test: function [
        options [block!]
        args [block!]
        /no-script
        /resources
            resources-path [file!]
    ][
        data: make string! 0
        script: %command-line/cli.reb
        call/wait/output compose [
            %../make/r3 
            "--resources" (join-of %command-line/ if resources [resources-path])
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

    data: cli-test [] []

    cli-test-ok?: function [
        switches [block!]
        args [block!]
        options [block!]
        script  [block!]
    ] compose/only [
        b: copy/deep (data)
        b/body/options: to-block append object b/body/options options
        b/body/script:  to-block append object b/body/script script
        strict-equal? mold/flat select cli-test switches args 'body  mold/flat b/body
    ]

    ;; lets test defaults
    cli-test-ok? [] [] [
        ;; default system/options
        suppress: _
        encap: _
        script: %command-line/cli.reb
        args: []
        debug: _
        secure: _
        version: _
        quiet: true
        about: false
        cgi: false
        no-window: false
        verbose: false
    ] [
        ;; default system/script (TBD)
    ]
]
