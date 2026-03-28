#ifndef SOFT_BODY_PHYSICS_H
#define SOFT_BODY_PHYSICS_H

#include "VectorMath.h"
#include "Hash.h"
#include <vector>
#include <map>
#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_set>
#include <queue>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ネイティブビルドでは SphereCollider をそのまま使う
// WASM ビルド（__EMSCRIPTEN__）では SphereColliderPhysics を使う
#ifdef __EMSCRIPTEN__
#  include "SphereColliderPhysics.h"
#else
class SphereCollider;
using SphereColliderPhysics = SphereCollider;
#endif

class SoftBodyPhysics {
public:
    struct MeshData {
        std::vector<float> verts;
        std::vector<float> uvs;
        std::vector<int>   tetIds;
        std::vector<int>   tetEdgeIds;
        std::vector<int>   tetSurfaceTriIds;
    };

    SoftBodyPhysics(const MeshData& tetMesh, const MeshData& visMesh,
                    float edgeCompliance = 100.0f, float volCompliance = 0.0f);
    ~SoftBodyPhysics();

    void preSolve(float dt, const glm::vec3& gravity);
    void solve(float dt);
    void postSolve(float dt);
    void initPhysics();
    void updateAllMeshes();

    void startGrab(const glm::vec3& pos);
    void moveGrabbed(const glm::vec3& pos, const glm::vec3& vel);
    void endGrab(const glm::vec3& pos, const glm::vec3& vel);
    void startGrab(float x, float y, float z);
    void moveGrabbed(float x, float y, float z, float vx, float vy, float vz);
    void endGrab(float x, float y, float z, float vx, float vy, float vz);

    void applyShapeRestoration(float strength);

    // ★ 球コリジョン登録
    void addSphereCollider(SphereColliderPhysics* col) { sphereColliders_.push_back(col); }
    void clearSphereColliders()                 { sphereColliders_.clear(); }

    // ★ 球の移動制限用：表面頂点までの最短距離を返す（moveDragで使用）
    float getMinDistToSurface(const glm::vec3& point) const;

    // Collide&Slide用：最近傍表面頂点位置と外向き法線を返す
    float getClosestSurfacePoint(const glm::vec3& point,
                                 glm::vec3& closestPos,
                                 glm::vec3& surfNormal) const;

    // レイキャスト内外判定（public版）
    bool isInsideMeshPublic(const glm::vec3& point) const { return isInsideMesh(point); }

    const std::vector<float>& getPositions() const { return positions; }
    float*  getPositionsPtr()  { return positions.data(); }
    size_t  getPositionsSize() const { return positions.size(); }
    size_t  getNumParticles()  const { return numParticles; }

    const std::vector<int>& getTetIds()           const { return tetIds; }
    size_t getTetIdsSize()  const { return tetIds.size(); }
    size_t getNumTets()     const { return numTets; }

    const std::vector<int>& getEdgeIds()          const { return edgeIds; }
    size_t getEdgeIdsSize() const { return edgeIds.size(); }

    const std::vector<int>& getTetSurfaceTriIds() const { return tetSurfaceTriIds; }
    size_t getTetSurfaceTriIdsSize() const { return tetSurfaceTriIds.size(); }

    const std::vector<float>& getVisPositions()   const { return vis_positions; }
    float*  getVisPositionsPtr()  { return vis_positions.data(); }
    size_t  getVisPositionsSize() const { return vis_positions.size(); }
    size_t  getNumVisVerts()      const { return numVisVerts; }

    const std::vector<float>& getVisNormals()     const { return vis_normals; }
    float*  getVisNormalsPtr()   { return vis_normals.data(); }
    size_t  getVisNormalsSize()  const { return vis_normals.size(); }

    const std::vector<int>& getVisSurfaceTriIds() const { return visSurfaceTriIds; }
    size_t getVisSurfaceTriIdsSize() const { return visSurfaceTriIds.size(); }

    const std::vector<float>& getVisUVs() const { return vis_uvs; }
    float*  getVisUVsPtr()  { return vis_uvs.data(); }
    size_t  getVisUVsSize() const { return vis_uvs.size(); }
    bool    hasUVs()        const { return !vis_uvs.empty(); }

    std::vector<float> smoothedVertices;
    std::vector<int>   smoothSurfaceTriIds;
    bool smoothDisplayMode = true;

    const std::vector<float>& getSmoothVertices() const {
        return smoothDisplayMode ? smoothedVertices : vis_positions;
    }
    const std::vector<int>& getSmoothTriIds() const {
        return smoothDisplayMode ? smoothSurfaceTriIds : visSurfaceTriIds;
    }
    void setSmoothDisplayMode(bool mode) { smoothDisplayMode = mode; }
    void updateSmoothMesh();

    const std::vector<float>& getTetEdgeVertices() const { return tetEdgeVertices; }
    float*  getTetEdgeVerticesPtr()  { return tetEdgeVertices.data(); }
    size_t  getTetEdgeVerticesSize() const { return tetEdgeVertices.size(); }

    const MeshData& getMeshData()    const { return meshData; }
    const MeshData& getVisMeshData() const { return vismeshData; }

    void  setEdgeCompliance(float c) { edgeCompliance = c; }
    float getEdgeCompliance()  const { return edgeCompliance; }
    void  setVolCompliance(float c)  { volCompliance = c; }
    float getVolCompliance()   const { return volCompliance; }
    void  setDamping(float d)        { damping = d; }
    float getDamping()         const { return damping; }
    void  setGravity(float x, float y, float z) { gravity = glm::vec3(x, y, z); }
    void  setGroundY(float y)        { groundY = y; }
    float getGroundY()         const { return groundY; }

    void step(float dt, int numSubsteps = 1);

    // ★ 個別頂点のinvMassを設定（固定ピン用）
    void setInvMass(size_t i, float w) { if (i < invMasses.size()) invMasses[i] = w; }
    float getInvMass(size_t i) const   { return (i < invMasses.size()) ? invMasses[i] : 0.0f; }

    // =========================================================
    // コリジョン有効/無効フラグ（デバッグ計測用）
    // =========================================================
    void setCollisionEnabled(bool e) { collisionEnabled_ = e; }
    bool getCollisionEnabled() const { return collisionEnabled_; }
    // =========================================================

    bool isGrabbing() const { return grabId >= 0; }
    int  getGrabId()  const { return grabId; }

    std::vector<float> edgeLambdas;
    std::vector<float> volLambdas;

    static MeshData loadTetMesh(const std::string& filename);
    static MeshData ReadVertexAndFace(const std::string& objPath);
    static MeshData loadTetMeshFromString(const std::string& data);
    static MeshData ReadVertexAndFaceFromString(const std::string& data);

private:
    const MeshData meshData;
    const MeshData vismeshData;

    size_t numParticles;
    size_t numTets;
    size_t numVisVerts;
    size_t numVisParticles;

    std::vector<float> positions;
    std::vector<float> prevPositions;
    std::vector<float> velocities;

    std::vector<int>   tetIds;
    std::vector<int>   tetSurfaceTriIds;
    std::vector<int>   edgeIds;

    std::vector<float> restVols;
    std::vector<float> edgeLengths;
    std::vector<float> invMasses;
    std::vector<float> oldInvMasses;

    std::vector<float> vis_positions;
    std::vector<float> vis_normals;
    std::vector<float> vis_uvs;
    std::vector<int>   visSurfaceTriIds;
    std::vector<float> skinningInfo;
    std::vector<float> tetEdgeVertices;

    float edgeCompliance;
    float volCompliance;
    float damping  = 0.99f;
    float groundY  = -2.0f;
    glm::vec3 gravity = glm::vec3(0.0f, -9.8f, 0.0f);

    int   grabId      = -1;
    float grabInvMass = 0.0f;
    std::vector<int> activeParticles;

    std::vector<float> tempBuffer;
    std::vector<float> grads;

    // ★ 球コライダーリスト（非所有ポインタ）
    std::vector<SphereColliderPhysics*> sphereColliders_;

    bool collisionEnabled_ = true;  // false でコリジョン完全無効（計測用）

    // =========================================================
    // ★ エッジBFS コリジョン最適化
    // =========================================================
    std::vector<int>               tetSurfaceVertIds_;  // 表面頂点（重複なし）
    std::vector<std::vector<int>>  surfaceAdjacency_;   // 表面エッジ隣接リスト

    void buildTetSurfaceVertIds();
    void buildSurfaceAdjacency();
    void collectVertsInRadius(const glm::vec3& center, float searchRadius,
                              std::vector<int>& outVerts) const;

    // ★ レイキャスト内外判定（6方向多数決）
    // 球中心がtetSurfaceTriIdsで囲まれたメッシュの内部にあるか判定
    bool isInsideMesh(const glm::vec3& point) const;
    // =========================================================

    const std::vector<std::vector<int>> volIdOrder = {
        {1,3,2}, {0,2,3}, {0,3,1}, {0,1,2}
    };

    float getTetVolume(int nr);
    void  solveEdges(float compliance, float dt);
    void  solveVolumes(float compliance, float dt);
    void  solveSphereCollisions();
    void  updateVisMesh();
    void  updateTetEdgeVertices();
    void  computeVisNormals();
    void  computeSkinningInfo(const std::vector<float>& visVerts);
    void  clear();
};

#endif
