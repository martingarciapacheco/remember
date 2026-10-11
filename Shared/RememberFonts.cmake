# Embedded, unmodified Inter 4.1 for non-Apple builds. Apple uses its system UI font.
if(NOT APPLE)
    juce_add_binary_data(RememberFontData
        HEADER_NAME RememberFontData.h
        NAMESPACE RememberFontData
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/../Assets/Fonts/Inter/Inter-Regular.ttf
            ${CMAKE_CURRENT_LIST_DIR}/../Assets/Fonts/Inter/Inter-SemiBold.ttf
            ${CMAKE_CURRENT_LIST_DIR}/../Assets/Fonts/Inter/InterDisplay-Regular.ttf
            ${CMAKE_CURRENT_LIST_DIR}/../Assets/Fonts/Inter/InterDisplay-SemiBold.ttf)
endif()

function(remember_link_fonts target)
    if(NOT APPLE)
        target_link_libraries(${target} PRIVATE RememberFontData)
    endif()
endfunction()
