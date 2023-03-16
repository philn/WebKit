list(APPEND WebCore_UNIFIED_SOURCE_LIST_FILES
    "platform/SourcesGCrypt.txt"
)
list(APPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES
    "${WEBCORE_DIR}/crypto/gcrypt"
)

list(APPEND WebCore_LIBRARIES
    LibGcrypt::LibGcrypt
)
