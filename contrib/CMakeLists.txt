set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins")

add_custom_target(plugins)
macro(radiant_plugin name)
    message(STATUS "Found Plugin ${name}")
    add_library(${name} MODULE ${ARGN})
    add_dependencies(plugins ${name})
    copy_dlls(${name})
    install(
            TARGETS ${name}
            LIBRARY DESTINATION plugins
    )
endmacro()

add_subdirectory(bobtoolz)
add_subdirectory(brushexport)
add_subdirectory(prtview)
add_subdirectory(shaderplug)
add_subdirectory(sunplug)
add_subdirectory(ufoaiplug)
