cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_compile_definitions(ARM_MATH_CM7;ARM_MATH_MATRIX_CHECK;ARM_MATH_ROUNDING)
add_compile_options(-mfloat-abi=hard -mfpu=fpv5-d16 -O3)
add_link_options(-mfloat-abi=hard -mfpu=fpv5-d16 )

function(collect_subdirectories OUTPUT_VAR ROOT_DIR)
    set(SUBDIRS "${ROOT_DIR}")  # 优先把根目录放进去
    function(_recurse dir)
        file(GLOB CHILDREN RELATIVE ${dir} ${dir}/*)
        foreach(CHILD ${CHILDREN})
            set(CHILD_PATH "${dir}/${CHILD}")
            if(IS_DIRECTORY ${CHILD_PATH})
                list(APPEND SUBDIRS ${CHILD_PATH})
                _recurse(${CHILD_PATH})
            endif()
        endforeach()
        set(SUBDIRS ${SUBDIRS} PARENT_SCOPE)
    endfunction()
    _recurse(${ROOT_DIR})
    list(REMOVE_DUPLICATES SUBDIRS)
    set(${OUTPUT_VAR} ${SUBDIRS} PARENT_SCOPE)
endfunction()

function(collect_all_c_files OUTPUT_VAR ROOT_DIR)
    file(GLOB_RECURSE C_FILES "${ROOT_DIR}/*.c")
    list(REMOVE_DUPLICATES C_FILES)
    set(${OUTPUT_VAR} ${C_FILES} PARENT_SCOPE)
endfunction()



set(LVGL_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/lvgl)
file(GLOB_RECURSE SOURCES ${LVGL_ROOT_DIR}/src/*.c)
file(GLOB_RECURSE EXAMPLE_SOURCES ${LVGL_ROOT_DIR}/examples/*.c)
file(GLOB_RECURSE DEMO_SOURCES ${LVGL_ROOT_DIR}/demos/*.c)
# LV_DEMO_MUSIC_LARGE=0 时不需要 large 资源；这些文件极大，-O3 并行编译容易 OOM
list(FILTER DEMO_SOURCES EXCLUDE REGEX ".*/demos/music/assets/.*_large\\.c$")

file(GLOB_RECURSE GENERATED_SOURCES ${LVGL_ROOT_DIR}/generated/*.c)
file(GLOB_RECURSE SDK_SOURCES ${LVGL_ROOT_DIR}/sdk/*.c)
file(GLOB_RECURSE IMPORT_SOURCES ${LVGL_ROOT_DIR}/import/*.c)

file(GLOB_RECURSE BSP_SOURCES ${CMAKE_SOURCE_DIR}/BSP/Src/*.c)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Project2)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME}
        BSP/Src/LED.c
        BSP/Inc/LED.h
        BSP/Src/DAC_I2S.c
        BSP/Inc/DAC_I2S.h
)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# PendSV 里是汇编 "bl vTaskSwitchContext"，LTO 当没人用会删掉这个符号
target_compile_options(FreeRTOS PRIVATE -fno-lto)
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -Wl,-u,vTaskSwitchContext
    -Wl,-u,pxCurrentTCB
)

# Link directories setup


# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
        ${SOURCES}
        ${EXAMPLE_SOURCES}
        ${DEMO_SOURCES}
        ${GENERATED_SOURCES}
        ${SDK_SOURCES}
        ${IMPORT_SOURCES}
        ${BSP_SOURCES}

)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
        ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/DSP/Include
        ${LVGL_ROOT_DIR}/src
        ${LVGL_ROOT_DIR}/examples
        ${LVGL_ROOT_DIR}/demos
        ${LVGL_ROOT_DIR}/generated
        ${LVGL_ROOT_DIR}/sdk
        ${LVGL_ROOT_DIR}/import
        BSP/Inc

)
#add_subdirectory(APP/Mylvgl)
add_subdirectory(APP/Effect)
add_subdirectory(lvgl_main)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    ui
    stm32cubemx
        effect
        ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/DSP/Lib/GCC/libarm_cortexM7lfdp_math.a
    -lm
    # Add user defined libraries
)

target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
