package provide Wise 1.0

#
# WISE user interface support
#
# The following procedures support the WISE style user interface.
# There are 4 exported functions:
#
#  Wise_Checklist and Wise_RadioList display a list of check buttons
#      (or radio buttons) for the user to select.  Both these routines
#      display next/back/finish/cancel buttons.
#  Wise_Message displays a long message, such as a copyright notice,
#      along with a set of buttons
#  Wise_GetDirName gets a directory name from the user



# Center the window on the screen.

proc Wise_CenterWindow {w} {
    update idletasks
    set x [expr [winfo screenwidth $w]/2 - [winfo reqwidth $w]/2 \
            - [winfo vrootx [winfo parent $w]]]
    set y [expr [winfo screenheight $w]/2 - [winfo reqheight $w]/2 \
            - [winfo vrooty [winfo parent $w]]]
    wm geom $w +$x+$y
    wm deiconify $w
    wm transient $w .
    raise $w
}

# Create the logo image (once)

proc WiseCreateLogo {} {
    global TekiInfo WiseInfo
    if {[info commands logoImage] == ""} {
        set WiseInfo(photo) [image create photo logoImage]
        logoImage read [file join $TekiInfo(library) logo.gif]
    }
}


# Create and display the WISE wizard window, centered on the screen.
# Cache the window for better performance

proc WiseMakeWizard {} {
    global WiseInfo TekiInfo
    if {[info commands .wise] == ""} {
        toplevel .wise
        WiseCreateLogo
        label .wise.logo -image logoImage
        grid .wise.logo -row 0 -column 0 -rowspan 2 -sticky n

        label .wise.title -font $TekiInfo(varfont)
        grid .wise.title -column 1 -row 0 -columnspan 5 -sticky ew
        grid rowconfigure .wise 1 -weight 1

        listbox .wise.list
        grid .wise.list -row 1 -column 1 -columnspan 5 -sticky nesw
        button .wise.back -text {< Back} -command "set WiseInfo(done) back"
        button .wise.next -text {Next >} -command "set WiseInfo(done) next"
        button .wise.finish -text {Finish} -command "set WiseInfo(done) finish"
        button .wise.cancel -text {Cancel} -command "set WiseInfo(done) cancel"
        grid .wise.back -row 2 -column 2 -sticky nsew
        grid .wise.next -row 2 -column 3 -sticky nsew
        grid .wise.finish -row 2 -column 4 -sticky nsew
        grid .wise.cancel -row 2 -column 5 -padx 10
        Wise_CenterWindow .wise
    } else {
        wm deiconify .wise
    }
}

# Display a checklist for the user in the WISE window.  
#
#    title is the window title
#    items is list {label value on?}
# One checkbox is created for each item, with the label as specified
#
# The return value depends on which button (next, back, finish, or cancel)
# the user presses.  It is one of the following:
#    back
#    cancel
#    {next list-of-selected-items}
#    {finish list-of-selected-items}
# where list-of-selected-items is the value part from the "items" list

proc Wise_Checklist {title items} {
    global WiseInfo

    WiseMakeWizard
    wm title .wise $title
    .wise.title configure -text $title

    set count 0
    foreach i $items {
        set name [lindex $i 0]
        set on [lindex $i 2]
        set w .wise.list.b$count
        set WiseInfo($count) $on
        checkbutton $w -variable WiseInfo($count) -text $name -anchor w
        pack $w -side top -fill x
        # grid $w -row $count -sticky nsw
        incr count
    }

    set WiseInfo(done) 0
    vwait WiseInfo(done)
    set rv $WiseInfo(done)

    if {[lsearch "next finish" $rv] != -1} {
        set count 0
        foreach i $items {
            set value [lindex $i 1]
            if $WiseInfo($count) {
                lappend rv $value
            }
            incr count
        }
    }

    wm withdraw .wise
    set count 0
    foreach i $items {
        destroy .wise.list.b$count
        incr count
    }
    unset WiseInfo
    return $rv
}

# Display a radio-button list for the user in the WISE window.  
#
#    title is the window title
#    items is list {label value on?}
# One checkbox is created for each item, with the label as specified
#
# The return value depends on which button (next, back, finish, or cancel)
# the user presses.  It is one of the following:
#    back
#    cancel
#    {next selected-item}
#    {finish selected-item}
# where selected-item is the value part (from the "items" list) of the
# radio-button the user selected

proc Wise_Radiolist {title items {backDisabled 0}} {
    global WiseInfo

    WiseMakeWizard
    wm title .wise $title
    .wise.title configure -text $title

    set count 0
    set WiseInfo(radio) {}
    foreach i $items {
        set name [lindex $i 0]
        set value [lindex $i 1]
        set on [lindex $i 2]
        set w .wise.list.b$count
        if {$on} {
            set WiseInfo(radio) $value
        }
        radiobutton $w -variable WiseInfo(radio) -text $name -anchor w -value $value
        pack $w -side top -fill x
        incr count
    }

    set WiseInfo(done) 0
    if {$backDisabled} {
        .wise.back configure -state disabled
    }
    vwait WiseInfo(done)
    set rv $WiseInfo(done)

    wm withdraw .wise
    .wise.back configure -state normal

    set count 0
    foreach i $items {
        destroy .wise.list.b$count
        incr count
    }

    if {[lsearch "next finish" $rv] != -1} {
        set rv [concat $rv $WiseInfo(radio)]
    }
    unset WiseInfo
    return $rv
}

#
# Display a long message, such as a copyright notice, to the user
# in a textbox.
#
#    title is the window title
#    msg is the text of the message
#    items is a list of labels.  One button is created for each label.
#
# The return value is the index of the button pressed

proc Wise_Message {title msg items {dispLogo 1}} {
    global WiseInfo

    if {[info commands .wisemsg] == ""} {
        toplevel .wisemsg
        wm title .wisemsg $title
        WiseCreateLogo
        label .wisemsg.logo -image logoImage
        scrollbar .wisemsg.scroll -command ".wisemsg.text yview"
        text .wisemsg.text -relief sunken -bd 2 -yscrollcommand ".wisemsg.scroll set" -setgrid 1 -height 10
        Wise_CenterWindow .wisemsg
    }

    set WiseInfo(done) -1
    set count 1
    foreach i $items {
        button .wisemsg.b$count -text $i -command "set WiseInfo(done) $count"
        grid .wisemsg.b$count -row 2 -column $count
        incr count
    }

    .wisemsg.text delete 1.0 end
    .wisemsg.text insert end $msg
    grid .wisemsg.text -column 1 -row 0 -columnspan [expr $count-1] -sticky nsew
    grid .wisemsg.scroll -column $count -row 0 -sticky ns
    grid rowconfigure .wisemsg 0 -weight 1

    if {$dispLogo} {
        grid .wisemsg.logo -row 0 -column 0 -sticky n
    } else {
        grid forget .wisemsg.logo
    }

    Wise_CenterWindow .wisemsg
    vwait WiseInfo(done)
    set rv [expr $WiseInfo(done)-1]
    wm withdraw .wisemsg

    set count 1
    foreach i $items {
        destroy .wisemsg.b$count
        incr count
    }
    unset WiseInfo
    return $rv
}

# Ask the user to enter a directory name.  Return either the
# directory name, or the empty string if they hit "cancel".  Right
# now, the "Browse..." option is funky, since it requires the user
# to select a file in the directory where they want to install...
#
# XXX: need to fix this...

proc Wise_GetDirName {default} {
    global WiseInfo
    catch {destroy .d}
    toplevel .d
    wm title .d "Select directory"

    set WiseInfo(done) 0
    set WiseInfo(value) $default
    label .d.label -text "Enter directory name"
    entry .d.entry -textvariable WiseInfo(value)
    button .d.ok -text OK -command "set WiseInfo(done) ok"
    button .d.browse -text Browse... -command {
        set WiseInfo(value) [file dirname [tk_getOpenFile -title "Select file in directory"]]
    }
    button .d.cancel -text Cancel -command "set WiseInfo(done) cancel"
    grid .d.label -row 0 -column 0 -sticky e
    grid .d.entry -row 0 -column 1 -columnspan 2 -sticky ew
    grid .d.ok -row 1 -column 0 
    grid .d.browse -row 1 -column 1 
    grid .d.cancel -row 1 -column 2
    grid columnconfigure .d 1 -weight 1
    Wise_CenterWindow .d
    vwait WiseInfo(done)
    if {$WiseInfo(done) == "ok"} {
        set rv $WiseInfo(value)
    } else {
        set rv {}
    }
    destroy .d
    unset WiseInfo
    return $rv
}

