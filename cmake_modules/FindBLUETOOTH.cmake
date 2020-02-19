# - Try to find the bluez options processing library
# The module will set the following variables
#
#  BLUETOOTH_FOUND - System has bluez
#  BLUETOOTH_INCLUDE_DIR - The bluetooth include directory
#  BLUETOOTH_LIBRARY - The libraries needed to use bluetooth

# use pkg-config to get the directories and then use these values
# in the FIND_PATH() and FIND_LIBRARY() calls

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
  pkg_search_module(PC_BLUETOOTH QUIET bluetooth)
endif ()

# Find the include directories
FIND_PATH(BLUETOOTH_INCLUDE_DIR
    NAMES bluetooth/hci.h
    HINTS
          ${PC_BLUETOOTH_INCLUDEDIR}
          ${PC_BLUETOOTH_INCLUDE_DIRS}
    DOC "Path containing the hci.h include file"
    )

FIND_LIBRARY(BLUETOOTH_LIBRARY
    NAMES bluetooth
    HINTS
          ${PC_BLUETOOTH_LIBRARYDIR}
          ${PC_BLUETOOTH_LIBRARY_DIRS}
    DOC "bluetooth library path"
    )

include(FindPackageHandleStandardArgs)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(BLUETOOTH
  REQUIRED_VARS BLUETOOTH_INCLUDE_DIR BLUETOOTH_LIBRARY
  VERSION_VAR PC_BLUETOOTH_VERSION)

MARK_AS_ADVANCED(BLUETOOTH_INCLUDE_DIR BLUETOOTH_LIBRARY)
