function(set_target_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wshadow             # warn when variable shadows another
            -Wconversion         # warn on implicit type conversions
            -Wsign-conversion    # warn on signed/unsigned conversions
            -Wno-array-bounds # suppress GCC 13 array-bounds false positives completely
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE
            /W4
            /WX  # treat warnings as errors
        )
    else()
        message(WARNING "Unknown compiler — warnings not configured")
    endif()
endfunction()