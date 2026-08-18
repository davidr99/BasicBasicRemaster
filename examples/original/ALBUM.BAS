





rem
rem This program displays a series of .bmp files to the screen.
rem


      rem
      rem I will handle re-painting to save space
      rem
      repaintok=0
      on paint gosub 1000


      REM windows name "Family Album"

      rem use best available video mode
      rem Assume 256 colors, if all pictures use less then
      rem change this
      maxcolor=system(17)
      if maxcolor=0 then
        maxcolor=256  : rem maxcolor can =0 if >65365 colors
      end if
      screen 1000,256

      rem black background to start
      line (0,0)-(system(8),system(9),0,bf


      rem
      rem make array to hold bitmap info
      rem
      dim a&(256)


     rem
     rem enable mouse
     rem
     y=mouseon



     rem
     rem display first picture
     rem
     restore filelist
     read ffname$
     gosub displayfile
     repaintok=1


     rem
     rem main program loop.
     rem  - Sleep 60 seconds
     rem  - If key pressed stop program
     rem  - display next picture
     rem
   popcount=0
   newpicture=0
   result=0
   do
     sleep 2
     a=mouseb
     if a <>0 then
       newpicture=151
     end if
     newpicture=newpicture+1
     if newpicture>150 then
       read ffname$
       if ffname$="end"
          restore filelist
          read ffname$
       end if
       repaintok=0
       gosub displayfile
       repaintok=1
       popcount=popcount+1
       if popcount=5 then
          position -2,-2,-2,-2
          popcount=0
       end if
       newpicture=0
     end if
   loop while result=0

   stop








     rem
     rem Read and display the file ffname$.  Adjust window size
     rem to fit size of picture (but do not change origin).
     rem

displayfile:

       rem
       rem read bitmap header and get info on it
       rem
       a=bitmaph(ffname$,a&(0))
       x=0
       y=0
       pxsize=a&(1)
       pysize=a&(2)
       colors=a&(4)
       if colors=1 or colors=2 then
          colors=1
       elseif colors=4 then
          colors=16
       else
          colors=256
       end if

       rem
       rem Now select palette resolution required to reproduce bitmap
       rem
       rem If you are going to do a STOREBITMAP after loading the bitmap
       rem you should usually define a palette the same as the resolution
       rem of the picture you are loading.  This is expecially true if you
       rem tell BasicBasic to convert colors on loading.  BasicBasic may
       rem use all palette entries available, thereby increasing the number
       rem of palette entries required to show the picture.
       rem
       rem Also, note, that Windows in standard VGA (16 color) does not allow
       rem palette changes.
       rem

       if colors>maxcolor then colors=maxcolor


       rem
       rem only if in Windows load colors from bitmap
       rem

       if ostype=2 and maxcolor>16 then
          a=bitmapc(ffname$,a&(0))
          if colors>236 then
           for i=10 to 246
             palette i,a&(i)
           next i
          else
           for i=0 to colors-1
            palette i+10,a&(i)
           next i
          end if
       end if

       if pxsize<largestx then
         x=int((largestx-pxsize)/2)
       end if
       if pysize<largesty then
         y=int((largesty-pysize)/2)
       end if


       position system(8),system(9),pxsize,pysize,1
        x=0
        y=0

       rem
       rem Set to always convert to closest color possible
       rem
       convert=0
       xmult=1
       ymult=1
       line (0,0)-(largestx,largesty),0,bf
       loadbitmap ffname$,0,x,y,0,0,pxsize,pysize,convert,xmult,ymult

       return





rem
rem below is list of files to display
rem

filelist:

        data "samplew3.bmp
        data "end"






       rem
       rem  Handle screen re-paint here
       rem

1000

        if repaintok=1 then
          gosub displayfile
        end if
        return
















