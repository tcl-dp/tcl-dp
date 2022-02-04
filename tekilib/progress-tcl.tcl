package provide Progress-Tcl 1.0

# This set of routines is used to give feedback to the user that
# a series of steps is completing.  
#
# Progress_StepInit is called to start the process.  You pass it a header
#     message and a list of steps (each a message)
# Progress_StepPrint is called when a step is completed.
# Progress_StepEnd is to terminate the process

proc Progress_StepInit {msg steps} {
    global ProgressInfo
    puts stderr $msg
    set ProgressInfo(steps) $steps
}

proc Progress_StepPrint {} {
    global ProgressInfo
    puts stderr "    [lindex $ProgressInfo(steps) 0]"
    set ProgressInfo(steps) [lrange $ProgressInfo(steps) 1 end]
}

proc Progress_StepEnd {} {
    global ProgressInfo
    catch {unset ProgressInfo(steps)}
}

