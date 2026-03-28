#ifndef __EMSCRIPTEN__

#include "SphereColliderRendering.h"
#include "SphereColliderPhysics.h"
#include "ShaderProgram.h"
#include <glm/gtc/matrix_transform.hpp>

// ============================================================
// bind  （PhysicsのメッシュデータでGPUバッファを構築）
// ============================================================
void SphereColliderRendering::bind(SphereColliderPhysics* physics) {
    physics_ = physics;
    if (physics_) setupBuffers();
}

void SphereColliderRendering::unbind() {
    deleteGL();
    physics_ = nullptr;
}

// ============================================================
// setupBuffers
// ============================================================
void SphereColliderRendering::setupBuffers() {
    deleteGL();
    if (!physics_) return;

    const auto& vertices = physics_->getVertices();
    const auto& indices  = physics_->getIndices();
    if (vertices.empty() || indices.empty()) return;

    computeNormals();

    indexCount_ = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glGenBuffers(1, &nbo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(float),
                 vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, nbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 normals_.size() * sizeof(float),
                 normals_.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(unsigned),
                 indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
}

// ============================================================
// draw
// ============================================================
void SphereColliderRendering::draw(ShaderProgram& shader,
                                    const glm::mat4& view,
                                    const glm::mat4& projection,
                                    const glm::vec3& lightPos)
{
    if (!physics_ || !physics_->visible || vao_ == 0) return;

    float radiusScale = physics_->getRadiusScale();
    glm::vec3 meshOffset = physics_->getMeshOffset();
    glm::vec3 center     = physics_->getCenter();

    glm::mat4 model = glm::translate(glm::mat4(1.0f), center - meshOffset * radiusScale);
    model = glm::scale(model, glm::vec3(radiusScale));

    shader.use();
    shader.setUniform("model",       model);
    shader.setUniform("view",        view);
    shader.setUniform("projection",  projection);
    shader.setUniform("lightPos",    lightPos);
    shader.setUniform("lightColor",  glm::vec3(1.0f));
    shader.setUniform("viewPos",     lightPos);
    shader.setUniform("vertColor",   glm::vec4(0.2f, 0.6f, 1.0f, 0.7f));
    shader.setUniform("useTexture",  false);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
}

// ============================================================
// startDrag  （スクリーン座標からワールド座標を計算してPhysicsへ渡す）
// ============================================================
void SphereColliderRendering::startDrag(float sx, float sy,
                                         const glm::mat4& view,
                                         const glm::mat4& projection,
                                         int windowW, int windowH)
{
    if (!physics_) return;

    // カメラ位置を取得してドラッグ開始時の奥行きを確定・保存する
    glm::mat4 invV   = glm::inverse(view);
    glm::vec3 camPos = glm::vec3(invV[3]);
    grabDistance_ = glm::length(physics_->getCenter() - camPos);

    glm::vec3 worldPos = screenToWorld(sx, sy, grabDistance_,
                                       view, projection, windowW, windowH);
    physics_->startDragAt(worldPos, grabDistance_);
}

// ============================================================
// moveDrag
// ============================================================
void SphereColliderRendering::moveDrag(float sx, float sy,
                                        const glm::mat4& view,
                                        const glm::mat4& projection,
                                        int windowW, int windowH,
                                        float dt)
{
    if (!physics_ || !physics_->isDragging()) return;

    // startDrag時に確定した grabDistance_ を使う（毎フレーム再計算しない）
    // 再計算すると球が動くたびに距離が変わり遠くに飛んでしまう
    glm::vec3 worldPos = screenToWorld(sx, sy, grabDistance_,
                                       view, projection, windowW, windowH);
    physics_->moveDragTo(worldPos, dt);
}

// ============================================================
// endDrag
// ============================================================
void SphereColliderRendering::endDrag() {
    if (physics_) physics_->endDrag();
}

// ============================================================
// screenToWorld
// ============================================================
glm::vec3 SphereColliderRendering::screenToWorld(float sx, float sy, float depth,
                                                   const glm::mat4& view,
                                                   const glm::mat4& projection,
                                                   int windowW, int windowH) const
{
    float ndcX =  (2.0f * sx) / windowW - 1.0f;
    float ndcY = -(2.0f * sy) / windowH + 1.0f;

    glm::mat4 invVP = glm::inverse(projection * view);

    glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPt  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    nearPt /= nearPt.w;
    farPt  /= farPt.w;

    glm::vec3 rayDir = glm::normalize(glm::vec3(farPt - nearPt));
    return glm::vec3(nearPt) + rayDir * depth;
}

// ============================================================
// computeNormals
// ============================================================
void SphereColliderRendering::computeNormals() {
    const auto& vertices = physics_->getVertices();
    const auto& indices  = physics_->getIndices();

    size_t numVerts = vertices.size() / 3;
    normals_.assign(numVerts * 3, 0.0f);

    for (size_t i = 0; i < indices.size(); i += 3) {
        unsigned i0 = indices[i], i1 = indices[i+1], i2 = indices[i+2];
        glm::vec3 v0(vertices[i0*3], vertices[i0*3+1], vertices[i0*3+2]);
        glm::vec3 v1(vertices[i1*3], vertices[i1*3+1], vertices[i1*3+2]);
        glm::vec3 v2(vertices[i2*3], vertices[i2*3+1], vertices[i2*3+2]);
        glm::vec3 fn = glm::cross(v1 - v0, v2 - v0);
        for (unsigned idx : {i0, i1, i2}) {
            normals_[idx*3]   += fn.x;
            normals_[idx*3+1] += fn.y;
            normals_[idx*3+2] += fn.z;
        }
    }
    for (size_t i = 0; i < numVerts; ++i) {
        glm::vec3 n(normals_[i*3], normals_[i*3+1], normals_[i*3+2]);
        float len = glm::length(n);
        if (len > 1e-5f) n /= len;
        normals_[i*3] = n.x; normals_[i*3+1] = n.y; normals_[i*3+2] = n.z;
    }
}

// ============================================================
// deleteGL
// ============================================================
void SphereColliderRendering::deleteGL() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_);       vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_);       ebo_ = 0; }
    if (nbo_) { glDeleteBuffers(1, &nbo_);       nbo_ = 0; }
}

#endif // __EMSCRIPTEN__
