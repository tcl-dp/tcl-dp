package require dp 4.0

puts "Enter hostname of server:"
set host localhost
set server [dp_MakeRPCClient $host 4544]

proc DoCmd {args} {
    global server
    eval dp_RDO $server BroadCast $args
}

dp_RDO $server JoinGroup 

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
.menubar.object.menu add command -label "Clear" -command "DoCmd Clear"
.menubar.object.menu add command -label "Circle" -command "DoCmd CreateCircle"

# Create canvas
canvas .c -background blue
pack .c -fill both

proc CreateRect {x y} {
    DoCmd .c create rectangle $x $y $x $y -width 4 -outline white
}

proc CreateCircle {} {
    set i [.c create oval 150 150 170 170 -fill skyblue]
    .c bind $i <Any-Enter> "DoCmd .c itemconfig $i -fill red"
    .c bind $i <Any-Leave> "DoCmd .c itemconfig $i -fill SkyBlue2"
    .c bind $i <3> "DoCmd plotDown .c $i %x %y"
    .c bind $i <B3-Motion> "DoCmd plotMove .c $i %x %y"
}

proc Clear {} {.c delete all}

proc plotDown {w item x y} {
    global plot
    $w raise $item
    set plot(lastX) $x
    set plot(lastY) $y
}

proc plotMove {w item x y} {
    global plot
    $w move $item [expr $x-$plot(lastX)] [expr $y-$plot(lastY)]
    set plot(lastX) $x
    set plot(lastY) $y
}

bind .c <B1-Motion> {CreateRect %x %y}


