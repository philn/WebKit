# Copyright (C) 2023 ChangSeok Oh <changseok@webkit.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

#[=======================================================================[.rst:
FindWhisper
-----------

Find whisper headers and libraries.

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables in your project:

``Whisper_FOUND``
  true if (the requested version of) whisper is available.
``Whisper_LIBRARIES``
  the libraries to link against to use whisper.
``Whisper_INCLUDE_DIRS``
  where to find the whisper headers.

#]=======================================================================]

find_path(Whisper_INCLUDE_DIR
    NAMES whisper.h
)

find_library(Whisper_LIBRARY
    NAMES ${Whisper_NAMES} whisper
)

# Find components
if (Whisper_INCLUDE_DIR AND Whisper_LIBRARY)
    set(Whisper_LIBS_FOUND "whisper (required): ${Whisper_LIBRARY}")
else ()
    set(Whisper_LIBS_NOT_FOUND "whisper (required)")
endif ()

macro(FIND_WHISPER_COMPONENT _component_prefix _library)
    find_library(${_component_prefix}_LIBRARY
        NAMES ${_library}
    )

    if (${_component_prefix}_LIBRARY)
        list(APPEND Whisper_LIBS_FOUND "${_library} (required): ${${_component_prefix}_LIBRARY}")
    else ()
        list(APPEND Whisper_LIBS_NOT_FOUND "${_library} (required)")
    endif ()
endmacro()

if (NOT Whisper_FIND_QUIETLY)
    if (Whisper_LIBS_FOUND)
        message(STATUS "Found the following Whisper libraries:")
        foreach (found ${Whisper_LIBS_FOUND})
            message(STATUS " ${found}")
        endforeach ()
    endif ()
    if (Whisper_LIBS_NOT_FOUND)
        message(STATUS "The following Whisper libraries were not found:")
        foreach (found ${Whisper_LIBS_NOT_FOUND})
            message(STATUS " ${found}")
        endforeach ()
    endif ()
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Whisper
    FOUND_VAR Whisper_FOUND
    REQUIRED_VARS Whisper_INCLUDE_DIR Whisper_LIBRARY
)

if (Whisper_INCLUDE_DIR AND Whisper_LIBRARY)
    set(Whisper_FOUND 1)
else ()
    set(Whisper_FOUND 0)
endif ()

mark_as_advanced(
    Whisper_INCLUDE_DIR
    Whisper_LIBRARY
    Whisper_FOUND
)

if (Whisper_FOUND)
    set(Whisper_LIBRARIES ${Whisper_LIBRARY})
    set(Whisper_INCLUDE_DIRS ${Whisper_INCLUDE_DIR})
endif ()
