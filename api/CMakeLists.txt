# Cmake CMakeLists.txt control file for Tcl-DP dpApi.
#
# In actual usage, you just add dpApi.c and dpApi.h to your main
# application's build.  The purpose of this build is to compile
# and run the test program.
#
# John McGehee, Voom, Inc.   8/10/2010

# This is a sub-CMakeLists.txt file.


include_directories(${PROJECT_SOURCE_DIR}/api ${PROJECT_BINARY_DIR}/api)

add_executable(dpApiExample
    dpApi.c
    dpApiExample.c
)
    
if (CMAKE_HOST_UNIX)
    message("Configuring for UNIX target.")
    
elseif (CMAKE_HOST_WIN32)
    message("Configuring for Windows target.")
    target_link_libraries(dpApiExample wsock32)

else ()
    message(FATAL_ERROR "Unsupported platform '${CMAKE_HOST_SYSTEM_NAME}'.")
endif ()


### Unit tests ###
enable_testing()

# Configure a Tcl file to pass some of the CMake settings to the tcltest
# test harness.
# This is kind of bogus because ../CMakeLists.txt also makes an identical
# ../tests/testconfig.tcl, but tests/all.tcl needs a ./tests/testconfig.tcl.
configure_file(
    "${PROJECT_SOURCE_DIR}/tests/testconfig.tcl.in"
    "${PROJECT_BINARY_DIR}/api/tests/testconfig.tcl"
)
