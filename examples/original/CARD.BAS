


        rem
        rem  Example of using controls in a BasicBasic program.
        rem



        rem mode 1000 the best mode
        screen 1000

        rem color background
        line (0,0)-(system(10),system(11)),8,bf

          xoffset=50
          yoffset=80
        position 0,0,xoffset+550,yoffset+300

        currentrecord=1

        rem turn on mouse
        a=mouseon


        rem open my file
        OPEN "ROLODEX.DAT" FOR RANDOM AS #1 len=128
        FIELD #1,18 AS FIRST$,18 AS LAST$,30 AS ADDRESS$,18 AS CITY$,2 AS STATE$,5 AS ZIP$,1 as sex$,16 as food$


        rem
        rem define my controls
        rem
        line (xoffset+1,yoffset-4)-(xoffset+521,yoffset+199),14,b
        line (xoffset+2,yoffset-3)-(xoffset+522,yoffset+200),14,bf
        line (xoffset+3,yoffset-2)-(xoffset+523,yoffset+201),14,bf
        line (xoffset,yoffset-5)-(xoffset+520,yoffset+198),7,bf
        line (xoffset,yoffset-5)-(xoffset+520,yoffset+198),0,b
        control "First Name:",1001,5,"static",0,1+xoffset,1+yoffset,100,30,0,7
        control "Last:",1002,5,"static",0,210+xoffset,1+yoffset,100,30,0,7
        control "Address:",1003,5,"static",0,1+xoffset,32+yoffset,100,30,0,7
        control "City:",1004,5,"static",0,1+xoffset,63+yoffset,100,30,0,7
        control "State:",1005,5,"static",0,1+xoffset,94+yoffset,100,30,0,7
        control "Zip:",1006,5,"static",0,201+xoffset,94+yoffset,100,30,0,7
        control "Food:",1007,5,"static",0,201+xoffset,129+yoffset,100,30,0,7

        control "Edit 1",1101,0,"edit",0,101+xoffset,1+yoffset,100,30,0,7
        control "Edit 2",1102,0,"edit",0,311+xoffset,1+yoffset,200,30,0,7
        control "Edit 3",1103,0,"edit",0,101+xoffset,32+yoffset,200,30,0,7
        control "Edit 4",1104,0,"edit",0,101+xoffset,63+yoffset,200,30,0,7
        control "Edit 5",1105,0,"edit",0,101+xoffset,94+yoffset,50,30,0,7
        control "Edit 6",1106,0,"edit",0,302+xoffset,94+yoffset,100,30,0,7
        control " Male",1107,0,"radio",0,111+xoffset,129+yoffset,100,30,7,6
        control " Female",1108,0,"radio",0,111+xoffset,156+yoffset,100,30,7,6
                line (110+xoffset,128+yoffset)-(111+xoffset+100,156+yoffset+30),0,b
        control "",1112,0,"combo",0,302+xoffset,129+yoffset,100,90,0,7

        line (xoffset+39,yoffset+213)-(xoffset+372,yoffset+258),14,b
        line (xoffset+40,yoffset+214)-(xoffset+373,yoffset+259),14,b
        line (xoffset+41,yoffset+215)-(xoffset+374,yoffset+260),14,b
        line (xoffset+39,yoffset+213)-(xoffset+371,yoffset+257),1,bf
        line (xoffset+39,yoffset+213)-(xoffset+371,yoffset+257),0,b
        control "Previous",1109,0,"push",0,xoffset+41,yoffset+215,80,40,7,6
        control "Next",1110,0,"push",0,xoffset+290,yoffset+215,80,40,7,6

        line (xoffset+388,yoffset+213)-(xoffset+471,yoffset+258),14,b
        line (xoffset+389,yoffset+214)-(xoffset+472,yoffset+259),14,b
        line (xoffset+390,yoffset+215)-(xoffset+473,yoffset+260),14,b
        line (xoffset+388,yoffset+213)-(xoffset+470,yoffset+257),1,bf
        line (xoffset+388,yoffset+213)-(xoffset+470,yoffset+257),0,b
        control "Exit",1111,0,"push",0,xoffset+389,yoffset+215,80,40,7,6


        gosub setuprecord


        firsttab=1101
        maxtab=1112
        a=setfocus(firsttab)

        result%=0
        do while result%=0
          a$=inkey$
          if len(a$)>1 then
            achar=asc(right$(a$,1))
            if achar=68 then
              result%=1
            elseif achar=107 then
              radioon 1107
              radiooff 1108
              lset sex$="M"
            elseif achar=108 then
              radioon 1108
              radiooff 1107
              lset sex$="F"
            elseif achar=109 then
              if currentrecord>1 then
                gosub savethisrecord
                currentrecord=currentrecord-1
                gosub setuprecord
              end if
            elseif achar=110 then
                gosub savethisrecord
                if eof(1)=0 then
                  currentrecord=currentrecord+1
                  gosub setuprecord
                end if
            elseif achar=111 then
                gosub savethisrecord
                stop
            end if
          else
            if a$=chr$(9) then
              focusitem=getfocus
              focusitem=focusitem+1
              if focusitem>maxtab then
                focusitem=firsttab
              elseif focusitem<firsttab then
                focusitem=maxtab
              end if
              a=setfocus(focusitem)
            end if
          end if
        loop


stop



rem
rem save current data
rem

savethisrecord:
        lset first$=getctext(1101)
        lset last$=getctext(1102)
        lset address$=getctext(1103)
        lset city$=getctext(1104)
        lset state$=getctext(1105)
        lset zip$=getctext(1106)
        lset food$=getctext(1112)
        a$=last$

        do while right$(a$,1)=" "
          a$=left$(a$,len(a$)-1)
        loop

        if a$<>"" then
           put #1,currentrecord
        else
           gosub setuprecord
        end if

        return




rem
rem get the data for the record in currentrecord
rem
setuprecord:
        get #1,currentrecord
        color 15,1
        locate yoffset+215,xoffset+145
        print "Current Record";
        locate yoffset+235,xoffset+170
        print using " ####";currentrecord;

    if eof(1)=0 then
        rem
        rem display current data record information
        rem
        setctext 1101,first$
        setctext 1102,last$
        setctext 1103,address$
        setctext 1104,city$
        setctext 1105,state$
        setctext 1106,zip$
        if sex$="M" then
          radioon 1107
          radiooff 1108
        else
          radioon 1108
          radiooff 1107
        end if
        gosub fillfoodcombo
     else
        setctext 1101," "
        setctext 1102,""
        setctext 1103,""
        setctext 1104,""
        setctext 1105,""
        setctext 1106,""
        lset food$=""
        gosub fillfoodcombo
        radioon 1107
        radiooff 1108
     end if
        a=setfocus(1101)
return



fillfoodcombo:
                a=list(1112,11,0,"")
                a=list(1112,1,0,"Cajun")
                a=list(1112,1,0,"Tex-Mex")
                a=list(1112,1,0,"Bland")
                a=list(1112,1,0,"Barbeque")
                if food$<>"" then
                  a=list(1112,2,0,food$);
                  f$=getctext(1112)
                  if f$<>food$ then
                    a=list(1112,1,0,food$)
                    a=list(1112,2,0,food$)
                  end if
                end if

        return















