function(disable_pax_mprotect target)
    if (BSD STREQUAL "NetBSD")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND paxctl +m "$<TARGET_FILE:${target}>"
            COMMENT "Disabling PaX MPROTECT restrictions for '${target}'"
            VERBATIM
        )
    else()
        message(FATAL_ERROR "disable_pax_mprotect only applies on NetBSD.")
    endif()
endfunction()
