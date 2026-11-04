# The engine module: the world (P590, engine.Bodies), the fire (P595, HeatField) and the balls (P598, Balls, on the
# grid) in engine/core, and their MicroPython binding. The binding draws into s3lcd's frame, so it reads s3lcd's header.
add_library(usermod_engine INTERFACE)

target_sources(usermod_engine INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/binding.c
    ${CMAKE_CURRENT_LIST_DIR}/core/world.c
    ${CMAKE_CURRENT_LIST_DIR}/core/fx_fire.c
    ${CMAKE_CURRENT_LIST_DIR}/core/grid.c
    ${CMAKE_CURRENT_LIST_DIR}/core/fx_balls.c
    )

target_include_directories(usermod_engine INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/core
    ${CMAKE_CURRENT_LIST_DIR}/../s3lcd/src
    )

target_link_libraries(usermod INTERFACE usermod_engine)
