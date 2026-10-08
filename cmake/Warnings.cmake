# Interface target carrying the project's warning flags. Link it PRIVATEly.
add_library(dc_warnings INTERFACE)

if(MSVC)
    target_compile_options(dc_warnings INTERFACE /W4 /permissive- /utf-8)
    if(DC_WARNINGS_AS_ERRORS)
        target_compile_options(dc_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(
        dc_warnings
        INTERFACE -Wall
                  -Wextra
                  -Wpedantic
                  -Wshadow
                  -Wconversion
                  -Wsign-conversion
                  -Wnon-virtual-dtor
                  -Wold-style-cast
                  -Wcast-align
                  -Woverloaded-virtual
                  -Wnull-dereference
                  -Wdouble-promotion
                  -Wformat=2
                  -Wimplicit-fallthrough)
    if(DC_WARNINGS_AS_ERRORS)
        target_compile_options(dc_warnings INTERFACE -Werror)
    endif()
endif()
