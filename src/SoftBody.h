// SoftBody.h
// ============================================================
//  SoftBody: SoftBodyPhysics（物理）+ SoftBodyRendering（GL描画）
//  を束ねるネイティブ用ファサードクラス。
//  WASM ビルドでは SoftBodyPhysics を直接使うため、
//  このクラスごと除外できるよう GL 依存を分離してある。
// ============================================================
#ifndef SOFT_BODY_H
#define SOFT_BODY_H

#include "SoftBodyPhysics.h"

#ifndef __EMSCRIPTEN__

#include "SoftBodyRendering.h"
#include <GL/glew.h>
#include <glm/glm.hpp>

class ShaderProgram;

class SoftBody {
public:
    using MeshData = SoftBodyPhysics::MeshData;

    SoftBody(const MeshData& tetMesh, const MeshData& visMesh,
             float edgeCompliance = 0.1f, float volCompliance = 0.0f);
    ~SoftBody();

    // 物理シミュレーション
    void preSolve(float dt, const glm::vec3& gravity);
    void solve(float dt);
    void postSolve(float dt);
    void initPhysics();

    // 描画
    void drawVisMesh(ShaderProgram& shader);
    void drawTetMeshWireframe(ShaderProgram& shader);
    void updateAllMeshes();

    // インタラクション
    void startGrab(const glm::vec3& pos);
    void moveGrabbed(const glm::vec3& pos, const glm::vec3& vel);
    void endGrab(const glm::vec3& pos, const glm::vec3& vel);

    // アクセサ
    const std::vector<float>& getPositions() const { return physics_.getPositions(); }
    const MeshData& getMeshData()    const { return physics_.getMeshData(); }
    const MeshData& getVisMeshData() const { return physics_.getVisMeshData(); }

    // パラメータ設定
    void setEdgeCompliance(float c) { physics_.setEdgeCompliance(c); }
    void setVolCompliance(float c)  { physics_.setVolCompliance(c); }
    void setModelMatrix(const glm::mat4& m) { modelMatrix_ = m; }
    const glm::mat4& getModelMatrix() const { return modelMatrix_; }

    // 表示制御
    void setTetMeshVisible(bool v) { showTetMesh_ = v; }
    bool isTetMeshVisible() const  { return showTetMesh_; }

    // 静的ヘルパー
    static MeshData loadTetMesh(const std::string& filename)
        { return SoftBodyPhysics::loadTetMesh(filename); }
    static MeshData ReadVertexAndFace(const std::string& objPath)
        { return SoftBodyPhysics::ReadVertexAndFace(objPath); }

    // 内部アクセス（必要な場合）
    SoftBodyPhysics&   getPhysics()   { return physics_; }
    SoftBodyRendering& getRendering() { return rendering_; }

private:
    SoftBodyPhysics   physics_;
    SoftBodyRendering rendering_;
    glm::mat4 modelMatrix_ = glm::mat4(1.0f);
    bool showTetMesh_ = true;

    // 四面体ワイヤーフレーム用バッファ
    GLuint tetVAO_ = 0, tetVBO_ = 0;
    void setupTetWireframe();
    void updateTetWireframe();
    void deleteTetBuffers();
};

#endif // __EMSCRIPTEN__
#endif // SOFT_BODY_H
