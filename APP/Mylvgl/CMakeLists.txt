file(GLOB_RECURSE UI_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/custom/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/guider_customer_fonts/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/guider_fonts/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/image/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.c"

)

add_library(ui STATIC
        ${UI_SOURCES}
)

# 记得加上头文件搜索路径
target_include_directories(ui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/custom
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/guider_customer_fonts
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/guider_fonts
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/image
)