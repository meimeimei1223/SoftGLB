#pragma once

// ============================================================
// SphereColliderRendering.h
//   SphereColliderPhysics の描画ラッパー — OpenGL 専用
//   WASM ビルドではコンパイル不要（#ifndef EMSCRIPTEN で除外可）
// ============================================================

#ifndef __EMSCRIPTEN__

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>

class SphereColliderPhysics;
class ShaderProgram;

class SphereColliderRendering {
public:
    SphereColliderRendering() = default;
    ~SphereColliderRendering() { deleteGL(); }

    // Physics オブジェクトを紐付けてGPUバッファを構築
    void bind(SphereColliderPhysics* physics);
    void unbind();

    // 描画
    void draw(ShaderProgram& shader,
              const glm::mat4& view,
              const glm::mat4& projection,
              const glm::vec3& lightPos);

    // ドラッグ操作（スクリーン座標 → ワールド座標変換を内包）
    void startDrag(float screenX, float screenY,
                   const glm::mat4& view, const glm::mat4& projection,
                   int windowW, int windowH);

    void moveDrag(float screenX, float screenY,
                  const glm::mat4& view, const glm::mat4& projection,
                  int windowW, int windowH,
                  float dt);

    void endDrag();

private:
    SphereColliderPhysics* physics_ = nullptr;

    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, nbo_ = 0;
    GLsizei indexCount_ = 0;

    float grabDistance_ = 5.0f;   // startDrag時に確定し moveDrag中は変えない

    std::vector<float> normals_;

    void setupBuffers();
    void deleteGL();
    void computeNormals();

    // スクリーン座標 + 奥行き → ワールド座標
    glm::vec3 screenToWorld(float sx, float sy, float depth,
                            const glm::mat4& view,
                            const glm::mat4& projection,
                            int windowW, int windowH) const;
};

#endif // __EMSCRIPTEN__
