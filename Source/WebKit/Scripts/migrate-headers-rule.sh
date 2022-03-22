#!/bin/sh
if [ "${WK_PLATFORM_NAME}" != macosx ]; then
    echo "#import <WebKitLegacy/${INPUT_FILE_NAME}>" > "${SCRIPT_OUTPUT_FILE_0}"
else
    if grep -q "AVAILABLE.*9876_5" "${INPUT_FILE_PATH}"; then
        line=$(awk "/AVAILABLE.*9876_5/ { print FNR; exit }" "${INPUT_FILE_PATH}" )
        echo "${SCRIPT_OUTPUT_FILE_0}:$line: error: A class within a public header has unspecified availability." >&2
        exit 1
    fi    
    sed -Ee "s/\<Web(Core|KitLegacy)/\<WebKit/" -e "s/(^ *)WEBCORE_EXPORT /\1/" "${INPUT_FILE_PATH}" > "${SCRIPT_OUTPUT_FILE_0}"
    SCRIPT_INPUT_FILE="${SCRIPT_OUTPUT_FILE_0}" "${SRCROOT}/../WebKitLegacy/Scripts/postprocess-header-rule"
    SCRIPT_INPUT_FILE="${SCRIPT_OUTPUT_FILE_0}" "${SRCROOT}/Scripts/postprocess-header-rule"
fi
