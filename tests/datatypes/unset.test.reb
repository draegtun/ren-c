; datatypes/unset.r
(null? ())
(blank? type of ())
(not null? 1)

[#68
    (null? trap/with [a: ()] [_])
]

(error? trap [a: () a])
(not error? trap [set/opt 'a ()])

(
    a-value: 10
    unset 'a-value
    e: trap [a-value]
    e/id = 'no-value
)
