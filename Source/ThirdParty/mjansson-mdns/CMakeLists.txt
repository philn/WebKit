set_property(DIRECTORY . PROPERTY FOLDER "mjansson-mdns")

add_library(MjanssonMDNS STATIC
    mdns.c
)

set_target_properties(MjanssonMDNS PROPERTIES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE TRUE
    VISIBILITY_INLINES_HIDDEN TRUE
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
)

WEBKIT_ADD_TARGET_C_FLAGS(MjanssonMDNS
    -Wno-unused-but-set-variable
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-cast-align
)

add_library(MjanssonMDNS::MjanssonMDNS ALIAS MjanssonMDNS)
