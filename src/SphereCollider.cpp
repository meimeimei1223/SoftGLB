#include "SphereCollider.h"
#include "ShaderProgram.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

// ============================================================
// initFromOBJ
//   OBJ を最小限パース → AABB → 中心・半径を自動設定
// ============================================================
bool SphereCollider::initFromOBJ(const std::string& objPath) {
    std::ifstream file(objPath);
    if (!file.is_open()) {
        std::cerr << "[SphereCollider] Cannot open: " << objPath << std::endl;
        return false;
    }

    vertices_.clear();
    indices_.clear();

    std::vector<float> rawVerts; // OBJ 頂点 (x,y,z)
    std::string line;

    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            rawVerts.push_back(x);
            rawVerts.push_back(y);
            rawVerts.push_back(z);
        }
        else if (token == "f") {
            // スラッシュ区切りに対応（v, v/t, v/t/n, v//n）
            std::vector<int> faceIdx;
            std::string vStr;
            while (ss >> vStr) {
                int vIdx = std::stoi(vStr.substr(0, vStr.find('/'))) - 1;
                faceIdx.push_back(vIdx);
            }
            // ファンで三角形分割
            for (size_t i = 1; i + 1 < faceIdx.size(); ++i) {
                indices_.push_back(static_cast<unsigned>(faceIdx[0]));
                indices_.push_back(static_cast<unsigned>(faceIdx[i]));
                indices_.push_back(static_cast<unsigned>(faceIdx[i + 1]));
            }
        }
    }

    if (rawVerts.empty()) {
        std::cerr << "[SphereCollider] No vertices found in OBJ." << std::endl;
        return false;
    }

    // ---- AABB から中心・半径を計算 ----
    glm::vec3 aabbMin( std::numeric_limits<float>::max());
    glm::vec3 aabbMax(-std::numeric_limits<float>::max());

    for (size_t i = 0; i < rawVerts.size(); i += 3) {
        glm::vec3 p(rawVerts[i], rawVerts[i+1], rawVerts[i+2]);
        aabbMin = glm::min(aabbMin, p);
        aabbMax = glm::max(aabbMax, p);
    }

    glm::vec3 objCenter = (aabbMin + aabbMax) * 0.5f;
    radiusBase_ = glm::length(aabbMax - aabbMin) * 0.5f;
    radius_     = radiusBase_ * radiusScale_;   // スケール反映

    // OBJ の頂点を そのまま vertices_ に格納
    // （描画時は center_ を translate として使う）
    vertices_ = rawVerts;
    meshOffset_ = objCenter; // OBJ 空間での重心

    // 物理中心は初期位置（上方）に設定
    // ※ 必要なら caller が setCenter() で上書きしてください
    prevCenter_ = center_;

    std::cout << "[SphereCollider] Loaded: " << rawVerts.size()/3 << " verts, "
              << indices_.size()/3 << " tris | "
              << "center=" << objCenter.x << "," << objCenter.y << "," << objCenter.z
              << " radius=" << radius_ << std::endl;

    return true;
}

// ============================================================
// setupGL
// ============================================================
void SphereCollider::setupGL() {
    deleteGL();
    if (vertices_.empty() || indices_.empty()) return;

    computeNormals();

    indexCount_ = static_cast<GLsizei>(indices_.size());

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glGenBuffers(1, &nbo_);

    glBindVertexArray(vao_);

    // 頂点
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 vertices_.size() * sizeof(float),
                 vertices_.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);

    // 法線
    glBindBuffer(GL_ARRAY_BUFFER, nbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 normals_.size() * sizeof(float),
                 normals_.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(1);

    // インデックス
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices_.size() * sizeof(unsigned),
                 indices_.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
}

// ============================================================
// update  （毎フレーム：速度を差分で更新）
// ============================================================
void SphereCollider::update(float dt) {
    if (dt > 0.0f)
        velocity_ = (center_ - prevCenter_) / dt;
    prevCenter_ = center_;
}

// ============================================================
// startDrag
// ============================================================
void SphereCollider::startDrag(float sx, float sy,
                               const glm::mat4& view,
                               const glm::mat4& projection,
                               int windowW, int windowH)
{
    // 現在の球中心とカメラ位置からドラッグ距離（奥行き）を固定
    glm::mat4 invV = glm::inverse(view);
    glm::vec3 camPos = glm::vec3(invV[3]);
    grabDistance_ = glm::length(center_ - camPos);

    dragPrev_ = center_;
    dragDt_   = 0.0f;
    dragging_ = true;
}

// ============================================================
// moveDrag — 統合版（1〜4 moveMode 切り替え）
// ============================================================
#include "SoftBodyPhysics.h"

void SphereCollider::moveDrag(float sx, float sy,
                              const glm::mat4& view,
                              const glm::mat4& projection,
                              int windowW, int windowH,
                              float dt,
                              SoftBodyPhysics* physics)
{
    if (!dragging_) return;
    glm::vec3 target = screenToWorld(sx, sy, grabDistance_,
                                     view, projection, windowW, windowH);
    if (!physics || moveMode == MoveMode::TELEPORT) {
        applyTeleport(target, dt); return;
    }
    switch (moveMode) {
    case MoveMode::VELOCITY:
        applyVelocity(target, dt); break;
    case MoveMode::VELOCITY_SLIDE:
        applyVelocitySlide(target, dt, physics, false); break;
    case MoveMode::VELOCITY_SLIDE_SAFE:
        applyVelocitySlide(target, dt, physics, true); break;
    default:
        applyTeleport(target, dt); break;
    }
}

// ---- 1: TELEPORT ----
void SphereCollider::applyTeleport(const glm::vec3& target, float dt) {
    if (dragDt_ > 0.0f)
        velocity_ = (target - dragPrev_) / dragDt_;
    dragPrev_ = target;
    dragDt_   = dt;
    center_   = target;
}

// ---- 2: VELOCITY（速度クランプ） ----
void SphereCollider::applyVelocity(const glm::vec3& target, float dt) {
    glm::vec3 delta   = target - center_;
    float     moveLen = glm::length(delta);
    if (moveLen < 1e-7f) { dragPrev_ = target; dragDt_ = dt; return; }
    float dtSafe   = (dt > 0.0f) ? dt : 1.0f/60.0f;
    float maxSpeed = (maxSpeedOverride > 0.0f) ? maxSpeedOverride
                                               : (radius_ * 0.9f / dtSafe);
    float clamped  = std::min(moveLen, maxSpeed * dtSafe);
    glm::vec3 newCenter = center_ + (delta / moveLen) * clamped;
    velocity_ = (dragDt_ > 0.0f) ? (newCenter - dragPrev_) / dragDt_
                                 : glm::vec3(0.0f);
    dragPrev_ = newCenter; dragDt_ = dt; center_ = newCenter;
}

// ---- 3/4: VELOCITY + COLLIDE & SLIDE ----
void SphereCollider::applyVelocitySlide(const glm::vec3& target, float dt,
                                        SoftBodyPhysics* physics, bool safeMode)
{
    glm::vec3 delta   = target - center_;
    float     moveLen = glm::length(delta);
    if (moveLen < 1e-7f) { dragPrev_ = target; dragDt_ = dt; return; }

    // 速度クランプ
    float dtSafe   = (dt > 0.0f) ? dt : 1.0f/60.0f;
    float maxSpeed = (maxSpeedOverride > 0.0f) ? maxSpeedOverride
                                               : (radius_ * 0.9f / dtSafe);
    float clamped  = std::min(moveLen, maxSpeed * dtSafe);
    glm::vec3 clampedDelta = (delta / moveLen) * clamped;

    // 最近傍表面三角形の距離・外向き法線を取得（変形メッシュ対応）
    glm::vec3 closestPos, surfNormal;
    float dist = physics->getClosestSurfacePoint(center_, closestPos, surfNormal);

    float slideRadius = radius_ * slideRadiusScale;

    glm::vec3 newCenter;
    if (dist > slideRadius) {
        // 表面から十分離れている → 通常移動
        newCenter = center_ + clampedDelta;
    } else {
        // ---- safeMode(4): 内外判定して動作を切り替え ----
        if (safeMode) {
            // isInsideMesh（6方向レイキャスト）で確実に内外判定
            // キャッシュ：0.5cm 以上動いたときだけ再判定
            const float cacheThr = 0.005f;
            if (glm::length(center_ - insideCheckPos_) > cacheThr) {
                insideCheckPos_   = center_;
                insideCheckCache_ = physics->isInsideMeshPublic(center_);
            }

            if (insideCheckCache_) {
                // ★ 内部にいる：surfNormal は「最近傍三角形の外向き法線」
                //   移動ベクトルの surfNormal への射影 > 0 = 外に向かっている = 脱出を許可
                //   射影 <= 0 = 内側か接線方向 = ブロック
                float outComp = glm::dot(clampedDelta, surfNormal);
                if (outComp <= 0.0f) {
                    // 内向き・横方向の移動はブロック
                    dragPrev_ = center_; dragDt_ = dt; return;
                }
                // 外向き成分あり：外向き成分のみ抽出して移動（最速で脱出）
                glm::vec3 escapeDelta = surfNormal * outComp;
                newCenter = center_ + escapeDelta;
                velocity_ = (dragDt_ > 0.0f) ? (newCenter - dragPrev_) / dragDt_
                                             : glm::vec3(0.0f);
                dragPrev_ = newCenter; dragDt_ = dt; center_ = newCenter;
                return;
            }
        }

        // ---- 外部 or non-safeMode: Collide & Slide ----
        // 法線成分（内向き）をカットして表面沿いに滑らせる
        float normalComp = glm::dot(clampedDelta, surfNormal);
        glm::vec3 slideDelta = (normalComp < 0.0f)
                                   ? clampedDelta - surfNormal * normalComp
                                   : clampedDelta;
        newCenter = center_ + slideDelta;
        // めり込み解消：スライド後も近すぎたら押し出す
        glm::vec3 cp2, sn2;
        if (physics->getClosestSurfacePoint(newCenter, cp2, sn2) < slideRadius)
            newCenter = cp2 + sn2 * slideRadius;
    }
    velocity_ = (dragDt_ > 0.0f) ? (newCenter - dragPrev_) / dragDt_
                                 : glm::vec3(0.0f);
    dragPrev_ = newCenter; dragDt_ = dt; center_ = newCenter;
}

// ============================================================
// endDrag
// ============================================================
void SphereCollider::endDrag() {
    dragging_ = false;
}

// ============================================================
// draw
//   球 OBJ を center_ の位置に translate して描画
// ============================================================
void SphereCollider::draw(ShaderProgram& shader,
                          const glm::mat4& view,
                          const glm::mat4& projection,
                          const glm::vec3& lightPos)
{
    if (!visible || vao_ == 0) return;

    // OBJ 重心を原点に移動してから center_ へ translate し、さらに radiusScale_ でスケール
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center_ - meshOffset_ * radiusScale_);
    model = glm::scale(model, glm::vec3(radiusScale_));

    shader.use();
    shader.setUniform("model",      model);
    shader.setUniform("view",       view);
    shader.setUniform("projection", projection);
    shader.setUniform("lightPos",   lightPos);
    shader.setUniform("lightColor", glm::vec3(1.0f));
    shader.setUniform("viewPos",    lightPos); // 簡易
    shader.setUniform("vertColor",  glm::vec4(0.2f, 0.6f, 1.0f, 0.7f)); // 半透明青
    shader.setUniform("useTexture", false);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
}

// ============================================================
// screenToWorld  （スクリーン座標 + 奥行きからワールド座標）
// ============================================================
glm::vec3 SphereCollider::screenToWorld(float sx, float sy, float depth,
                                        const glm::mat4& view,
                                        const glm::mat4& projection,
                                        int windowW, int windowH) const
{
    // NDC
    float ndcX =  (2.0f * sx) / windowW - 1.0f;
    float ndcY = -(2.0f * sy) / windowH + 1.0f; // Y 反転

    // レイを生成してカメラ起点から depth だけ進む
    glm::mat4 invVP = glm::inverse(projection * view);

    glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPt  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    nearPt /= nearPt.w;
    farPt  /= farPt.w;

    glm::vec3 rayDir = glm::normalize(glm::vec3(farPt - nearPt));
    return glm::vec3(nearPt) + rayDir * depth;
}

// ============================================================
// computeNormals  （頂点法線を面法線の平均で計算）
// ============================================================
void SphereCollider::computeNormals() {
    size_t numVerts = vertices_.size() / 3;
    normals_.assign(numVerts * 3, 0.0f);

    for (size_t i = 0; i < indices_.size(); i += 3) {
        unsigned i0 = indices_[i], i1 = indices_[i+1], i2 = indices_[i+2];
        glm::vec3 v0(vertices_[i0*3], vertices_[i0*3+1], vertices_[i0*3+2]);
        glm::vec3 v1(vertices_[i1*3], vertices_[i1*3+1], vertices_[i1*3+2]);
        glm::vec3 v2(vertices_[i2*3], vertices_[i2*3+1], vertices_[i2*3+2]);
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
void SphereCollider::deleteGL() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_);       vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_);       ebo_ = 0; }
    if (nbo_) { glDeleteBuffers(1, &nbo_);       nbo_ = 0; }
}
