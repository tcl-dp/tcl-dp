# ldelete.tcl --
#
# This file contains a list element deletion utility procedure.
# It is used in the whiteboard example.
#
# ldelete 	- search a list for an item and delete the first one found;
#		- returns new list;
#
# 	example: ldelete {a b c a} a ==> b c a
#

proc ldelete {list elt} {
  set index [lsearch $list $elt];
  if {$index >= 0} {
    return [lreplace $list $index $index];
  }
  return $list;
}




