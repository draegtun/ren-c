Rebol []

print "!!!start!!!"
probe compose/only [
    options: (body-of system/options)
    script: (body-of system/script)
]
print "!!!end!!!"
