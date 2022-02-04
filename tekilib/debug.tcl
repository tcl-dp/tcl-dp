#!/usr/local/bin/wish -f
# debug.tcl --

package provide Debug 1.0

#
# Debugging support.   This section contains some elementary debugging
# support.  It is used as a sort of sophisticated puts statement.
# It assumes the following conventions:
#    o when a procedure is entered, Debug_Enter is called.
#    o when a procedure is left, Debug_Leave is called.
#    o To print a message, Debug_Print is called.
#    o Fatal errors can be logged with Debug_FatalError
#

# Global variables used in debugging:

set DebugInfo(DebugOn)       1;
set DebugInfo(DebugLevel)    5;
set DebugInfo(DebugProc)     Global;
set DebugInfo(DebugStack)    Global;

#
# Debugging stuff:
#    o Debug_Enter is called when a procedure is entered
#    o Debug_Leave when it's left, and
#    o Debug_Print to print a message (with formatting)
#    o Debug_FatalError prints a message an exits
#

#
# Debug_FatalError
#    Called when all hope is lost.  Just prints an error and
#    exits.
#
# Arguments:
#    message        Message to print before exiting
#
proc Debug_FatalError message {
    global DebugInfo errorCode errorInfo
    puts stderr "Fatal error: $message"
    puts stderr "errorCode = $errorCode"
    puts stderr "errorInfo = $errorInfo"
    puts stderr "Stack: $DebugInfo(DebugStack)"
    puts stderr "exiting..."
    exit
}

#
# Debug
#    Called to print a debugging message.  Indents it, too!
#
# Arguments:
#    args        Message to print
#
proc Debug_Print args {
    global DebugInfo
    if $DebugInfo(DebugOn) {
        set level $DebugInfo(DebugLevel)
        puts [format "%24s: %${level}s%s" $DebugInfo(DebugProc) {} $args]
    }
}

#
# Debug_Enter
#    Should be called when we enter a routine.  Updates the call stack,
#    increments the stack depth variable (DebugLevel), and prints the
#    name of the procedure.
#
# Arguments:
#    procName        Name of procedure being entered.
#
proc Debug_Enter procName {
    global DebugInfo
    set DebugInfo(DebugStack) [concat $procName $DebugInfo(DebugStack)]
    set DebugInfo(DebugProc) $procName
    incr DebugInfo(DebugLevel) 2
    Debug_Print "Entering $procName"
}

#
# Debug_Leave
#    Counterpart of Debug_Enter.  Should be called when we leave a routine.
#    Updates the call stack, decrements the stack depth variable (DebugLevel),
#    and prints the name of the procedure.
#
# Arguments:
#    none
#
proc Debug_Leave {} {
    global DebugInfo
    set procName [lrange $DebugInfo(DebugStack) 0 0]
    set DebugInfo(DebugStack) [lrange $DebugInfo(DebugStack) 1 end]
    set DebugInfo(DebugProc) [lrange $DebugInfo(DebugStack) 0 0]
    Debug_Print Leaving $procName
    incr DebugInfo(DebugLevel) -2
}


