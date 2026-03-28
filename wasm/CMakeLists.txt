# CMakeLists.txt
# Emscripten WASM ビルド用
# 
# ビルド方法:
#   cd wasm
#   mkdir build && cd build
#   emcmake cmake -G Ninja ..
#   emmake ninja
#
# 出力:
#   wasm/build/softbody.js   - JavaScript グルーコード
#   wasm/build/softbody.wasm - WebAssembly バイナリ

cmake_minimum_required(VERSION 3.10)
project(SoftBodyWasm)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ソースディレクトリ
set(SRC_DIR ${CMAKE_SOURCE_DIR}/../src)

# GLMディレクトリ (glm/glm.hppの構造に対応)
set(GLM_DIR ${CMAKE_SOURCE_DIR}/..)

# ソースファイル (GLB + 球コライダー機能を追加)
set(SOURCES
    ${SRC_DIR}/SoftBodyPhysics.cpp
    ${SRC_DIR}/SphereColliderPhysics.cpp
    ${SRC_DIR}/WasmBindings.cpp
    ${SRC_DIR}/MeshDataTypes.cpp
    ${SRC_DIR}/GlbLoader.cpp
    ${SRC_DIR}/MeshNormalizer.cpp
    ${SRC_DIR}/CentVoxTetrahedralizerHybrid.cpp
)

# ヘッダーファイル
set(HEADERS
    ${SRC_DIR}/SoftBodyPhysics.h
    ${SRC_DIR}/VectorMath.h
    ${SRC_DIR}/Hash.h
)

# 実行ファイル (WASM)
add_executable(softbody ${SOURCES} ${HEADERS})

# インクルードパス
target_include_directories(softbody PRIVATE ${SRC_DIR} ${GLM_DIR})

# サードパーティライブラリディレクトリを追加
target_include_directories(softbody PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

# GLM の設定
if(DEFINED ENV{GLM_INCLUDE_DIR})
    target_include_directories(softbody PRIVATE $ENV{GLM_INCLUDE_DIR})
elseif(EXISTS "${CMAKE_SOURCE_DIR}/../external/glm")
    target_include_directories(softbody PRIVATE ${CMAKE_SOURCE_DIR}/../external/glm)
endif()

# Emscripten 専用設定
if(EMSCRIPTEN)
    message(STATUS "Building for Emscripten (WebAssembly)")
    
    set(EMSCRIPTEN_FLAGS
        "--bind"
        "-O2"
        "-s WASM=1"
        "-s EXPORT_NAME=SoftBodyModule"
        "-s MODULARIZE=1"
        "-s INITIAL_MEMORY=67108864"
        "-s MAXIMUM_MEMORY=2147483648"
        "-s ALLOW_MEMORY_GROWTH=1"
        "-s STACK_SIZE=5242880"
        "-s DISABLE_EXCEPTION_CATCHING=0"
        "-s FILESYSTEM=1"
        "-s ENVIRONMENT=web,worker"
        "-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    )
    
    string(REPLACE ";" " " EMSCRIPTEN_FLAGS_STR "${EMSCRIPTEN_FLAGS}")
    
    set_target_properties(softbody PROPERTIES
        LINK_FLAGS "${EMSCRIPTEN_FLAGS_STR}"
        SUFFIX ".js"
    )
    
    target_compile_options(softbody PRIVATE -O3 -fexceptions)
    
    # OpenMP は Emscripten 非対応。#pragma omp を警告なしで無視させる。
    target_compile_options(softbody PRIVATE -Wno-unknown-pragmas)
    
    # tiny_obj_loaderの実装を有効化
    target_compile_definitions(softbody PRIVATE TINYOBJLOADER_IMPLEMENTATION)
    
else()
    message(STATUS "Building for native (testing)")
    find_package(glm QUIET)
    if(glm_FOUND)
        target_link_libraries(softbody glm::glm)
    endif()
endif()

message(STATUS "")
message(STATUS "=== SoftBody WASM Build ===")
message(STATUS "Source dir: ${SRC_DIR}")
message(STATUS "Output dir: ${CMAKE_BINARY_DIR}")
message(STATUS "Output: softbody.js, softbody.wasm")
message(STATUS "===========================")