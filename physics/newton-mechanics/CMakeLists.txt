add_library(emper-module-newton-mechanics
    ${CMAKE_CURRENT_SOURCE_DIR}/src/NewtonSystem.cpp
)

target_include_directories(emper-module-newton-mechanics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(emper-module-newton-mechanics PUBLIC
    emper-engine
)