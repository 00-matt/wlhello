# SPDX-FileCopyrightText: 2024 Matthew Smith <matthew@matthew.as>
# SPDX-License-Identifier: GPL-3.0-or-later

#[=======================================================================[.rst:
FindXkbcommon
-------------

Try to find libxkbcommon.

IMPORTED Targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` targets:

``Xkbcommon::xkbcommon``
  xkbcommon native library.

Result Variables
^^^^^^^^^^^^^^^^

``Xkbcommon_FOUND``
  True if libxkbcommon has been found.
``Xkbcommon_VERSION_STRING``
  Version number of found libxkbcommon.
``Xkbcommon_LIBRARIES``
  Libraries required to use libxkbcommon.
``Xkbcommon_INCLUDE_DIRS``
  Path to libxkbcommon headers.

#]=======================================================================]

include(FindPackageHandleStandardArgs)

find_package(PkgConfig QUIET)

if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_Xkbcommon QUIET xkbcommon)
endif()

if(PC_Xkbcommon_FOUND AND NOT Wayland_VERSION_STRING)
  set(Xkbcommon_VERSION_STRING ${PC_Xkbcommon_VERSION})
endif()

find_path(Xkbcommon_INCLUDE_DIR
  NAMES xkbcommon/xkbcommon.h
  HINTS ${PC_Xkbcommon_INCLUDE_DIR})
mark_as_advanced(Xkbcommon_INCLUDE_DIR)

if(NOT Xkbcommon_LIBRARY)
  find_library(Xkbcommon_LIBRARY
    NAMES xkbcommon
    HINTS ${PC_Xkbcommon_LIBRARY_DIRS})
  mark_as_advanced(Xkbcommon_LIBRARY)
endif()

find_package_handle_standard_args(Xkbcommon
  FOUND_VAR Xkbcommon_FOUND
  REQUIRED_VARS Xkbcommon_INCLUDE_DIR Xkbcommon_LIBRARY
  VERSION_VAR Xkbcommon_VERSION_STRING)

if(Xkbcommon_FOUND AND NOT TARGET Xkbcommon::xkbcommon)
  add_library(Xkbcommon::xkbcommon UNKNOWN IMPORTED)
  set_target_properties(Xkbcommon::xkbcommon PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${Xkbcommon_INCLUDE_DIR}"
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    IMPORTED_LOCATION "${Xkbcommon_LIBRARY}")
endif()
