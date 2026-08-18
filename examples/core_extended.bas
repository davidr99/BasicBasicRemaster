dim values(3)
for i=0 to 3
  values(i)=i*2
next i
sum=0
for i=3 to 0 step -1
  sum=sum+values(i)
next i
gosub formatresult
print result$
restore numbers
read first
read second
print first+second
extended$=chr$(0)+chr$(59)
print len(extended$);asc(left$(extended$,1));asc(right$(extended$,1))
inlinea=0:inlineb=0
if 1 then inlinea=3:inlineb=4
print "INLINEIF:";inlinea+inlineb
end
formatresult:
result$=ucase$(left$("total",5))+":"+right$(str$(sum),2)
return
numbers:
data 4,5
