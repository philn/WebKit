
set(UNIFDEF_EXECUTABLE ${CMAKE_BINARY_DIR}/bin/unifdef CACHE FILEPATH "Path to unifdef executable")
mark_as_advanced(UNIFDEF_EXECUTABLE)
add_executable(unifdef unifdef.c)
