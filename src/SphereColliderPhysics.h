#pragma once

// ============================================================
// SphereColliderPhysics.h
//   物理演算専用クラス — OpenGL 依存ゼロ
//   WASM ビルド時はこのクラスのみ使用する
//   描画が必要なネイティブビルドでは SphereColliderRendering を使う
// ============================================================

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <limits>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

class SphereColliderPhysics {
public:
    // ---------- 初期化 ----------
    // OBJ ファイルから形状を読み込み、AABB で中心・半径を決定
    bool initFromOBJ(const std::string& objPath);

    // OBJ 文字列から直接ロード（WASM 用）
    bool initFromOBJString(const std::string& objData);

    // ---------- 毎フレーム ----------
    void update(float dt);

    // ---------- ドラッグ操作（座標は呼び出し元が計算して渡す） ----------
    void startDragAt(const glm::vec3& worldPos, float grabDistance);
    void moveDragTo(const glm::vec3& worldPos, float dt);
    void endDrag();

    // ---------- コリジョン情報アクセサ ----------
    glm::vec3 getCenter()     const { return center_; }
    glm::vec3 getPrevCenter() const { return prevCenter_; }
    float     getRadius()     const { return radius_; }
    glm::vec3 getVelocity()   const { return velocity_; }
    glm::vec3 getMeshOffset() const { return meshOffset_; }
    float     getRadiusScale()const { return radiusScale_; }
    float     getRadiusBase() const { return radiusBase_; }

    bool isDragging() const { return dragging_; }

    // 位置の直接設定
    void setCenter(const glm::vec3& c) {
        center_ = c; prevCenter_ = c; velocity_ = glm::vec3(0.0f);
    }

    // 半径スケール設定（1.0 = OBJ の AABB そのまま）
    void setRadiusScale(float scale) {
        radiusScale_ = std::max(0.05f, scale);
        radius_      = radiusBase_ * radiusScale_;
    }
    void changeRadiusScale(float delta) { setRadiusScale(radiusScale_ + delta); }

    // 表示 ON/OFF フラグ（Rendering 側が参照する）
    bool visible = true;

    // ---------- メッシュデータ（Rendering 側が参照する） ----------
    const std::vector<float>&    getVertices() const { return vertices_; }
    const std::vector<unsigned>& getIndices()  const { return indices_; }

    // isInsideMesh キャッシュ（SoftBodyPhysics::preSolve から直接アクセス）
    mutable glm::vec3 insideCheckPos_   = glm::vec3(1e9f);
    mutable bool      insideCheckCache_ = false;

private:
    // ---- 物理 ----
    glm::vec3 center_      = glm::vec3(0.0f, 2.0f, 0.0f);
    float     radiusBase_  = 0.5f;
    float     radiusScale_ = 1.0f;
    float     radius_      = 0.5f;
    glm::vec3 velocity_    = glm::vec3(0.0f);
    glm::vec3 prevCenter_;

    // ---- ドラッグ ----
    bool      dragging_     = false;
    float     grabDistance_ = 5.0f;
    glm::vec3 dragPrev_;
    float     dragDt_ = 0.0f;

    // ---- OBJ メッシュオフセット ----
    glm::vec3 meshOffset_ = glm::vec3(0.0f);

    // ---- メッシュデータ（GL非依存の生データ） ----
    std::vector<float>    vertices_;
    std::vector<unsigned> indices_;

    // ---- 内部ヘルパー ----
    bool parseOBJ(std::istream& stream);
};
