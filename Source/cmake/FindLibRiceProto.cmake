# - Try to find LibRiceProto
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

pkg_check_modules(PC_RICE_PROTO rice-proto)

find_path(LibRiceProto_INCLUDE_DIR
    NAMES rice-proto.h
    HINTS ${PC_RICE_PROTO_INCLUDEDIR}
          ${PC_RICE_PROTO_INCLUDE_DIRS}
)

find_library(LibRiceProto_LIBRARY
    NAMES rice-proto
    HINTS ${PC_RICE_PROTO_LIBDIR}
          ${PC_RICE_PROTO_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibRiceProto
    FOUND_VAR LibRiceProto_FOUND
    REQUIRED_VARS LibRiceProto_LIBRARY LibRiceProto_INCLUDE_DIR
    VERSION_VAR LibRiceProto_VERSION
)

if (LibRiceProto_LIBRARY AND NOT TARGET LibRice::Proto)
    add_library(LibRice::Proto UNKNOWN IMPORTED GLOBAL)
    set_target_properties(LibRice::Proto PROPERTIES
        IMPORTED_LOCATION "${LibRiceProto_LIBRARY}"
        INTERFACE_COMPILE_OPTIONS "${LibRiceProto_COMPILE_OPTIONS}"
        INTERFACE_INCLUDE_DIRECTORIES "${LibRiceProto_INCLUDE_DIR}"
    )
endif ()

mark_as_advanced(LibRiceProto_INCLUDE_DIR LibRiceProto_LIBRARY)

if (LibRiceProto_FOUND)
    set(LibRiceProto_LIBRARIES ${LibRiceProto_LIBRARY})
    set(LibRiceProto_INCLUDE_DIRS ${LibRiceProto_INCLUDE_DIR})
endif ()
