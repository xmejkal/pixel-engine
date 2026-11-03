# The engine module (P589 probe): the world in engine/core and its MicroPython binding. --- probe, throwaway (P589) ---
add_library(usermod_engine INTERFACE)

target_sources(usermod_engine INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/binding.c
    ${CMAKE_CURRENT_LIST_DIR}/core/world.c
    )

target_include_directories(usermod_engine INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/core
    )

target_link_libraries(usermod INTERFACE usermod_engine)
