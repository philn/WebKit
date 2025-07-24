# - Try to find LibRiceIo
# Once done, this will define
#
#  RICE_IO_INCLUDE_DIRS - the LibRice include directories
#  RICE_IO_LIBRARIES - link these to use LibRice
#
# Copyright (C) 2025 Igalia S.L
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

find_package(PkgConfig QUIET)

pkg_check_modules(PC_RICE_IO rice-io)

find_path(LibRiceIo_INCLUDE_DIR
    NAMES rice-io.h
    HINTS ${PC_RICE_IO_INCLUDEDIR}
          ${PC_RICE_IO_INCLUDE_DIRS}
)

find_library(LibRiceIo_LIBRARY
    NAMES rice-io
    HINTS ${PC_RICE_IO_LIBDIR}
          ${PC_RICE_IO_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibRiceIo
    FOUND_VAR LibRiceIo_FOUND
    REQUIRED_VARS LibRiceIo_LIBRARY LibRiceIo_INCLUDE_DIR
    VERSION_VAR LibRiceIo_VERSION
)

if (LibRiceIo_LIBRARY AND NOT TARGET LibRice::Io)
    add_library(LibRice::Io UNKNOWN IMPORTED GLOBAL)
    set_target_properties(LibRice::Io PROPERTIES
        IMPORTED_LOCATION "${LibRiceIo_LIBRARY}"
        INTERFACE_COMPILE_OPTIONS "${LibRiceIo_COMPILE_OPTIONS}"
        INTERFACE_INCLUDE_DIRECTORIES "${LibRiceIo_INCLUDE_DIR}"
    )
endif ()

mark_as_advanced(LibRiceIo_INCLUDE_DIR LibRiceIo_LIBRARY)

if (LibRiceIo_FOUND)
    set(LibRiceIo_LIBRARIES ${LibRiceIo_LIBRARY})
    set(LibRiceIo_INCLUDE_DIRS ${LibRiceIo_INCLUDE_DIR})
endif ()
