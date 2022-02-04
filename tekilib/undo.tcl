#!/usr/local/bin/wish -f
# undo.tcl

package provide Undo 1.0

#
# Undo commands
#
# This section contains the commands to support a general "Undo"
# facility.  The basic structure is a stack of commands.  Each element
# of the stack contains a command that can Undo a previous action.
# For example, to "undo" a file copy, you would delete the file.
# The "file delete" command would then go on the stack.
#
# You can have several undo stack, each with an associated tag
#
# Three functions are provided:
#   o Undo_Add -- adds a command to the stack
#   o Undo_All -- execute the stack
#   o Undo_Clear -- clear the stack
#

#
# Undo_Add --
#   Adds to the Undo stack
#
# Arguments:
#   cmd
#       The command to execute to undo the current command.
#
proc Undo_Add {{tag all} cmd} {
    global UndoInfo
    if ![info exists UndoInfo($tag,UndoStack)] {
        set UndoInfo($tag,UndoStack) {}
    }
    set UndoInfo($tag,UndoStack) [concat [list $cmd] $UndoInfo($tag,UndoStack)]
}

#
# Undo_All --
#   Execute the Undo stack
#
# Arguments:
#   none
#
proc Undo_All {{tag all}} {
    global UndoInfo
    if ![info exists UndoInfo($tag,UndoStack)] {
        error "Invalid undo stack $tag"
    }
    foreach cmd $UndoInfo($tag,UndoStack) {
        catch {eval $cmd}
    }
    set UndoInfo(UndoStack) {}
}

#
# Undo_Clear --
#   Clear the Undo stack
#
# Arguments:
#   none
#
proc Undo_Clear {{tag all}} {
    global UndoInfo
    unset UndoInfo($tag,UndoStack)
}

