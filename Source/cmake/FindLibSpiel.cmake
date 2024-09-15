# - Try to find LibSpiel
# Once done, this will define
#
#  SPIEL_INCLUDE_DIRS - the LibSpiel include directories
#  SPIEL_LIBRARIES - link these to use LibSpiel
#
# Copyright (C) 2024 Igalia S.L
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

pkg_check_modules(PC_SPIEL spiel-1.0)

find_path(SPIEL_INCLUDE_DIRS
    NAMES spiel.h
    HINTS ${PC_SPIEL_INCLUDEDIR}
          ${PC_SPIEL_INCLUDE_DIRS}
)

find_library(SPIEL_LIBRARIES
    NAMES spiel-1.0
    HINTS ${PC_SPIEL_LIBDIR}
          ${PC_SPIEL_LIBRARY_DIRS}
)

if (SPIEL_INCLUDE_DIRS AND SPIEL_LIBRARIES)
  set(SPIEL_FOUND 1)
else ()
  set(SPIEL_FOUND 0)
endif ()

mark_as_advanced(
    SPIEL_INCLUDE_DIRS
    SPIEL_LIBRARIES
)
