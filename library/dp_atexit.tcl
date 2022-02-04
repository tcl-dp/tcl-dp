#
# dp_atexit -- command to install a Tcl callback to be invoked when
#        -- the exit command is evalutated.
#
# exit -- command to exit process, after all callbacks installed by
#      -- the dp_atexit command have been invoked.
#
#
# This module is structured as a sequence of "helper" functions and
# a single "dispatch" function that just calls the helpers.

set dp_atexit_inited 0

#######################################################################
#
# dp_atexit_appendUnique --
#
# Helper function to append callback to end of the callbacks list if
# it's not already there.
#
proc dp_atexit_appendUnique args {
    global dp_atexit_callbacks;

    if {[llength $args] != 1} {
        error {wrong # args : should be "dp_atexit appendUnique callback"};
    }

    # append callback to end of the callbacks list.
    set callback [lindex $args 0];
    if {[lsearch $dp_atexit_callbacks $callback] == -1} {
        lappend dp_atexit_callbacks $callback;
    }
}

#######################################################################
#
# dp_atexit_append --
#
# Helper function to append callback to end of the callbacks list
#
proc dp_atexit_append args {
    global dp_atexit_callbacks;

    if {[llength $args] != 1} {
        error {wrong # args : should be "dp_atexit appendUnique callback"};
    }
    set callback [lindex $args 0];
    lappend dp_atexit_callbacks $callback;
}

#######################################################################
#
# dp_atexit_prepend --
#
# Helper function to prepend callback to front of the callbacks list.
#
proc dp_atexit_prepend args {
    global dp_atexit_callbacks;

    if {[llength $args] != 1} {
        error {wrong # args : should be "dp_atexit prepend callback"};
    }
    set callback [lindex $args 0];
    set dp_atexit_callbacks "\{$callback\} $dp_atexit_callbacks";
}

#######################################################################
#
# dp_atexit_insert --
#
# Helper function to insert callback before the "before" callback in the
# callbacks list
#
proc dp_atexit_insert args {
    global dp_atexit_callbacks;

    if {[llength $args] != 2} {
        error {wrong # args : should be "dp_atexit insert before callback"};
    }
    set before   [lindex $args 0];
    set callback [lindex $args 1];
    set l {};
    foreach c $dp_atexit_callbacks {
        if {[string compare $before $c] == 0} {
            lappend l $callback;
        }
        lappend l $c;
    }
    set dp_atexit_callbacks $l;
}

#######################################################################
#
# dp_atexit_delete --
#
# Helper function to delete callback from the callbacks list.
#
proc dp_atexit_delete args {
    global dp_atexit_callbacks;

    if {[llength $args] != 1} {
      error {wrong # args : should be "dp_atexit delete callback"};
    }
    set callback [lindex $args 0];
    set l {};
    foreach c $dp_atexit_callbacks {
      if {[string compare $callback $c] != 0} {
        lappend l $c;
      }
    }
    set dp_atexit_callbacks $l;
}

#######################################################################
#
# dp_atexit_clear --
#
# Helper function to  clear callbacks list
#
proc dp_atexit_clear args {
    global dp_atexit_callbacks;

    if {[llength $args] != 0} {
      error {wrong # args : should be "dp_atexit clear"};
    }
    set dp_atexit_callbacks {};
}

#######################################################################
#
# dp_atexit --
#
# "option" may be appendUnique, append, prepend, insert, delete,
#                 clear, set, or list.
# "args" depends on the option specified.
#
# The global variable dp_atexit_callbacks is where we store the
# list of installed dp_atexit callbacks.
#
proc dp_atexit {{option list} args} {
    global dp_atexit_inited;
    global dp_atexit_callbacks;

    # Hide real exit command and init the global variable
    if {!$dp_atexit_inited} {
        incr dp_atexit_inited
        rename exit dp_atexit_really_exit
        dp_atexit_install_exit
        set dp_atexit_callbacks {}
    }

    case $option in {
        set { set dp_atexit_callbacks $args; }
        appendUnique { eval dp_atexit_appendUnique $args }
        append { eval dp_atexit_append $args }
        prepend { eval dp_atexit_prepend $args }
        insert { eval dp_atexit_insert $args }
        delete { eval dp_atexit_delete $args }
        clear { eval dp_atexit_clear $args }
        list {return $dp_atexit_callbacks}
        default {
            error {options: append, appendUnique, prepend, insert, delete,
clear, set, or list};
        }
    }
    return $dp_atexit_callbacks;
}

#######################################################################
#
# dp_atexit_install_exit -- Wrapper to install exit command that first
# invokes all callbacks installed by the dp_atexit command before doing
# real exit.
#
proc dp_atexit_install_exit {} {
    uplevel #0 {proc exit {{code 0}} {
        global dp_atexit_callbacks;

        while {1} {

          # Every iteration, we rescan dp_atexit_callbacks, in case
          # some callback modifies it.

          if {[catch {set dp_atexit_callbacks} callbacks]} {
              break;
          }
          if {[llength $callbacks] <= 0} {
              break;
          }
          set callback  [lindex $callbacks 0];
          set dp_atexit_callbacks [lrange $callbacks 1 end];

          catch {uplevel #0 $callback};
        }

        catch {unset dp_atexit_callbacks};
        catch {dp_atexit_really_exit $code};
    }
  }
}


