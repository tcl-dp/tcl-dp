wm grid . 1 1 1 1             

# Create menu bar:
frame .menubar -relief ridge
menubutton .menubar.file -text "File" -menu .menubar.file.menu
pack .menubar.file -side left
menubutton .menubar.object -text "Objects" -menu .menubar.object.menu
pack .menubar.object -side left
pack .menubar -side top -fill both

menu .menubar.file.menu
.menubar.file.menu add command -label "Exit"  -command exit

menu .menubar.object.menu
.menubar.object.menu add command -label "Clear" -command "Clear"
.menubar.object.menu add command -label "Circle" -command "CreateCircle"

# Create canvas
canvas .c -background green
pack .c -fill both

proc CreateRect {x y} {
    .c create rectangle $x $y $x $y -width 4 -outline white
}

proc CreateCircle {} {
    set i [.c create oval 150 150 170 170 -fill skyblue]
    .c bind $i <Any-Enter> ".c itemconfig $i -fill red"
    .c bind $i <Any-Leave> ".c itemconfig $i -fill SkyBlue2"
    .c bind $i <2> "PlotDown .c $i %x %y"
    .c bind $i <B2-Motion> "PlotMove .c $i %x %y"
}

proc Clear {} {.c delete all}

proc PlotDown {w item x y} {
    global plot
    $w raise $item
    set plot(lastX) $x
    set plot(lastY) $y
}

proc PlotMove {w item x y} {
    global plot
    $w move $item [expr $x-$plot(lastX)] [expr $y-$plot(lastY)]
    set plot(lastX) $x
    set plot(lastY) $y
}

bind .c <B1-Motion> {CreateRect %x %y}
