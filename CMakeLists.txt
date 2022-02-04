# Cmake CMakeLists.txt control file for Tcl-DP
#
# John McGehee, Voom, Inc.   7/21/2010

cmake_minimum_required(VERSION 2.8)
cmake_policy(VERSION 2.8)

# The project command sets variable PROJECT_NAME
project(dp)

# Project name and version
set(DP_MAJOR_VERSION 4)
set(DP_MINOR_VERSION 2)
set(DP_VERSION "${DP_MAJOR_VERSION}.${DP_MINOR_VERSION}")
set(DP_LIB_NAME "${PROJECT_NAME}${DP_MAJOR_VERSION}${DP_MINOR_VERSION}")
set(DP_LIB_FILE ${CMAKE_SHARED_LIBRARY_PREFIX}${DP_LIB_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX})
# DP_API_EXAMPLE_FILE has to be defined in this CMakeLists.txt so it will get into tests/testconfig.tcl
set(DP_API_EXAMPLE_FILE "dpApiExample${CMAKE_EXECUTABLE_SUFFIX}")
set(DP_LIBRARY_DIRECTORY "${PROJECT_BINARY_DIR}/library")
# TODO: Set the channel number according to the Tcl version
set(DP_CHANNEL_VERSION TCL_CHANNEL_VERSION_2)

if (TCL_HOME)
    # The user specified a Tcl other than the one that the
    # system knows about.  This is quite common.
    message("Checking Tcl location TCL_HOME='${TCL_HOME}' specified by the user.")

    if (NOT IS_DIRECTORY ${TCL_HOME})
        message(FATAL_ERROR "User-specified TCL_HOME '${TCL_HOME}' is not a directory.")
    endif ()
    
    # Set up FindTCL to find TCL_LIBRARY and TCL_TCLSH in TCL_HOME
    set(CMAKE_PREFIX_PATH ${TCL_HOME})
endif ()

# FindTclStub includes FindTCL
include(FindTclStub)

if (NOT TK_FOUND)
    # Tell the user that the "Could NOT find TK" message is okay
    message("${PROJECT_NAME} does not use Tk, so you can ignore the above 'Could NOT find TK' messages.")
endif ()
if (NOT TCL_FOUND)
    message("No Tcl was found.  Please download and install Tcl (Tk is not required).")
    message("You can specify a Tcl other than the default system Tcl")
    message(FATAL_ERROR "using the '-DTCL_HOME:PATH=/path/to/tcl' option.")
endif ()

if (TCL_HOME)
    # Make sure that FindTCL found Tcl inside TCL_HOME (Cmake bug 11033).
    # TODO: This automatic detection of the problem is getting monstrous.
    #       Perhaps we ought to delete this and just explain it in the README file.
    message("Checking to make sure the Tcl found is within the user-specified TCL_HOME.")
    get_filename_component(TCL_HOME_REALPATH ${TCL_HOME} REALPATH)

	# Get the Tcl home directory according to TCL_LIBRARY found by FindTCL
    get_filename_component(TCL_LIBRARY_HOME ${TCL_LIBRARY} REALPATH)
    get_filename_component(TCL_LIBRARY_HOME ${TCL_LIBRARY_HOME} PATH)
    get_filename_component(TCL_LIBRARY_HOME ${TCL_LIBRARY_HOME} PATH)
    message("According to FindTCL TCL_LIBRARY, using the Tcl installed within '${TCL_LIBRARY_HOME}'.")

	# Get Tcl home directory according to TCL_TCLSH found by FindTCL
    get_filename_component(TCL_TCLSH_HOME ${TCL_TCLSH} REALPATH)
    get_filename_component(TCL_TCLSH_HOME ${TCL_TCLSH_HOME} PATH)
    get_filename_component(TCL_TCLSH_HOME ${TCL_TCLSH_HOME} PATH)
    message("According to FindTCL TCL_TCLSH, using the Tcl installed within '${TCL_TCLSH_HOME}'.")

	if (TCL_STUB_LIBRARY)
		# Get Tcl home directory according to TCL_STUB_LIBRARY found by FindTclStub
    	get_filename_component(TCL_STUB_LIBRARY_HOME ${TCL_STUB_LIBRARY} REALPATH)
    	get_filename_component(TCL_STUB_LIBRARY_HOME ${TCL_STUB_LIBRARY_HOME} PATH)
    	get_filename_component(TCL_STUB_LIBRARY_HOME ${TCL_STUB_LIBRARY_HOME} PATH)
    	message("According to FindTclStub TCL_STUB_LIBRARY, using the Tcl installed within '${TCL_STUB_LIBRARY_HOME}'.")
	endif ()

    if (NOT  (${TCL_LIBRARY_HOME}      STREQUAL ${TCL_HOME_REALPATH}) OR
        NOT  (${TCL_TCLSH_HOME}        STREQUAL ${TCL_HOME_REALPATH}) OR
		# Bug: This does not properly handle the case where there is no tclstub library
        #      in TCL_HOME, but there is one in the default system installation
		(TCL_STUB_LIBRARY AND
             NOT (${TCL_STUB_LIBRARY_HOME} STREQUAL ${TCL_HOME_REALPATH})))
        message("As a workaround to CMake bug 11033, you must create symbolic links in TCL_HOME.")
        if (CMAKE_HOST_WIN32)
            # Windows
            message("For example, suppose you had specified TCL_HOME=\"C:\\Program Files\\tcl8.4.19\":")
            message("  cd \"C:\\Program Files\\tcl8.4.19\\lib\"")
            message("  mklink tcl.dll tcl8.4.dll")
			# No stub library for Windows
            message("  cd \"C:\\Program Files\\tcl8.4.19\\bin\"")
            message("  mklink tclsh.exe tclsh84.exe")
        else ()
            # UNIX
            message("For example, suppose you had specified TCL_HOME=/home/voom/tcl8.4.19:")
            message("  cd /home/voom/tcl8.4.19/lib")
            message("  ln -s libtcl8.4.so libtcl.so")
			if (TCL_STUB_LIBRARY)
                message("  ln -s libtclstub8.4.a libtclstub.a")
            endif ()
            message("  cd /home/voom/tcl8.4.19/bin")
            message("  ln -s tclsh8.4 tclsh")
        endif ()
        message(FATAL_ERROR "See http://www.cmake.org/Bug/view.php?id=11033 for details.")
    endif ()
endif ()

include_directories(${TCL_INCLUDE_PATH} ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})

# Source files common to all target platforms
set(GENERIC_SOURCE_FILES
    generic/dpChan.c
    generic/dpCmds.c
    generic/dpSock.c
    generic/dpSerial.c
    generic/dpRPC.c
    generic/dpInit.c
    generic/dpIdentity.c
    generic/dpPlugF.c
    generic/dpFilters.c
    generic/dpPackOff.c
)

if (CMAKE_HOST_UNIX)
    message("Configuring for UNIX target.")
    
    add_library(${DP_LIB_NAME} SHARED
	${GENERIC_SOURCE_FILES}
	unix/dpInit.c
	unix/dpSerial.c
	unix/dpSock.c
	unix/dpEmail.c
	unix/dpLocks.c
	unix/dpUnixIpm.c
	unix/dpUnixTcp.c
	unix/dpUnixUdp.c
    )

    if (TCL_STUB_LIBRARY)
	message("For portability among Tcl versions, linking to Tcl stub library '${TCL_STUB_LIBRARY}'.")
	target_link_libraries(${DP_LIB_NAME} ${TCL_STUB_LIBRARY})
    else ()
	message("Linking to shared library '${TCL_LIBRARY}'.")
	target_link_libraries(${DP_LIB_NAME} wsock32 ${TCL_LIBRARY})
    endif ()

    # Compiler options for gcc
    #set(CMAKE_C_FLAGS                "-Wall")
    #set(CMAKE_C_FLAGS_DEBUG          "-g -ggdb -DDEBUG")
    #set(CMAKE_C_FLAGS_RELEASE        "-O2 -DNDEBUG")
    #set(CMAKE_C_FLAGS_MINSIZEREL     "-O3 -Os -DNDEBUG")
    #set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
    
elseif (CMAKE_HOST_WIN32)
    message("Configuring for Windows target.")
    
    add_library(${DP_LIB_NAME} SHARED
	${GENERIC_SOURCE_FILES}
	win/dpAppInit.c
	win/dpWinInit.c
	win/dpWinSock.c
	win/dpWinSerial.c
	win/dpWinIPM.c
	win/dpWinTcp.c
	win/dpWinUDP.c
    )

    # The Tcl stub library did not work with Windows
    message("Linking to shared library '${TCL_LIBRARY}'.")
    target_link_libraries(${DP_LIB_NAME} wsock32 ${TCL_LIBRARY})

    # Compiler options for Visual Studio
    #if(MSVC)
        # C
        #set(CMAKE_C_FLAGS_DEBUG          "/D_DEBUG /W3 /Wall /MTd /Zi /Ob0 /Od /RTC1")
        #set(CMAKE_C_FLAGS_RELEASE        "/DNDEBUG /W3 /Wall /MT /O2 /Og /Ob2/GL")
        #set(CMAKE_C_FLAGS_MINSIZEREL     "/DNDEBUG /W3 /Wall /MT /Ox /GL")
        #set(CMAKE_C_FLAGS_RELWITHDEBINFO "/DNDEBUG /W3 /Wall /MT /ZI /O2 /Ob1 /GL")
    #endif ()
    
else ()
    message(FATAL_ERROR "Unsupported platform '${CMAKE_HOST_SYSTEM_NAME}'.")
endif ()


### Configuration Files ###
# Put configuration() file at the end of the build after all variables are set.
set(DP_TCL_LIB_FILES
    library/acl.tcl
    library/distribObj.tcl
    library/dp_atclose.tcl
    library/dp_atexit.tcl
    library/ldelete.tcl
    library/oo.tcl
    library/rpc.tcl)
# DP_TCL_LIB_FILES_TCLLIST is DP_TCL_LIB_FILES formatted as
# a Tcl list: {{libary/foo.tcl} {library/bar.tcl} ...}
set(DP_TCL_LIB_FILES_TCLLIST "{")
foreach (DP_TCL_LIB_FILE ${DP_TCL_LIB_FILES})
    # Build space delimited version of DP_TCL_LIB_FILES
    set(DP_TCL_LIB_FILES_TCLLIST "${DP_TCL_LIB_FILES_TCLLIST} {${DP_TCL_LIB_FILE}}")
    # DP 4.2: Because configure_file() is used to copy DP_TCL_LIB_FILES,
    # CMake variable references would work in DP_TCL_LIB_FILES, but
    # please only add CMake variable references to pkgIndex.tcl.in.
    configure_file(
        "${PROJECT_SOURCE_DIR}/${DP_TCL_LIB_FILE}"
        "${PROJECT_BINARY_DIR}/${DP_TCL_LIB_FILE}"
    )
endforeach ()
set(DP_TCL_LIB_FILES_TCLLIST "${DP_TCL_LIB_FILES_TCLLIST} }")

# Configure the Tcl package file to pass some of the CMake settings
# to the Tcl source code.
configure_file(
    "${PROJECT_SOURCE_DIR}/generic/pkgIndex.tcl.in"
    "${PROJECT_BINARY_DIR}/pkgIndex.tcl"
)

# Configure a Tcl file to pass some of the CMake settings to the tcltest
# test harness.
configure_file(
    "${PROJECT_SOURCE_DIR}/tests/testconfig.tcl.in"
    "${PROJECT_BINARY_DIR}/tests/testconfig.tcl"
)

# Configure a header file to pass some of the CMake settings
# to the C source code.
# The cpp macro USE_TCL_STUBS appears in dpconfig.h.in
set(USE_TCL_STUBS ${TCL_STUB_LIBRARY})
configure_file(
    "${PROJECT_SOURCE_DIR}/generic/dpconfig.h.in"
    "${PROJECT_BINARY_DIR}/generic/dpconfig.h"
)


### Installation ###
install(TARGETS ${DP_LIB_NAME} DESTINATION ${DP_LIB_NAME}/)
install(FILES ${PROJECT_BINARY_DIR}/pkgIndex.tcl DESTINATION ${DP_LIB_NAME}/)
install(FILES ${DP_TCL_LIB_FILES} DESTINATION ${DP_LIB_NAME}/library/)
# Install HTML documentation
install(DIRECTORY doc DESTINATION ${DP_LIB_NAME}
    FILES_MATCHING PATTERN doc/*.html
    PATTERN CVS EXCLUDE
    PATTERN man[0-9n] EXCLUDE
)
if (CMAKE_HOST_UNIX)
    # Install UNIX man pages
    install(DIRECTORY doc/ DESTINATION share/man
        FILES_MATCHING PATTERN doc/man[0-9n]/*.[0-9n]
        PATTERN CVS EXCLUDE
    )
endif ()


### Unit tests ###
enable_testing()
    
add_test(connect
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file connect.test
)
add_test(copy
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file copy.test
)
# On UNIX, the email test fails.  Windows has no email channel.
add_test(email_expected_to_fail
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file email.test
)
add_test(identity
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file identity.test
)
# The IP multicast test blocks forever waiting for a message that never arrives
#add_test(ipm_expected_to_fail
#    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file ipm.test
#)
add_test(netinfo
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file netinfo.test
)
add_test(plugin
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file plugin.test
)
add_test(plugin2
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file plugin2.test
)
add_test(rpc
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file rpc.test
)
# The serial test requires hardware set up, so it fails
add_test(serial_expected_to_fail
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file serial.test
)
# The ser_xmit test requires hardware set up, so it fails
add_test(ser_xmit_expected_to_fail
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file ser_xmit.test
)
add_test(tcp
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file tcp.test
)
add_test(udp
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file udp.test
)
add_test(xmit
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file xmit.test
)
# Test the C-API in api/
add_test(api
    ${TCL_TCLSH} ${PROJECT_SOURCE_DIR}/tests/all.tcl -file api.test
)


# If you want to run the tests that are commented out above,
# (like ipm), add the respective names here.
# TEST PROPERTY ENVIRONMENT requires CMake 2.8.
set_property(TEST
    api connect copy email_expected_to_fail identity
    netinfo plugin2 plugin rpc serial_expected_to_fail 
    ser_xmit_expected_to_fail tcp udp xmit
    PROPERTY ENVIRONMENT
    "LD_LIBRARY_PATH=${TCL_LIBRARY_PATH}:${DP_LIB_PATH}"
)


### The Tcl-DP C-API ###
add_subdirectory(api)
