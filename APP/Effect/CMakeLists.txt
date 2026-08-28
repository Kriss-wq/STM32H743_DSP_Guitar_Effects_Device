file(GLOB_RECURSE EFFECT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.c"

)

add_library(effect STATIC
        ${EFFECT_SOURCES}
)

target_include_directories(effect PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Src
        ${CMAKE_CURRENT_SOURCE_DIR}/Inc
        ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/DSP/Include
        ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
)
