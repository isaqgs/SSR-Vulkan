cmake_minimum_required(VERSION 3.20)
project(SSR_Entrega_4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_executable(ssr_app src/main.cpp)
target_link_libraries(ssr_app PRIVATE Vulkan::Vulkan glfw glm::glm)

target_compile_definitions(ssr_app PRIVATE GLFW_INCLUDE_VULKAN GLM_FORCE_DEPTH_ZERO_TO_ONE)

set(SHADER_SOURCES
    ${CMAKE_SOURCE_DIR}/shaders/scene.vert
    ${CMAKE_SOURCE_DIR}/shaders/scene.frag
    ${CMAKE_SOURCE_DIR}/shaders/ssr.vert
    ${CMAKE_SOURCE_DIR}/shaders/ssr.frag
    ${CMAKE_SOURCE_DIR}/shaders/composite.vert
    ${CMAKE_SOURCE_DIR}/shaders/composite.frag
)

find_program(GLSLC glslc REQUIRED)
set(SPIRV_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SPIRV_DIR})

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(FILE ${SHADER} NAME)
    add_custom_command(
        OUTPUT ${SPIRV_DIR}/${FILE}.spv
        COMMAND ${GLSLC} ${SHADER} -o ${SPIRV_DIR}/${FILE}.spv
        DEPENDS ${SHADER}
        COMMENT "Compiling ${FILE} to SPIR-V"
        VERBATIM
    )
    list(APPEND SPIRV_FILES ${SPIRV_DIR}/${FILE}.spv)
endforeach()

add_custom_target(shaders ALL DEPENDS ${SPIRV_FILES})
add_dependencies(ssr_app shaders)

target_compile_definitions(ssr_app PRIVATE
    SHADER_DIR="${SPIRV_DIR}"
)
