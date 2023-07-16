# Copyright (C) 2023 Igalia S.L.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND ITS CONTRIBUTORS ``AS
# IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR ITS
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#[=======================================================================[.rst:
FindPipeWire
-------

Find libpipewire headers and libraries.

Imported Targets
^^^^^^^^^^^^^^^^

``PipeWire::PipeWire``
  The libpipewire library, if found.

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables in your project:

``PipeWire_FOUND``
  true if (the requested version of) libpipewire is available.
``PipeWire_VERSION``
  the libpipewire version.
``PipeWire_LIBRARIES``
  the library to link against to use libpipewire.
``PipeWire_INCLUDE_DIRS``
  where to find the libpipewire headers.
``PipeWire_COMPILE_OPTIONS``
  this should be passed to target_compile_options(), if the imported
  target is not used for linking.

#]=======================================================================]

find_package(PkgConfig QUIET)
pkg_check_modules(PC_PipeWire QUIET libpipewire-0.3)
pkg_search_module(PC_Spa QUIET libspa-0.2)

set(PipeWire_COMPILE_OPTIONS ${PC_PipeWire_CFLAGS_OTHER})
set(PipeWire_VERSION ${PC_PipeWire_VERSION})

find_path(PipeWire_INCLUDE_DIR
    NAMES pipewire/pipewire.h
    HINTS ${PC_PipeWire_INCLUDEDIR}
          ${PC_PipeWire_INCLUDE_DIRS}
          ${PC_PipeWire_INCLUDE_DIRS}/pipewire-0.3
)

find_path(Spa_INCLUDE_DIR
    NAMES spa/param/props.h
    HINTS ${PC_Spa_INCLUDE_DIRS}
          ${PC_Spa_INCLUDE_DIRS}/spa-0.2
)

find_library(PipeWire_LIBRARY
    NAMES pipewire-0.3
    HINTS ${PC_PipeWire_LIBDIR}
          ${PC_PipeWire_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PipeWire
    REQUIRED_VARS PipeWire_LIBRARY PipeWire_INCLUDE_DIR Spa_INCLUDE_DIR
    VERSION_VAR PipeWire_VERSION
)

if (PipeWire_LIBRARY AND NOT TARGET PipeWire::PipeWire)
    add_library(PipeWire::PipeWire UNKNOWN IMPORTED GLOBAL)
    set_target_properties(PipeWire::PipeWire PROPERTIES
        IMPORTED_LOCATION "${PipeWire_LIBRARY}"
        INTERFACE_COMPILE_OPTIONS "${PipeWire_COMPILE_OPTIONS}"
        INTERFACE_INCLUDE_DIRECTORIES "${PipeWire_INCLUDE_DIR};${Spa_INCLUDE_DIR}"
    )
endif ()

mark_as_advanced(
  PipeWire_INCLUDE_DIR
  PipeWire_LIBRARY
)

if (PipeWire_FOUND)
    set(PipeWire_LIBRARIES ${PipeWire_LIBRARY})
    set(PipeWire_INCLUDE_DIRS ${PipeWire_INCLUDE_DIR})
endif ()
