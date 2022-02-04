proc Dp-4.0:Init { dir } {
     load [file join $dir tcl-dp40.dll] Dp
     }
package ifneeded Dp 4.0 [list Dp-4.0:Init $dir]
