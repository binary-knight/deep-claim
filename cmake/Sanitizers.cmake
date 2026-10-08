# Sanitizer flags applied project-wide when DC_SANITIZE is ON.
# MSVC has AddressSanitizer but no UndefinedBehaviorSanitizer, so the
# sanitizer CI leg runs on Ubuntu only (see DESIGN.md).
if(DC_SANITIZE)
    if(MSVC)
        message(WARNING "MSVC supports ASan only; UBSan is unavailable.")
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                            -fno-sanitize-recover=all)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()
