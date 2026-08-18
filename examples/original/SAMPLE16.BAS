

rem
rem Example of MESSAGEBOX command
rem

        cls

        rem
        rem Find out if mouse present or not.  Make messagebox prompt
        rem depend on mouse or not.
        rem

        a=mouseon
        if a<>0 then
          a$="Click on desired button"
        else
          a$="TAB to Desired Button"
        end if

        done=0
        do while done=0
          MESSAGEBOX "Do you want to exit?",a$,4
          if dialog$(6)="1" then
            done=1
          end if
        loop















