package provide Progress-Tk 1.0

# This set of routines is used to give feedback to the user that
# a series of steps is completing.  
#
# Progress_StepInit is called to start the process.  You pass it a header
#     message and a list of steps (each a message)
# Progress_StepPrint is called when a step is completed.
# Progress_StepEnd is to terminate the process

# Define some fonts

set majorversion [lindex [split $tk_version .] 0]
if {$majorversion >= 8} {
    set ProgressInfo(font) [font create -family Times -weight bold -size 18]
} else {
    set ProgressInfo(font) *-Times-Bold-R-Normal--*-240-*-*-*-*-*-*
}
unset majorversion


# Utility to center a window on the screen.

proc ProgressCenterWindow {w} {
    update idletasks
    set x [expr [winfo screenwidth $w]/2 - [winfo reqwidth $w]/2 \
            - [winfo vrootx [winfo parent $w]]]
    set y [expr [winfo screenheight $w]/2 - [winfo reqheight $w]/2 \
            - [winfo vrooty [winfo parent $w]]]
    wm geom $w +$x+$y
    wm deiconify $w
    wm transient $w .
}

proc Progress_StepInit {msg items} {
    global ProgressInfo
    set ProgressInfo(progress) 0
    if {[info commands .progress] == ""} {
        toplevel .progress
        label .progress.title -text $msg -font $ProgressInfo(font)
        grid .progress.title -column 0 -row 0 -sticky new
    }
    wm title .progress $msg
    .progress.title configure -text $msg
    set i 0
    foreach item $items {
        set ProgressInfo(progress,$i) 0
        set w .progress.item$i
        checkbutton $w -variable ProgressInfo(progress,$i) -text $item
        incr i
        grid $w -row $i -sticky nw
    }
    set ProgressInfo(progressItemCount) $i 
    ProgressCenterWindow .progress
    update
}

proc Progress_StepPrint {} {
    global ProgressInfo
    set i $ProgressInfo(progress)
    set ProgressInfo(progress,$i) 1
    incr ProgressInfo(progress)
    update
}

proc Progress_StepEnd {} {
    global ProgressInfo
    wm withdraw .progress
    for {set i 0} {$i < $ProgressInfo(progressItemCount) } {incr i} {
        destroy .progress.item$i
    }
    catch {unset ProgressInfo(progress)}
    catch {unset ProgressInfo(progressItemCount)}
    update
}

