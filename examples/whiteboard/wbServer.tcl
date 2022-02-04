#package require dp 4.0

dp_MakeRPCServer 2002

set files {}
set log {}

proc JoinGroup {} {
    global dp_rpcFile files log
    lappend files $dp_rpcFile
    foreach cmd $log {
	eval dp_RDO $dp_rpcFile $cmd
    }
    dp_atclose $dp_rpcFile append "dp_Leave $dp_rpcFile"
}

proc dp_Leave {file} {
    global files
    set files [ldelete $files $file]
}

proc BroadCast {args} {
    global files log
    lappend log $args
    foreach i $files {
	eval "dp_RDO $i $args"
    }
}


