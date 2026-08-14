cmake_minimum_required(VERSION 3.10)
project(resource
    VERSION 1.0.0
    DESCRIPTION "resource to code"
    LANGUAGES CXX
)

set(GENERATE_NAME ${RESOURCE_NAME_PREIFX}_generate)
set(LIBRARY_NAME ${RESOURCE_NAME_PREIFX}_resource)

set(RESOURCE_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

# generate resource header file
add_executable(${GENERATE_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/generate.cpp)
target_compile_definitions(${GENERATE_NAME} PRIVATE
    -DINPUT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    -DOUTPUT_NAME="${RESOURCE_OUTPUT_DIR}/data.h"
)
add_custom_command(
    TARGET ${GENERATE_NAME} POST_BUILD
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GENERATE_NAME}
    COMMENT "<=== Generating resource header file ===>"
)

# resource library
add_library(${LIBRARY_NAME} STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/resource.cpp)
target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${LIBRARY_NAME} PRIVATE ${RESOURCE_OUTPUT_DIR})
add_dependencies(${LIBRARY_NAME} ${GENERATE_NAME})
