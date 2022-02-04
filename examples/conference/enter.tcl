
#
# enter.tcl -- Tcl/Tk script for conference client.
#

# Get information from user.
#

package require dp 4.0

set host localhost;
set port 9943;
set name Bubba

# Become an RPC client of the conference server.  Store the
# file handle that represents the RPC connection in the 
# global variable server;
#

set server [dp_MakeRPCClient $host $port]

# The conference server will occasionally RPC a Hear 
# command to us when we need to hear a message from some speaker.
#

proc Hear {speaker message} \
{
  puts stdout "$speaker --> $message";
}

# Commands available to user, who invokes these commands from stdin.
#

proc help {} \
{
  puts stdout "Conferencing Commands: ";
  puts stdout "\thelp";
  puts stdout "\twho";
  puts stdout "\tsay ?message?";
  puts stdout "\tbye or quit or leave";
}

proc who {} \
{
  global server;

  # RPC to the conference server to get the list of all its client names;
  #
  puts stdout [dp_RPC $server set names];
}

proc say {args} \
{
  global server;

  # Tell the conference server, by RPC, what I want to said to all clients.
  # The conference server will repeat my message to all clients, by RPC,
  # for them to Hear.
  #
  dp_RPC $server Say $args;
}

# To leave, we just exit, and the file is automatically closed.  The
# conference server will automatically clen up on his end.
proc leave {} {exit}
proc bye {} {exit};
proc quit {} {exit}

# On startup, automatically enter the conference.
#

proc enter {} \
{
  global server;
  global name;

  # Tell the conference server, by RPC, that I'm Entering the conference.
  #
  dp_RPC $server Enter $name;
  puts stdout "Entered conference.";
}

enter;


