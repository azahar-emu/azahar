# Source: https://github.com/KDE/ffmpegthumbs/blob/master/cmake/FindFFmpeg.cmake
# Modified for use in the Azahar Emulator project.
# ----------------------------------------------------------------------------------
#
# - Try to find the required ffmpeg components(default: AVFORMAT, AVUTIL, AVCODEC)
#
# Once done this will define
#  FFMPEG_FOUND         - System has the all required components.
#  FFMPEG_INCLUDE_DIRS  - Include directory necessary for using the required components headers.
#  FFMPEG_LIBRARIES     - Link these to use the required ffmpeg components.
#  FFMPEG_DEFINITIONS   - Compiler switches required for using the required ffmpeg components.
#
# For each of the components
#   - AVCODEC
#   - AVFILTER
#   - AVDEVICE
#   - AVFORMAT
#   - AVUTIL
#   - POSTPROCESS
#   - SWSCALE
#   - SWRESAMPLE
# the following variables will be defined:
#  <component>_FOUND        - System has <component>
#  <component>_INCLUDE_DIRS - Include directory necessary for using the <component> headers
#  <component>_LIBRARIES    - Link these to use <component>
#  <component>_DEFINITIONS  - Compiler switches required for using <component>
#  <component>_VERSION      - The components version
#
# As the versions of the various FFmpeg components differ for a given release,
# and CMake supports only one common version for all components, use the
# following to specify exact required versions for multiple components:
#
# find_package(FFmpeg 57.48 COMPONENTS AVCODEC)
# find_package(FFmpeg 57.40 COMPONENTS AVFORMAT)
# find_package(FFmpeg 55.27 COMPONENTS AVUTIL)
#
# SPDX-FileCopyrightText: 2006 Matthias Kretz <kretz@kde.org>
# SPDX-FileCopyrightText: 2008 Alexander Neundorf <neundorf@kde.org>
# SPDX-FileCopyrightText: 2011 Michael Jansen <kde@michael-jansen.biz>
# SPDX-FileCopyrightText: 2021 Stefan Brüns <stefan.bruens@rwth-aachen.de>
# SPDX-License-Identifier: BSD-3-Clause

include(FindPackageHandleStandardArgs)

# The default components were taken from a survey over other FindFFMPEG.cmake files
if (NOT FFmpeg_FIND_COMPONENTS)
  set(FFmpeg_FIND_COMPONENTS AVCODEC AVFORMAT AVUTIL)
endif ()

list(LENGTH FFmpeg_FIND_COMPONENTS _numComponents)
if ((${_numComponents} GREATER 1) AND DEFINED ${FFmpeg_FIND_VERSION})
  message(WARNING "Using a required version in combination with multiple COMPONENTS is not supported")
  set(_FFmpeg_REQUIRED_VERSION 0)
elseif (DEFINED FFmpeg_FIND_VERSION)
  set(_FFmpeg_REQUIRED_VERSION ${FFmpeg_FIND_VERSION})
else ()
  set(_FFmpeg_REQUIRED_VERSION 0)
endif ()
set(_FFmpeg_ALL_COMPONENTS AVCODEC AVDEVICE AVFORMAT AVUTIL POSTPROC SWSCALE SWRESAMPLE AVFILTER)

#
### Macro: set_component_found
#
# Marks the given component as found if both *_LIBRARIES AND *_INCLUDE_DIRS is present.
#
macro(set_component_found _component )
  if (${_component}_LIBRARIES AND ${_component}_INCLUDE_DIRS)
    set(${_component}_FOUND TRUE)
    set(FFmpeg_${_component}_FOUND TRUE)
  endif ()
endmacro()

#
### Macro: find_component
#
# Checks for the given component by invoking pkgconfig and then looking up the libraries and
# include directories.
#
macro(find_component _component _pkgconfig _library _header)

  if (NOT WIN32)
     # use pkg-config to get the directories and then use these values
     # in the FIND_PATH() and FIND_LIBRARY() calls
     find_package(PkgConfig)
     if (PKG_CONFIG_FOUND)
       pkg_check_modules(PC_${_component} QUIET ${_pkgconfig})
     endif ()
  endif (NOT WIN32)

  find_path(${_component}_INCLUDE_DIRS ${_header}
    HINTS
      ${PC_${_component}_INCLUDEDIR}
      ${PC_${_component}_INCLUDE_DIRS}
    PATH_SUFFIXES
      ffmpeg
  )

  find_library(${_component}_LIBRARIES NAMES ${_library}
      HINTS
      ${PC_${_component}_LIBDIR}
      ${PC_${_component}_LIBRARY_DIRS}
  )

  set(${_component}_DEFINITIONS  ${PC_${_component}_CFLAGS_OTHER} CACHE STRING "The ${_component} CFLAGS.")
  set(${_component}_VERSION      ${PC_${_component}_VERSION}      CACHE STRING "The ${_component} version number.")

  set_component_found(${_component})

  mark_as_advanced(
    ${_component}_INCLUDE_DIRS
    ${_component}_LIBRARIES
    ${_component}_DEFINITIONS
    ${_component}_VERSION)

endmacro()

# Check for cached results. If there are skip the costly part.
if (NOT FFMPEG_LIBRARIES)

  # Check for all possible component.
  find_component(AVCODEC     libavcodec     avcodec     libavcodec/avcodec.h)
  find_component(AVFILTER    libavfilter    avfilter    libavfilter/avfilter.h)
  find_component(AVFORMAT    libavformat    avformat    libavformat/avformat.h)
  find_component(AVDEVICE    libavdevice    avdevice    libavdevice/avdevice.h)
  find_component(AVUTIL      libavutil      avutil      libavutil/avutil.h)
  find_component(SWSCALE     libswscale     swscale     libswscale/swscale.h)
  find_component(SWRESAMPLE  libswresample  swresample  libswresample/swresample.h)
  find_component(POSTPROC    libpostproc    postproc    libpostproc/postprocess.h)

  # Check if the required components were found and add their stuff to the FFMPEG_* vars.
  foreach (_component ${_FFmpeg_ALL_COMPONENTS})
    if (${_component}_FOUND)
      set(FFMPEG_LIBRARIES   ${FFMPEG_LIBRARIES}   ${${_component}_LIBRARIES})
      set(FFMPEG_DEFINITIONS ${FFMPEG_DEFINITIONS} ${${_component}_DEFINITIONS})
      list(APPEND FFMPEG_INCLUDE_DIRS ${${_component}_INCLUDE_DIRS})
    endif ()
  endforeach ()

  # Build the include path with duplicates removed.
  if (FFMPEG_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)
  endif ()

  # cache the vars.
  set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS} CACHE STRING "The FFmpeg include directories." FORCE)
  set(FFMPEG_LIBRARIES    ${FFMPEG_LIBRARIES}    CACHE STRING "The FFmpeg libraries." FORCE)
  set(FFMPEG_DEFINITIONS  ${FFMPEG_DEFINITIONS}  CACHE STRING "The FFmpeg cflags." FORCE)

  mark_as_advanced(FFMPEG_INCLUDE_DIRS
                   FFMPEG_LIBRARIES
                   FFMPEG_DEFINITIONS)

else ()
  # Set the noncached _FOUND vars for the components.
  foreach (_component ${_FFmpeg_ALL_COMPONENTS})
    set_component_found(${_component})
  endforeach ()
endif ()

# Compile the list of required vars
unset(_FFmpeg_REQUIRED_VARS)
set(_FFmpeg_FOUND_LIBRARIES "")
foreach (_component ${FFmpeg_FIND_COMPONENTS})
  if (${_component}_FOUND)
    if (NOT WIN32)
      string(REGEX MATCH "^([0-9]+)" _component_version_major "${${_component}_VERSION}")
      if (NOT ${_component}_VERSION)
        message(WARNING "${_component}: Unable to check component version, using anyway...")
      elseif (
        (NOT (_component_version_major EQUAL _FFmpeg_REQUIRED_VERSION)) AND
        (NOT (_FFmpeg_REQUIRED_VERSION EQUAL 0))
      )
          message(FATAL_ERROR
            "${_component}: ${${_component}_VERSION} != ${_FFmpeg_REQUIRED_VERSION}\n"
            "${_component} version ${_FFmpeg_REQUIRED_VERSION}.x *exactly* is required.\n"
            "Check your FFmpeg version, and be sure to clean the build directory before retrying.\n"
            "If building with FFmpeg is impractical, you can disable it by setting ENABLE_FFMPEG to OFF.")
      endif ()
    else (NOT WIN32)
      message(WARNING "${_component}: Version check is not supported on Windows, using anyway...")
    endif(NOT WIN32)
    list(APPEND _FFmpeg_FOUND_LIBRARIES ${${_component}_LIBRARIES})
  endif ()
  list(APPEND _FFmpeg_REQUIRED_VARS ${_component}_LIBRARIES ${_component}_INCLUDE_DIRS ${_component}_FOUND)
endforeach ()
list(INSERT _FFmpeg_REQUIRED_VARS 0 _FFmpeg_FOUND_LIBRARIES)

# Give a nice error message if some of the required vars are missing.
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS ${_FFmpeg_REQUIRED_VARS}
    HANDLE_COMPONENTS)