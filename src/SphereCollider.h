#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <limits>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

class ShaderProgram;
class SoftBodyPhysics;

class SphereCollider {
public:
    // ---- 移動モード（1〜4キー） ----
    enum class MoveMode {
        TELEPORT            = 1,  // 1: 強制テレポート
        VELOCITY            = 2,  // 2: 速度クランプ
        VELOCITY_SLIDE      = 3,  // 3: 速度 + Collide&Slide
        VELOCITY_SLIDE_SAFE = 4,  // 4: 速度 + Slide + 内部停止
    };
    MoveMode moveMode = MoveMode::TELEPORT;

    static const char* getMoveName(MoveMode m) {
        switch (m) {
        case MoveMode::TELEPORT:            return "1:Teleport";
        case MoveMode::VELOCITY:            return "2:Velocity";
        case MoveMode::VELOCITY_SLIDE:      return "3:Vel+Slide";
        case MoveMode::VELOCITY_SLIDE_SAFE: return "4:Vel+Slide+Safe";
        default: return "?";
        }
    }

    bool initFromOBJ(const std::string& objPath);
    void setupGL();
    void update(float dt);

    void startDrag(float screenX, float screenY,
                   const glm::mat4& view, const glm::mat4& projection,
                   int windowW, int windowH);

    void moveDrag(float screenX, float screenY,
                  const glm::mat4& view, const glm::mat4& projection,
                  int windowW, int windowH,
                  float dt,
                  SoftBodyPhysics* physics = nullptr);

    void endDrag();

    void draw(ShaderProgram& shader,
              const glm::mat4& view,
              const glm::mat4& projection,
              const glm::vec3& lightPos);

    glm::vec3 getCenter()     const { return center_; }
    glm::vec3 getPrevCenter() const { return prevCenter_; }
    float     getRadius()     const { return radius_; }
    glm::vec3 getVelocity()   const { return velocity_; }
    bool      isDragging()    const { return dragging_; }

    void setCenter(const glm::vec3& c) {
        center_ = c; prevCenter_ = c; velocity_ = glm::vec3(0.0f);
    }
    void setRadiusScale(float scale) {
        radiusScale_ = std::max(0.05f, scale);
        radius_      = radiusBase_ * radiusScale_;
    }
    float getRadiusScale() const { return radiusScale_; }
    void  changeRadiusScale(float delta) { setRadiusScale(radiusScale_ + delta); }

    bool visible = true;

    // isInsideMesh キャッシュ（preSolve から直接アクセス）
    mutable glm::vec3 insideCheckPos_   = glm::vec3(1e9f);
    mutable bool      insideCheckCache_ = false;

    // スライド判定半径スケール（S/Shift+S で調整）
    // 小さいほど「近づいてからスライド発動」
    float slideRadiusScale = 0.5f;

    // 速度上限（0=自動: radius*0.9/dt）
    float maxSpeedOverride = 0.0f;

private:
    glm::vec3 center_      = glm::vec3(0.0f, 2.0f, 0.0f);
    float     radiusBase_  = 0.5f;
    float     radiusScale_ = 1.0f;
    float     radius_      = 0.5f;
    glm::vec3 velocity_    = glm::vec3(0.0f);
    glm::vec3 prevCenter_;

    bool      dragging_     = false;
    float     grabDistance_ = 5.0f;
    float     dragDt_       = 0.0f;
    glm::vec3 dragPrev_;

    glm::vec3 meshOffset_ = glm::vec3(0.0f);
    GLuint    vao_ = 0, vbo_ = 0, ebo_ = 0, nbo_ = 0;
    GLsizei   indexCount_ = 0;
    std::vector<float>    vertices_;
    std::vector<float>    normals_;
    std::vector<unsigned> indices_;

    void applyTeleport(const glm::vec3& target, float dt);
    void applyVelocity(const glm::vec3& target, float dt);
    void applyVelocitySlide(const glm::vec3& target, float dt,
                            SoftBodyPhysics* physics, bool safeMode);

    glm::vec3 screenToWorld(float sx, float sy, float depth,
                            const glm::mat4& view,
                            const glm::mat4& projection,
                            int windowW, int windowH) const;
    void computeNormals();
    void deleteGL();
};
