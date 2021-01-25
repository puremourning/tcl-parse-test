# This is boilerplate to use the latest policies available
# Info:
# https://cliutils.gitlab.io/modern-cmake/chapters/basics.html#minimum-version
cmake_minimum_required(VERSION 3.7...3.19)
if ( ${CMAKE_VERSION} VERSION_LESS 3.12 )
    cmake_policy( VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} )
endif()
# End.

project( TclAnalyzer
  VERSION 1.0
  DESCRIPTION "TCL Language Server"
  LANGUAGES CXX
)

# All of this project uses c++17 and we certainly don't want random extensions
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find TCL
find_package(TCL 8.6 REQUIRED)

# Build debug by default
set( DEFAULT_BUILD_TYPE "Debug")
if ( NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES )
  message( STATUS
    "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified."
  )
  set( CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE
    STRING "Choose the type of build."
    FORCE
  )

  # Set the possible values of build type for cmake-gui
  set_property( CACHE CMAKE_BUILD_TYPE
    PROPERTY
      STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo"
  )
endif()

# Controlling the flags for debug/release is per-config, not per-target (and
# necessarily per-compiler)
if ( NOT MSVC )
  set( CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fno-omit-frame-pointer" )
  set( CMAKE_CXX_FLAGS_RELEASE "-g -O2" )
endif()

# add the executable
add_subdirectory(src)
