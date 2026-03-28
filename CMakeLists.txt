cmake_minimum_required(VERSION 3.16)
project(SoftBodyLiverWasm)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_compile_options(-Wno-volatile)

# ============================================================
# サードパーティ
# ============================================================
set(THIRD_PARTY_SOURCES
    ${CMAKE_SOURCE_DIR}/third_party/tinyobjloader/tiny_obj_loader_impl.cpp
)

set(THIRD_PARTY_INCLUDES
    ${CMAKE_SOURCE_DIR}/third_party/tinyobjloader
    ${CMAKE_SOURCE_DIR}/third_party/stb_image
    ${CMAKE_SOURCE_DIR}/third_party/tinygltf
)

# ============================================================
# ソースディレクトリの自動検出
#   src/ サブディレクトリがあればそちらを、なければルートを使う
# ============================================================
if(EXISTS "${CMAKE_SOURCE_DIR}/src/main.cpp")
    set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")
    message(STATUS "Source dir: src/")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/main.cpp")
    set(SRC_DIR "${CMAKE_SOURCE_DIR}")
    message(STATUS "Source dir: project root")
else()
    message(FATAL_ERROR "Cannot find main.cpp — check project structure")
endif()

# ============================================================
# WASM ビルド（Emscripten）
# ============================================================
if(EMSCRIPTEN)
    message(STATUS "Building for WebAssembly (Emscripten)")

    set(WASM_SOURCES
        ${SRC_DIR}/SoftBodyPhysics.cpp
        ${SRC_DIR}/MeshDataTypes.cpp
        ${SRC_DIR}/MeshNormalizer.cpp
        ${SRC_DIR}/CentVoxTetrahedralizerHybrid.cpp
        ${SRC_DIR}/GlbLoader.cpp
        ${SRC_DIR}/WasmBindings.cpp
        ${THIRD_PARTY_SOURCES}
    )

    if(EXISTS "${SRC_DIR}/SphereColliderPhysics.cpp")
        list(APPEND WASM_SOURCES "${SRC_DIR}/SphereColliderPhysics.cpp")
        message(STATUS "SphereColliderPhysics: enabled")
    else()
        message(WARNING "SphereColliderPhysics.cpp not found — copy the generated file to ${SRC_DIR}")
    endif()

    add_executable(${PROJECT_NAME} ${WASM_SOURCES})
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${SRC_DIR}
        ${THIRD_PARTY_INCLUDES}
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES
        SUFFIX ".js"
        LINK_FLAGS "\
            -s MODULARIZE=1 \
            -s EXPORT_NAME='SoftBodyModule' \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap'] \
            --bind \
            -O2 \
        "
    )

# ============================================================
# ネイティブビルド
# ============================================================
else()
    message(STATUS "Building native (OpenGL)")

    file(GLOB SOURCES
        "${SRC_DIR}/*.cpp"
        "${SRC_DIR}/*.h"
        "${SRC_DIR}/*.hpp"
        "${SRC_DIR}/*.glsl"
    )
    list(APPEND SOURCES ${THIRD_PARTY_SOURCES})

    find_package(OpenGL REQUIRED)
    find_package(GLEW   REQUIRED)
    find_package(glfw3  REQUIRED)

    include_directories(
        ${SRC_DIR}
        ${GLEW_INCLUDE_DIRS}
        ${GLFW_INCLUDE_DIRS}
        ${OPENGL_INCLUDE_DIR}
        ${THIRD_PARTY_INCLUDES}
    )

    link_directories(${GLEW_LIBRARY_DIRS})

    add_executable(${PROJECT_NAME} ${SOURCES})

    target_link_libraries(${PROJECT_NAME} PUBLIC
        ${OPENGL_LIBRARIES}
        ${GLEW_LIBRARIES}
        glfw
    )

    set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
endif()
