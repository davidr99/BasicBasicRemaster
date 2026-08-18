screen 1000
if mouseon<>-1 then goto failed
position 20,20,320,240
color 15,1
cls
locate 10,10
print "Modern BasicBasic"
line (5,35)-(300,220),7,b
line (10,40)-(80,90),4,bf
circle (130,80),30,14
paint (130,80),2,14
pset (200,80),12

control "",2001,0,"edit",0,10,120,120,24,15,1
setctext 2001,"hello"
if getctext(2001)<>"hello" then goto failed
control "Choice",2002,0,"radio",0,140,120,90,24,15,1
radioon 2002
createfont 1,18,0,0,0,400,0,0,0,0,0,0,0,0,"Arial"
selectfont 1
if dlen("test")<=0 then goto failed

createbitmap 1,0,64,64
selectbitmap 1
line (0,0)-(63,63),3,bf
selectdisplay
copybits 1,0,0,64,64,display,230,140,0
open "win32-smoke.ok" for output as #1
print #1,"PASS"
close #1
end

failed:
open "?:\\invalid\\failure" for input as #1
