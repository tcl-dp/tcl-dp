#########################################
#
# This is the file transfer server
#
# (Not too impressive, huh?)
#
#       mperham 3/97
#
package forget dp
package require dp 4.0

# set dataPort 19742

proc fserver {{port 19743}} {
    dp_MakeRPCServer $port
}

# call procedure fserver
# You'll need a vwait here if you want to run this in a tclsh.
# vwait forever



