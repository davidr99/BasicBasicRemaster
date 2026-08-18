open "bbasic-file-test.dat" for output as #1
print #1,3,4,"hello"
close #1
open "bbasic-file-test.dat" for input as #1
input #1,a,b,text$
close #1
print a+b
print text$

open "bbasic-random-test.dat" for random as #1 len=8
field #1,4 as fielda$,4 as fieldb$
lset fielda$="AB"
lset fieldb$="CD"
put #1,1
lset fielda$=""
lset fieldb$=""
get #1,1
close #1
print fielda$+fieldb$
