; functions/control/loop.r
[
    num: 0
    loop 10 [num: num + 1]
    10 = num
]
; cycle return value
[false = loop 1 [false]]
; break cycle
[
    num: 0
    loop 10 [num: num + 1 break]
    num = 1
]
; break return value
[void? loop 10 [break]]
; break/return return value
[2 = loop 10 [break/return 2]]
; continue cycle
[
    success: true
    loop 1 [continue success: false]
    success
]
; zero repetition
[
    success: true
    loop 0 [success: false]
    success
]
[
    success: true
    loop -1 [success: false]
    success
]
; Test that return stops the loop
[
    f1: does [loop 1 [return 1 2]]
    1 = f1
]
; Test that errors do not stop the loop and errors can be returned
[
    num: 0
    e: loop 2 [num: num + 1 try [1 / 0]]
    all [error? e num = 2]
]
; loop recursivity
[
    num: 0
    loop 5 [
        loop 2 [num: num + 1]
    ]
    num = 10
]
; recursive use of 'break
[
    f: func [x] [
        loop 1 [
            either x = 1 [
                use [break] [
                    break: 1
                    f 2
                    1 = get/opt 'break
                ]
            ][
                false
            ]
        ]
    ]
    f 1
]