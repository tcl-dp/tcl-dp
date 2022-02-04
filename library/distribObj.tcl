# distribObj.tcl --
#
# This file contains utilities to manage distributed objects.
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

################################################################
#
# Support for triggers is kept in the trigger array.  This array
# is indexed on object name and slot.  The array stores a list of
# commands
#
proc dp_AppendTrigger {beforeAfter obj slot args} {
    global trigger
    if {[lsearch "before after" $beforeAfter] != -1} {
	lappend trigger($obj,$slot,$beforeAfter) $args
    } else {
	error "bad option \"$beforeAfter\": should be \"before\" or \"after\""
    }
}

proc dp_AppendTriggerUnique {beforeAfter obj slot args} {
    global trigger
    if {[lsearch "before after" $beforeAfter] != -1} {
	if {[info exists trigger($obj,$slot,$beforeAfter)] &&
	    ([lsearch $trigger($obj,$slot,$beforeAfter) $args] == -1) &&
	    ([lsearch $trigger($obj,$slot,$beforeAfter) [list $args]] == -1)} {
	    lappend trigger($obj,$slot,$beforeAfter) $args
	}
    } else {
	error "bad option \"$beforeAfter\": should be \"before\" or \"after\""
    }
}

proc dp_SetTrigger {beforeAfter obj slot args} {
    global trigger
    if {[lsearch "before after" $beforeAfter] != -1} {
	set trigger($obj,$slot,$beforeAfter) [list $args]
    } else {
	error "bad option \"$beforeAfter\": should be \"before\" or \"after\""
    }
}

proc dp_ReleaseTrigger {beforeAfter obj slot args} {
    global trigger
    if {[lsearch "before after" $beforeAfter] != -1} {
	set trigger($obj,$slot,$beforeAfter)  \
		[ldelete $trigger($obj,$slot,$beforeAfter) $args]
    } else {
	error "bad option \"$beforeAfter\": should be \"before\" or \"after\""
    }
}

proc dp_ClearTriggers {beforeAfter obj slot} {
    global trigger
    if {[lsearch "before after" $beforeAfter] != -1} {
	catch {unset trigger($obj,$slot,$beforeAfter)}
    } else {
	error "bad option \"$beforeAfter\": should be \"before\" or \"after\""
    }
}

proc dp_GetTriggers {beforeAfter obj slot} {
    global trigger
    if {[lsearch "before after" $beforeAfter] != -1} {
	if [info exists trigger($obj,$slot,$beforeAfter)] {
	    return $trigger($obj,$slot,$beforeAfter);
	} else {
	    return {};
	}
    } else {
	error "bad option \"$beforeAfter\": should be \"before\" or \"after\""
    }
}

################################################################
#
# Objects are distributed using the dp_RPC mechanism.  The distribution
# information is kept in the array distObjInfo, which index on the object
# name and sometimes the process.  For each object, we need
# to know the processes the owner process (or, at least,
# the next step in the path to the owner), the reference count for the
# number of times this object has been distributed to a process, and
# the processes it's been distributed to.  Thus,
#
#	  owner process	  <==	$objInfo($obj,owner)
#      client processes	  <==	$objInfo($obj,clients)
# ref count to $process	  <==	$objInfo($obj,$process)
#

#
# Distribute an object.  Two steps:
#	1. Make up a list of "-slot value" initialization arguments
#	2. Foreach client process, do an RPC to create the object if
#	   it's not over there, otherwise increment the reference
#	   count.  Return an error if the object already exists on
#	   the far end (i.e., because someone else made it).
#

proc dp_DistributeObject {obj processes makeCmd} {
    global objInfo

    # Step 1
    set params {}
    foreach conf [$obj configure] {
	set slot [string trimleft [lindex $conf 0] -]
	lappend params -$slot [lindex $conf 2]
    }

    # Step 2
    foreach proc $processes {
	if {![info exist objInfo($obj,$proc)]} {
	    set robj [dp_RPC $proc info procs $obj]
	    if {[string length $robj] != 0} {
		set err "Error while distributing object \"$obj\"\n"
		append err "Object already exists on $proc"
		error $err
	    }
	    if {[catch {dp_RPC $proc -events rpc -timeout 30000 \
			dp_CreateRemoteObject $makeCmd $obj $params} msg]} {
		error "timeout while creating object $obj on $proc: $msg"
	    }
	    set objInfo($obj,$proc) 0
	    lappend objInfo($obj,clients) $proc
	    dp_atclose $proc prepend "unset objInfo($obj,$proc)"
	} 
	incr objInfo($obj,$proc)
    }
}

#
# Create an object in a process.
#	1. Create the object using the makeCmd
#	2. Record the owner of the object
#	3. Arrange for cleanup if the connection dies.
#
proc dp_CreateRemoteObject {makeCmd obj params} {
    global dp_rpcFile
    global objInfo
    eval $makeCmd $obj $params
    set objInfo($obj,owner) $dp_rpcFile
    dp_atclose $dp_rpcFile prepend "dp_DeleteRemoteObject $obj"
}

#
# Delete a remote object:
#   For each client process:
#	1. Verify that the object has been distributed to the client.
#	   Error if not.
#	2. Decrement the reference count to the client
#	3. If reference count is zero, delete the object in the client
#
proc dp_UndistributeObject {obj processes} {
  global objInfo;

  foreach proc $processes {
      if {![info exists objInfo($obj,$proc)]} {
	  set err "Error while undistributing object \"$obj\"\n"
	  append err "Object not distributed to $proc from this process"
	  error $err
      }
      incr objInfo($obj,$proc) -1
      if {$objInfo($obj,$proc) == 0} {
	  dp_RDO $proc dp_DeleteRemoteObject $obj
          set objInfo($obj,clients) [ldelete $objInfo($obj,clients) $proc];
	  unset objInfo($obj,$proc)
	  dp_atclose $proc delete "unset objInfo($obj,$proc)"
      }
  }
  if {[llength $objInfo($obj,clients)] == 0} {
      unset objInfo($obj,clients)
  }
}

#
# Get names of all slots in object
#
proc dp_SlotNames {obj} {
  set slots {};
  foreach conf [$obj configure] {
      set slot [string trimleft [lindex $conf 0] -];
      lappend slots $slot;
  }
  return $slots
}

#
# Nuke an object.  This cleans up the objInfo array and deletes the
# command from the interpreter.
#	1. Recursively delete the object in all the clients.
#	2. Clean up tables: including object owner and triggers.
#	3. Send the destroy message to the object
#	4 Delete the command from the intepreter.
#
proc dp_DeleteRemoteObject {obj} {
  global objInfo

  if {[info exists objInfo($obj,clients)]} {
      foreach proc $objInfo($obj,clients) {
	  dp_RDO $proc dp_DeleteRemoteObject $obj
	  unset objInfo($obj,$proc)
      }
      unset objInfo($obj,clients)
  }

  catch {unset objInfo($obj,owner)}
  foreach slot [dp_SlotNames $obj] {
      catch {unset trigger($obj,$slot)}
  }

  catch {$obj destroy}
  catch {rename $obj ""}
}

#
# Set a slot's value:
#    If the object was created remotely (i.e., has an owner),
#    then send the change to the owner. Otherwise propagate
#    the change to the clients
#
proc dp_setf {obj args} {
    global objInfo
    if [info exists objInfo($obj,owner)] {
	eval dp_RDO $objInfo($obj,owner) dp_setf $obj $args
    } else {
	eval dp_downsetf $obj $args
    }
}

#
# Part of propagating a slot change to a client.
#
# Should be
#	1. Pass the change on to the clients
#	2. Process all the "before" triggers, ignoring errors.
#	3. Set the slot.
#	4. Process all the "after" triggers, ignoring errors.
#
proc dp_downsetf {obj args} {
    global trigger objInfo

    if [info exists objInfo($obj,clients)] {
	foreach proc $objInfo($obj,clients) {
	    eval dp_RDO $proc dp_downsetf $obj $args
	}
    }

    set len [llength $args]
    for {set i 0} {$i < $len} {incr i} {
	set slot [lindex $args $i]
	incr i
	set value [lindex $args $i]
	if [info exists trigger($obj,$slot,before)] {
	    foreach cmd $trigger($obj,$slot,before) {
		catch {uplevel #0 eval $cmd}
	    }
	}

	$obj configure -$slot $value

	if [info exists trigger($obj,$slot,after)] {
	    foreach cmd $trigger($obj,$slot,after) {
		catch {uplevel #0 eval $cmd}
	    }
	}
    }
}

#
# Get access to a slot.  Just for consistency with setf
#
proc dp_getf {obj slot} {$obj slot-value $slot}


