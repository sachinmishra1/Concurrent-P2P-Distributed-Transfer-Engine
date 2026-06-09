# Sanitiser options — set at cmake configure time:
#   cmake -B build -DENABLE_ASAN=ON
#   cmake -B build -DENABLE_TSAN=ON
#   cmake -B build -DENABLE_UBSAN=ON
option(ENABLE_ASAN  "Enable Address Sanitizer (-fsanitize=address)" OFF)
option(ENABLE_TSAN  "Enable Thread Sanitizer (-fsanitize=thread)" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer (-fsanitize=undefined)" OFF)

function(enable_sanitisers target)
    if(ENABLE_ASAN AND ENABLE_TSAN)
        message(FATAL_ERROR "ASan and TSan cannot be used simultaneously!")
    endif()

    set(SAN_FLAGS "")
    if(ENABLE_ASAN)
        list(APPEND SAN_FLAGS "-fsanitize=address" "-fno-omit-frame-pointer")
    endif()
    if(ENABLE_TSAN)
        list(APPEND SAN_FLAGS "-fsanitize=thread")
    endif()
    if(ENABLE_UBSAN)
        list(APPEND SAN_FLAGS "-fsanitize=undefined")
    endif()

    if(SAN_FLAGS)
        target_compile_options(${target} PRIVATE ${SAN_FLAGS})
        target_link_options(${target} PRIVATE ${SAN_FLAGS})
        message(STATUS "[Sanitisers] Enabled for target '${target}': ${SAN_FLAGS}")
    endif()
endfunction()