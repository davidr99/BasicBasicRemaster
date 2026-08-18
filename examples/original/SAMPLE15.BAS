

        REM
        REM DEMONSTRATION OF PLAYING A WAVE FILE OVER WINDOWS SOUND
        REM SYSTEM OR SOUNDBLASTER UNDER DOS
        REM

        a=mouseon

        REM
        REM See if sound device present
        REM

            a=snddev(0)

            if a=0 then

              Messagebox "Sound system not present","Sound warning",64

            else

              a$="wav"+chr$(0)+"*.wav"+chr$(0)+chr$(0)
              openfileread a$,"","","Play a Wave File"

              if dialog$(102)="1" then
                ffname$=dialog$(100)
'                seterrlevel 5
                playsound ffname$,1
'                seterrlevel 7
              end if

            end if


print "Sound now playing..."
do while inkey$=""
loop








