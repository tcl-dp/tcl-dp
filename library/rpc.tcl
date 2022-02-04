# rpc.tcl --
#
# Utilities to create reentrant RPC clients and servers (full duplex)
#
# This file contains the utility procedures to help implement 
# user-friendly remote procedure calls (RPC's) on top of the 
# network primitives provided by the dp_connect, dp_accept, etc. and 
# by various other primitives.
#
# Copyright 1992 Regents of the University of California
# Permission to use, copy, modify, and distribute this
# software and its documentation for any purpose and without
# fee is hereby granted, provided that this copyright
# notice appears in all copies.  The University of California
# makes no representations about the suitability of this
# software for any purpose.  It is provided "as is" without
# express or implied warranty.
#

########################################################################

#
# Access control lists -- sort of an xhost style implementation
#
set dp_Acl {}

proc dp_Host {str} {
    global dp_Acl
    set key [string range $str 0 0]
    set str [string range $str 1 end]
    case $key in {
    "+" {
	    if {[string length $str] == 0} {
		set dp_Acl {}
		set rv {Access control disabled. Any clients may connect}
	    } else {
		if {([string first * $str] != -1) ||
		    ([string first \[ $str] != -1) ||
		    ([string first ? $str] != -1) ||
		    ([string first \] $str] != -1)} {
		    set rv "Clients from $str may connect"
		    set dp_Acl [Acl+ $dp_Acl $str]
		} else {
		    set ipaddr [dp_netinfo -address $str]
		    if {$ipaddr == "255.255.255.255"} {
			error "Unknown host $str"
		    }
		    set dp_Acl [Acl+ $dp_Acl $ipaddr]
		    set rv "Clients from $ipaddr may connect"
		}
	    }
	}

    "-" {
	    if {[string length $str] == 0} {
		set dp_Acl {{0 *} {0 *}}
		set rv {Access control enabled. No clients may connect}
	    } else {
		if {([string first * $str] != -1) ||
		    ([string first \[ $str] != -1) ||
		    ([string first ? $str] != -1) ||
		    ([string first \] $str] != -1)} {
		    set rv "Clients from $str may not connect"
		    set dp_Acl [Acl- $dp_Acl $str]
		} else {
		    set addr [dp_address create $str 0]
		    set ipaddr [lindex [dp_address info $addr] 0]
		    dp_address delete $addr
		    if {$ipaddr == "255.255.255.255"} {
			error "Unknown host $str"
		    }
		    set dp_Acl [Acl- $dp_Acl $ipaddr]
		    set rv "Clients from $ipaddr may not connect"
		}
	    }
	}

    default {return $dp_Acl}
    }
    return $rv
}

########################################################################

proc dp_CheckHost {file inetAddr} {
    global dp_Acl
    if {[AclCheck $dp_Acl $inetAddr] == 0} {
	error "Host not authorized"
    }
}

proc dp_AcceptRPCConnection {loginFunc checkCmd file} {
    # puts "dp_AcceptRPCConnection $loginFunc $checkCmd $file"
    set connection [dp_accept $file]
    # puts "connection = $connection"
    set newFile [lindex $connection 0]
    set inetAddr [lindex $connection 1]
    if {[string compare "none" $loginFunc] != 0} {
	set error [catch {eval $loginFunc $file $inetAddr} msg]
	if $error {
	    puts $newFile "Connection refused: $msg"
	    close $newFile
	    return;
	}
    }
    puts $newFile "Connection accepted"
    # puts "Calling dp_admin $newFile -check $checkCmd"
    dp_admin register $newFile -check $checkCmd
    dp_CleanupRPC $newFile
}

########################################################################

proc dp_MakeRPCClient {host port {checkCmd none}} {
    # puts "attempting to connect"
    set client [lindex [dp_connect tcp -host $host -port $port] 0]
    # puts "connected -- waiting for reply"
    set return [gets $client]
    # puts stdout $return
    if {[lindex $return 1] == "refused:"} {
	close $client
	error $return
    }
    if {[string match "Server not responding*" $return]} {
        close $client
        error $return
    }
    dp_admin register $client -check $checkCmd
    dp_CleanupRPC $client
    # puts stdout "Created $client"
    return $client
}

proc dp_MakeRPCServer {{port 0} {loginFunc none} {checkCmd none}
			{retPort 0}} {
    # puts "dp_MakeRPCServer $port $loginFunc $checkCmd $retPort"
    set rv [dp_connect tcp -server 1 -myport $port]
    # puts "rv = $rv"
    set server [lindex $rv 0]

    fileevent $server readable "dp_AcceptRPCConnection $loginFunc $checkCmd $server"
    dp_atexit appendUnique "close $server"
    dp_atclose $server append "dp_ShutdownServer $server"
    return $server
}

#######################################################################
#
# This creates two "callbacks" which will clean up RPC connections
# behind the user's back.  If the user calls "close $rpcchan", we
# "alias" this to "dp_CloseRPC $rpcchan" which is defined below.
#
# Likewise, if the user tries to exit tclsh/wish, we close the
# RPC channel first (which will, in turn, call dp_CloseRPC as above.
#

proc dp_CleanupRPC {file} {
    dp_atclose $file appendUnique "dp_CloseRPC $file"
    dp_atexit appendUnique "close $file"
}

#
# Shut down the listening socket.  This is usually invoked as an
# atclose callback.  It arranges to delete the filehandler once all
# processing has been done.
#

proc dp_ShutdownServer {file} {
    catch {dp_atexit delete "close $file"}
}

#
# Shut down a connection by telling the other end to shutdown and
# removing the filehandler on this file.
#
# Step 1: remove the file handler to prevent evaluating any new RPCs
# Step 2: Send an RDO to the far end to shutdown the connection
# Step 3: Clean up the call to shutdown the connection on exit.
#

#
# Close an RPC channel:  shut down the other side and unregister.
# This is a callback done right before the actual close.
# (Thus no actual close command)
#
proc dp_CloseRPC {file} {
    dp_RDO $file dp_CloseRPCFile
    dp_admin delete $file
    dp_atexit delete "close $file"
    # puts stdout "close $file"
}

#
# Respond to remote sites request to close the rpc file.
# In this case, we don't want to call dp_CloseRPC (which will,
# in turn, try to close the remote site which is already closed),
# so we need to remove the dp_ShutdownRPC call from the atclose
# callback list before calling close.
#

proc dp_CloseRPCFile {} {
    global dp_rpcFile
    dp_admin delete $dp_rpcFile
    dp_atclose $dp_rpcFile delete "dp_CloseRPC $dp_rpcFile"
    dp_atexit delete "close $dp_rpcFile"

    # Under Unix, we seem to lose the callback on the server
    # socket unless we pause before closing the socket.  I
    # have no idea why this is true, but select() stops
    # responding to connection requests.  In any case,
    # this is an ugly hack that seems to work

    after idle close $dp_rpcFile
}

###########################################################################
#
# Trap read errors on sockets and close the socket
#
proc tkerror {info} {
    global tk_library
    if [info exists tk_library] {tkerror.tk $info} else {error $info}
}

# This file contains a default version of the tkError procedure.  It
# posts a dialog box with the error message and gives the user a chance
# to see a more detailed stack trace.

proc tkerror.tk err {
    global errorInfo
    set info $errorInfo
    if {[tk_dialog .tkerrorDialog "Error in Tcl Script" \
	    "Error: $err" error 0 OK "See Stack Trace"] == 0} {
	return
    }

    set w .tkerrorTrace
    catch {destroy $w}
    toplevel $w -class ErrorTrace
    wm minsize $w 1 1
    wm title $w "Stack Trace for Error"
    wm iconname $w "Stack Trace"
    button $w.ok -text OK -command "destroy $w"
    text $w.text -relief raised -bd 2 -yscrollcommand "$w.scroll set" \
	    -setgrid true -width 40 -height 10
    scrollbar $w.scroll -relief flat -command "$w.text yview"
    pack $w.ok -side bottom -padx 3m -pady 3m -ipadx 2m -ipady 1m
    pack $w.scroll -side right -fill y
    pack $w.text -side left -expand yes -fill both
    $w.text insert 0.0 $info
    $w.text mark set insert 0.0

    # Center the window on the screen.

    wm withdraw $w
    update idletasks
    set x [expr [winfo screenwidth $w]/2 - [winfo reqwidth $w]/2 \
	    - [winfo vrootx [winfo parent $w]]]
    set y [expr [winfo screenheight $w]/2 - [winfo reqheight $w]/2 \
	    - [winfo vrooty [winfo parent $w]]]
    wm geom $w +$x+$y
    wm deiconify $w
}

########################################################################

set dp_RPROCtable(null) {};

proc dp_RPROC {name arguments body} {

  # RPROC defines a procedure that is callable by RPC clients;
  # RPROC is semantic sugar.
  #
  proc $name $arguments $body;

  # Record the dp_RPROC in the dp_RPROCtable;
  #
  global dp_RPROCtable;
  set dp_RPROCtable($name) $name;
}

########################################################################
#
# auto_load_all
#
# This procedure source's all Tcl library scripts not already source'd.
# This procedure is useful for when you want to later undefine
# the "proc" command, for making your interpreter RPC safe.
#

proc auto_load_all {} {
  global auto_index;

  set catchout {}
  set noitcl 1
  #
  # The next info command will either trigger an error or return
  # a null string if itcl has not been initialized.  Either way,
  # catchout retains the null string when itcl is not present.
  #
  catch {info namespace all itcl} catchout

  #
  # If the info command returned the string 'itcl' then
  # itcl is present.
  #
  if {[string compare $catchout "itcl"] == 0} {
    set noitcl 0
  }
  
  if {$noitcl} {
    foreach name [array names auto_index] {
      if {[string length [info commands $name]] == 0} {
	uplevel #0 $auto_index($name);
      }
    }
  } else {
    foreach name [array names auto_index] {
      set namespace [info namespace qualifiers $name]
      set cmd [info namespace tail $name]
      if {[string length [namespace $namespace "info commands $cmd"]] == 0} {
	uplevel #0 $auto_index($name)
      }
    }
  }
}

