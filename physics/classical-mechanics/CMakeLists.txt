add_library(emper-module-classical-mechanics
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ClassicalMechanicsSystem.cpp
)

target_include_directories(emper-module-classical-mechanics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(emper-module-classical-mechanics PUBLIC
    emper-engine
)