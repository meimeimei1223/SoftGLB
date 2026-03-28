// ネイティブビルドでは SphereCollider をそのまま使う
// WASM ビルドでは SphereColliderPhysics を使う
#ifdef __EMSCRIPTEN__
#  include "SphereColliderPhysics.h"
#else
#  include "SphereCollider.h"
typedef SphereCollider SphereColliderPhysics;
#endif
#include "SoftBodyPhysics.h"
#include "tiny_obj_loader.h"
#include <unordered_set>

static SoftBodyPhysics::MeshData buildMeshData(
    const tinyobj::attrib_t& attrib,
    const std::vector<tinyobj::shape_t>& shapes);

SoftBodyPhysics::SoftBodyPhysics(const MeshData& tetMesh, const MeshData& visMesh,
                                 float edgeCompliance, float volCompliance)
    : meshData(tetMesh)
    , vismeshData(visMesh)
    , edgeCompliance(edgeCompliance)
    , volCompliance(volCompliance)
{
    std::cout << "SoftBodyPhysics: tet=" << tetMesh.verts.size()/3
              << " verts, " << tetMesh.tetIds.size()/4 << " tets"
              << " | vis=" << visMesh.verts.size()/3 << " verts" << std::endl;

    numParticles    = tetMesh.verts.size() / 3;
    numTets         = tetMesh.tetIds.size() / 4;
    numVisVerts     = visMesh.verts.size() / 3;
    numVisParticles = visMesh.verts.size() / 3;

    positions     = tetMesh.verts;
    prevPositions = tetMesh.verts;
    velocities.resize(3 * numParticles, 0.0f);

    tetIds           = tetMesh.tetIds;
    tetSurfaceTriIds = tetMesh.tetSurfaceTriIds;
    edgeIds          = tetMesh.tetEdgeIds;

    vis_positions    = visMesh.verts;
    vis_normals.resize(3 * numVisVerts, 0.0f);
    visSurfaceTriIds = visMesh.tetSurfaceTriIds;
    vis_uvs          = visMesh.uvs;

    restVols.resize(numTets, 0.0f);
    edgeLengths.resize(edgeIds.size() / 2, 0.0f);
    invMasses.resize(numParticles, 0.0f);

    edgeLambdas.resize(edgeIds.size() / 2, 0.0f);
    volLambdas.resize(numTets, 0.0f);

    tempBuffer.resize(4 * 3, 0.0f);
    grads.resize(4 * 3, 0.0f);

    skinningInfo.resize(4 * numVisVerts, -1.0f);
    computeSkinningInfo(visMesh.verts);

    tetEdgeVertices.resize(meshData.tetEdgeIds.size() * 3, 0.0f);

    initPhysics();
    updateAllMeshes();
}

SoftBodyPhysics::~SoftBodyPhysics() {
    clear();
}

void SoftBodyPhysics::initPhysics() {
    std::fill(invMasses.begin(),   invMasses.end(),   0.0f);
    std::fill(restVols.begin(),    restVols.end(),    0.0f);
    std::fill(edgeLambdas.begin(), edgeLambdas.end(), 0.0f);
    std::fill(volLambdas.begin(),  volLambdas.end(),  0.0f);

    for (size_t i = 0; i < numTets; i++) {
        float vol = getTetVolume(i);
        restVols[i] = vol;
        float pInvMass = vol > 0.0f ? 1.0f / (vol / 4000000.0f) : 1000000.0f;
        for (int j = 0; j < 4; j++)
            invMasses[tetIds[4*i+j]] += pInvMass;
    }

    for (size_t i = 0; i < edgeLengths.size(); i++) {
        int id0 = edgeIds[2*i];
        int id1 = edgeIds[2*i+1];
        edgeLengths[i] = std::sqrt(VectorMath::vecDistSquared(positions, id0, positions, id1));
    }

    // ★ tet表面頂点インデックスと隣接リストを構築（一度だけ）
    buildTetSurfaceVertIds();
    buildSurfaceAdjacency();
}

float SoftBodyPhysics::getTetVolume(int nr) {
    int id0 = tetIds[4*nr];
    int id1 = tetIds[4*nr+1];
    int id2 = tetIds[4*nr+2];
    int id3 = tetIds[4*nr+3];

    VectorMath::vecSetDiff(tempBuffer, 0, positions, id1, positions, id0);
    VectorMath::vecSetDiff(tempBuffer, 1, positions, id2, positions, id0);
    VectorMath::vecSetDiff(tempBuffer, 2, positions, id3, positions, id0);
    VectorMath::vecSetCross(tempBuffer, 3, tempBuffer, 0, tempBuffer, 1);

    return VectorMath::vecDot(tempBuffer, 3, tempBuffer, 2) / 6.0f;
}

// ★ tetSurfaceTriIds から重複なし頂点インデックスを構築
void SoftBodyPhysics::buildTetSurfaceVertIds() {
    std::unordered_set<int> seen;
    tetSurfaceVertIds_.clear();
    for (int idx : tetSurfaceTriIds) {
        if (seen.insert(idx).second)
            tetSurfaceVertIds_.push_back(idx);
    }
    std::cout << "[SoftBodyPhysics] tet surface verts: "
              << tetSurfaceVertIds_.size() << " / " << numParticles << std::endl;
}

// ★ 表面頂点の隣接リストを構築
void SoftBodyPhysics::buildSurfaceAdjacency() {
    surfaceAdjacency_.assign(numParticles, {});
    std::vector<std::unordered_set<int>> adjSet(numParticles);
    for (size_t i = 0; i < tetSurfaceTriIds.size(); i += 3) {
        int v0 = tetSurfaceTriIds[i];
        int v1 = tetSurfaceTriIds[i+1];
        int v2 = tetSurfaceTriIds[i+2];
        adjSet[v0].insert(v1); adjSet[v0].insert(v2);
        adjSet[v1].insert(v0); adjSet[v1].insert(v2);
        adjSet[v2].insert(v0); adjSet[v2].insert(v1);
    }
    for (size_t i = 0; i < numParticles; i++)
        surfaceAdjacency_[i].assign(adjSet[i].begin(), adjSet[i].end());
    std::cout << "[SoftBodyPhysics] surface adjacency built." << std::endl;
}

// ★ エッジBFS：球中心から searchRadius 以内の表面頂点を収集
void SoftBodyPhysics::collectVertsInRadius(const glm::vec3& center,
                                           float searchRadius,
                                           std::vector<int>& outVerts) const {
    outVerts.clear();
    if (tetSurfaceVertIds_.empty()) return;

    float r2 = searchRadius * searchRadius;

    // Step1: 起点を線形探索で1つ見つける O(S)
    int   seedId   = -1;
    float minDist2 = std::numeric_limits<float>::max();
    for (int vi : tetSurfaceVertIds_) {
        glm::vec3 p(positions[vi*3], positions[vi*3+1], positions[vi*3+2]);
        float d2 = glm::dot(p - center, p - center);
        if (d2 < minDist2) { minDist2 = d2; seedId = vi; }
    }
    if (seedId < 0) return;

    // 起点が探索半径外なら即リターン
    {
        glm::vec3 seed(positions[seedId*3], positions[seedId*3+1], positions[seedId*3+2]);
        if (glm::dot(seed - center, seed - center) > r2) return;
    }

    // Step2: BFSで隣接頂点を展開
    std::vector<bool> visited(numParticles, false);
    std::queue<int>   q;
    visited[seedId] = true;
    q.push(seedId);

    while (!q.empty()) {
        int vi = q.front(); q.pop();
        glm::vec3 p(positions[vi*3], positions[vi*3+1], positions[vi*3+2]);
        if (glm::dot(p - center, p - center) > r2) continue;
        outVerts.push_back(vi);
        for (int ni : surfaceAdjacency_[vi]) {
            if (!visited[ni]) { visited[ni] = true; q.push(ni); }
        }
    }
}

// ★ 表面頂点までの最短距離
float SoftBodyPhysics::getMinDistToSurface(const glm::vec3& point) const {
    float minD = std::numeric_limits<float>::max();
    for (int vi : tetSurfaceVertIds_) {
        glm::vec3 p(positions[vi*3], positions[vi*3+1], positions[vi*3+2]);
        float d = glm::length(p - point);
        if (d < minD) minD = d;
    }
    return minD;
}

// ★ 最近傍表面頂点 + 外向き法線（Collide&Slide用）
float SoftBodyPhysics::getClosestSurfacePoint(const glm::vec3& point,
                                              glm::vec3& closestPos,
                                              glm::vec3& surfNormal) const {
    float minD = std::numeric_limits<float>::max();
    closestPos = point;
    surfNormal = glm::vec3(0, 1, 0);
    for (int vi : tetSurfaceVertIds_) {
        glm::vec3 p(positions[vi*3], positions[vi*3+1], positions[vi*3+2]);
        float d = glm::length(p - point);
        if (d < minD) { minD = d; closestPos = p; }
    }
    glm::vec3 diff = point - closestPos;
    float len = glm::length(diff);
    if (len > 1e-6f) surfNormal = diff / len;
    return minD;
}

// ★ レイキャスト内外判定（Möller–Trumbore + 6方向多数決）
// tetSurfaceTriIds の三角形に対してレイを飛ばし、奇数交差なら内側
bool SoftBodyPhysics::isInsideMesh(const glm::vec3& point) const {
    if (tetSurfaceTriIds.empty()) return false;

    static const glm::vec3 dirs[6] = {
        { 1,0,0},{-1,0,0},{ 0,1,0},{0,-1,0},{0,0, 1},{0,0,-1}
    };

    int insideVotes = 0;
    for (const auto& dir : dirs) {
        int crossings = 0;
        for (size_t i = 0; i < tetSurfaceTriIds.size(); i += 3) {
            int v0 = tetSurfaceTriIds[i];
            int v1 = tetSurfaceTriIds[i+1];
            int v2 = tetSurfaceTriIds[i+2];
            glm::vec3 a(positions[v0*3], positions[v0*3+1], positions[v0*3+2]);
            glm::vec3 b(positions[v1*3], positions[v1*3+1], positions[v1*3+2]);
            glm::vec3 c(positions[v2*3], positions[v2*3+1], positions[v2*3+2]);

            // Möller–Trumbore
            glm::vec3 e1 = b - a, e2 = c - a;
            glm::vec3 h  = glm::cross(dir, e2);
            float     det = glm::dot(e1, h);
            if (std::abs(det) < 1e-7f) continue;
            float f = 1.0f / det;
            glm::vec3 s = point - a;
            float u = f * glm::dot(s, h);
            if (u < 0.0f || u > 1.0f) continue;
            glm::vec3 q = glm::cross(s, e1);
            float v = f * glm::dot(dir, q);
            if (v < 0.0f || u + v > 1.0f) continue;
            float t = f * glm::dot(e2, q);
            if (t > 1e-6f) crossings++;
        }
        if (crossings % 2 == 1) insideVotes++;
    }
    return insideVotes > 3;  // 4方向以上で内側判定
}

// ★ preSolve：速度積分 + 地面判定 + 球コリジョン（BFS + CCD）
void SoftBodyPhysics::preSolve(float dt, const glm::vec3& gravity) {
    for (size_t i = 0; i < numParticles; i++) {
        if (invMasses[i] == 0.0f) continue;
        VectorMath::vecAdd(velocities, i, {gravity.x, gravity.y, gravity.z}, 0, dt);
        VectorMath::vecScale(velocities, i, damping);
        VectorMath::vecCopy(prevPositions, i, positions, i);
        VectorMath::vecAdd(positions, i, velocities, i, dt);
        if (positions[3*i+1] < groundY) {
            VectorMath::vecCopy(positions, i, prevPositions, i);
            positions[3*i+1]  = groundY;
            velocities[3*i+1] = 0.0f;
        }
    }

    // ★ 球コリジョン（レイキャスト内外判定 + CCD）
    for (SphereColliderPhysics* col : sphereColliders_) {
        if (!collisionEnabled_) continue;

        glm::vec3 prevC = col->getPrevCenter();
        glm::vec3 curC  = col->getCenter();
        glm::vec3 move  = curC - prevC;
        float     r     = col->getRadius();
        float     moveLen    = glm::length(move);
        float     searchR    = r + moveLen + 0.05f;
        glm::vec3 searchCenter = (prevC + curC) * 0.5f;

        // レイキャスト内外判定：球が内部にいる間はコリジョン完全無効
        const float cacheThr = 0.005f;
        if (glm::length(curC - col->insideCheckPos_) > cacheThr) {
            col->insideCheckPos_   = curC;
            col->insideCheckCache_ = isInsideMesh(curC);
        }
        if (col->insideCheckCache_) continue;

        std::vector<int> nearVerts;
        collectVertsInRadius(searchCenter, searchR, nearVerts);

        for (int si : nearVerts) {
            if (invMasses[si] == 0.0f) continue;

            glm::vec3 p    (positions    [3*si], positions    [3*si+1], positions    [3*si+2]);
            glm::vec3 pPrev(prevPositions[3*si], prevPositions[3*si+1], prevPositions[3*si+2]);

            // CCD：球とパーティクル双方の移動を考慮
            glm::vec3 pMove   = p - pPrev;
            glm::vec3 relMove = move - pMove;

            glm::vec3 w = pPrev - prevC;
            float a = glm::dot(relMove, relMove);
            float b = -2.0f * glm::dot(w, relMove);
            float c = glm::dot(w, w) - r * r;
            float tHit = 1.0f;

            if (a < 1e-10f) {
                if (c < 0.0f) tHit = 0.0f;
                else          continue;
            } else {
                float disc = b * b - 4.0f * a * c;
                if (disc < 0.0f) continue;
                float sqrtDisc = std::sqrt(disc);
                float t0 = (-b - sqrtDisc) / (2.0f * a);
                float t1 = (-b + sqrtDisc) / (2.0f * a);
                if (t1 < 0.0f || t0 > 1.0f) continue;
                tHit = std::max(0.0f, t0);
            }

            glm::vec3 hitC = prevC + tHit * move;
            glm::vec3 hitP = pPrev + tHit * pMove;
            glm::vec3 diff = hitP - hitC;
            float     dist = glm::length(diff);
            if (dist < 1e-6f) continue;

            glm::vec3 n = diff / dist;

            // ★ 引き抜きチェック：現在位置が既に球の外にある → スキップ
            glm::vec3 curDiff(positions[3*si]   - curC.x,
                              positions[3*si+1] - curC.y,
                              positions[3*si+2] - curC.z);
            if (glm::length(curDiff) >= r) continue;

            positions[3*si]   = hitC.x + n.x * r;
            positions[3*si+1] = hitC.y + n.y * r;
            positions[3*si+2] = hitC.z + n.z * r;

            glm::vec3 vel(velocities[3*si], velocities[3*si+1], velocities[3*si+2]);
            glm::vec3 relVel = vel - col->getVelocity();
            float     vn     = glm::dot(relVel, n);
            if (vn < 0.0f) {
                velocities[3*si]   -= vn * n.x;
                velocities[3*si+1] -= vn * n.y;
                velocities[3*si+2] -= vn * n.z;
            }
        }
    }
}

void SoftBodyPhysics::solve(float dt) {
    for (int i = 0; i < 5; i++) {
        solveEdges(edgeCompliance, dt);
        for (int j = 0; j < 2; j++)
            solveVolumes(volCompliance, dt);
        // ★ コリジョン制約：エッジ制約に上書きされないよう同ループで実行
        solveSphereCollisions();
    }
}

// ★ solve ループ内コリジョン制約（内部頂点を毎イテレーション強制排出）
void SoftBodyPhysics::solveSphereCollisions() {
    for (SphereColliderPhysics* col : sphereColliders_) {
        if (!collisionEnabled_) continue;
        // preSolve で更新済みのレイキャストキャッシュを参照
        // 球が内部にいる間は押し出し完全スキップ
        if (col->insideCheckCache_) continue;

        glm::vec3 curC = col->getCenter();
        float     r    = col->getRadius();
        if (getMinDistToSurface(curC) > r * 2.0f) continue;

        std::vector<int> nearVerts;
        collectVertsInRadius(curC, r + 0.05f, nearVerts);
        for (int si : nearVerts) {
            if (invMasses[si] == 0.0f) continue;
            glm::vec3 p(positions[3*si], positions[3*si+1], positions[3*si+2]);
            glm::vec3 diff = p - curC;
            float dist = glm::length(diff);
            if (dist >= r || dist < 1e-6f) continue;
            glm::vec3 n = diff / dist;
            positions[3*si]   = curC.x + n.x * r;
            positions[3*si+1] = curC.y + n.y * r;
            positions[3*si+2] = curC.z + n.z * r;
        }
    }
}

void SoftBodyPhysics::solveEdges(float compliance, float dt) {
    float alpha = compliance / (dt * dt);

    for (size_t i = 0; i < edgeLengths.size(); i++) {
        int   id0 = edgeIds[2*i];
        int   id1 = edgeIds[2*i+1];
        float w0  = invMasses[id0];
        float w1  = invMasses[id1];
        float w   = w0 + w1;
        if (w == 0.0f) continue;

        VectorMath::vecSetDiff(grads, 0, positions, id0, positions, id1);
        float len = std::sqrt(VectorMath::vecLengthSquared(grads, 0));
        if (len == 0.0f) continue;

        VectorMath::vecScale(grads, 0, 1.0f / len);
        float C       = len - edgeLengths[i];
        float dLambda = -(C + alpha * edgeLambdas[i]) / (w + alpha);
        edgeLambdas[i] += dLambda;

        VectorMath::vecAdd(positions, id0, grads, 0,  dLambda * w0);
        VectorMath::vecAdd(positions, id1, grads, 0, -dLambda * w1);
    }
}

void SoftBodyPhysics::solveVolumes(float compliance, float dt) {
    float alpha = compliance / (dt * dt);

    for (size_t i = 0; i < numTets; i++) {
        float w = 0.0f;

        for (int j = 0; j < 4; j++) {
            int id0 = tetIds[4*i + volIdOrder[j][0]];
            int id1 = tetIds[4*i + volIdOrder[j][1]];
            int id2 = tetIds[4*i + volIdOrder[j][2]];

            VectorMath::vecSetDiff(tempBuffer, 0, positions, id1, positions, id0);
            VectorMath::vecSetDiff(tempBuffer, 1, positions, id2, positions, id0);
            VectorMath::vecSetCross(grads, j, tempBuffer, 0, tempBuffer, 1);
            VectorMath::vecScale(grads, j, 1.0f / 6.0f);

            w += invMasses[tetIds[4*i+j]] * VectorMath::vecLengthSquared(grads, j);
        }

        if (w == 0.0f) continue;

        float C       = getTetVolume(i) - restVols[i];
        float dLambda = -(C + alpha * volLambdas[i]) / (w + alpha);
        volLambdas[i] += dLambda;

        for (int j = 0; j < 4; j++) {
            int id = tetIds[4*i+j];
            VectorMath::vecAdd(positions, id, grads, j, dLambda * invMasses[id]);
        }
    }
}

void SoftBodyPhysics::postSolve(float dt) {
    for (size_t i = 0; i < numParticles; i++) {
        if (invMasses[i] == 0.0f) continue;
        VectorMath::vecSetDiff(velocities, i, positions, i, prevPositions, i, 1.0f / dt);
    }
}

void SoftBodyPhysics::step(float dt, int numSubsteps) {
    float stepDt = dt / float(numSubsteps);
    for (int i = 0; i < numSubsteps; i++) {
        preSolve(stepDt, gravity);
        solve(stepDt);
        postSolve(stepDt);
    }
    updateAllMeshes();
}

void SoftBodyPhysics::computeSkinningInfo(const std::vector<float>& visVerts) {
    std::cout << "Computing skinning info..." << std::endl;

    glm::vec3 tetMin(std::numeric_limits<float>::max());
    glm::vec3 tetMax(std::numeric_limits<float>::lowest());

    for (size_t i = 0; i < positions.size(); i += 3) {
        tetMin.x = std::min(tetMin.x, positions[i]);
        tetMin.y = std::min(tetMin.y, positions[i+1]);
        tetMin.z = std::min(tetMin.z, positions[i+2]);
        tetMax.x = std::max(tetMax.x, positions[i]);
        tetMax.y = std::max(tetMax.y, positions[i+1]);
        tetMax.z = std::max(tetMax.z, positions[i+2]);
    }

    glm::vec3 tetSize = tetMax - tetMin;
    float maxSize = std::max({tetSize.x, tetSize.y, tetSize.z});
    float spacing = maxSize * 1.0f;

    Hash hash(spacing, numVisVerts);
    hash.create(visVerts);

    skinningInfo.assign(4 * numVisVerts, -1.0f);
    std::vector<float> minDist(numVisVerts, std::numeric_limits<float>::max());
    const float border = 0.05f;

    std::vector<float> tetCenter(3, 0.0f);
    std::vector<float> mat(9, 0.0f);
    std::vector<float> bary(4, 0.0f);

    for (size_t i = 0; i < numTets; i++) {
        std::fill(tetCenter.begin(), tetCenter.end(), 0.0f);
        for (int j = 0; j < 4; j++)
            VectorMath::vecAdd(tetCenter, 0, positions, tetIds[4*i+j], 0.25f);

        float rMax = 0.0f;
        for (int j = 0; j < 4; j++) {
            float r2 = VectorMath::vecDistSquared(tetCenter, 0, positions, tetIds[4*i+j]);
            rMax = std::max(rMax, std::sqrt(r2));
        }
        rMax += border;

        hash.query(tetCenter, 0, rMax);
        if (hash.querySize == 0) continue;

        int id0 = tetIds[4*i];
        int id1 = tetIds[4*i+1];
        int id2 = tetIds[4*i+2];
        int id3 = tetIds[4*i+3];

        VectorMath::vecSetDiff(mat, 0, positions, id0, positions, id3);
        VectorMath::vecSetDiff(mat, 1, positions, id1, positions, id3);
        VectorMath::vecSetDiff(mat, 2, positions, id2, positions, id3);
        VectorMath::matSetInverse(mat);

        for (int j = 0; j < hash.querySize; j++) {
            int id = hash.queryIds[j];
            if (minDist[id] <= 0.0f) continue;
            if (VectorMath::vecDistSquared(visVerts, id, tetCenter, 0) > rMax * rMax) continue;

            VectorMath::vecSetDiff(bary, 0, visVerts, id, positions, id3);
            VectorMath::matSetMult(mat, bary, 0, bary, 0);
            bary[3] = 1.0f - bary[0] - bary[1] - bary[2];

            float dist = 0.0f;
            for (int k = 0; k < 4; k++)
                dist = std::max(dist, -bary[k]);

            if (dist < minDist[id]) {
                minDist[id]          = dist;
                skinningInfo[4*id]   = static_cast<float>(i);
                skinningInfo[4*id+1] = bary[0];
                skinningInfo[4*id+2] = bary[1];
                skinningInfo[4*id+3] = bary[2];
            }
        }
    }

    std::cout << "Skinning info done." << std::endl;
}

void SoftBodyPhysics::updateVisMesh() {
    vis_positions.resize(3 * numVisVerts, 0.0f);

    int nr = 0;
    for (size_t i = 0; i < numVisVerts; i++) {
        int tetNr = static_cast<int>(skinningInfo[nr++]) * 4;
        if (tetNr < 0) { nr += 3; continue; }

        float b0 = skinningInfo[nr++];
        float b1 = skinningInfo[nr++];
        float b2 = skinningInfo[nr++];
        float b3 = 1.0f - b0 - b1 - b2;

        int id0 = tetIds[tetNr++];
        int id1 = tetIds[tetNr++];
        int id2 = tetIds[tetNr++];
        int id3 = tetIds[tetNr++];

        VectorMath::vecSetZero(vis_positions, i);
        VectorMath::vecAdd(vis_positions, i, positions, id0, b0);
        VectorMath::vecAdd(vis_positions, i, positions, id1, b1);
        VectorMath::vecAdd(vis_positions, i, positions, id2, b2);
        VectorMath::vecAdd(vis_positions, i, positions, id3, b3);
    }

    computeVisNormals();
}

void SoftBodyPhysics::updateTetEdgeVertices() {
    tetEdgeVertices.clear();
    tetEdgeVertices.reserve(meshData.tetEdgeIds.size() * 3);

    for (size_t i = 0; i < meshData.tetEdgeIds.size(); i += 2) {
        int id0 = meshData.tetEdgeIds[i];
        int id1 = meshData.tetEdgeIds[i+1];

        tetEdgeVertices.push_back(positions[id0*3]);
        tetEdgeVertices.push_back(positions[id0*3+1]);
        tetEdgeVertices.push_back(positions[id0*3+2]);

        tetEdgeVertices.push_back(positions[id1*3]);
        tetEdgeVertices.push_back(positions[id1*3+1]);
        tetEdgeVertices.push_back(positions[id1*3+2]);
    }
}

void SoftBodyPhysics::updateAllMeshes() {
    updateTetEdgeVertices();
    updateVisMesh();
    updateSmoothMesh();
}

void SoftBodyPhysics::updateSmoothMesh() {
    smoothedVertices    = vis_positions;
    smoothSurfaceTriIds = visSurfaceTriIds;
}

void SoftBodyPhysics::computeVisNormals() {
    std::vector<glm::vec3> normals(numVisVerts, glm::vec3(0.0f));

    for (size_t i = 0; i < vismeshData.tetSurfaceTriIds.size(); i += 3) {
        int id0 = vismeshData.tetSurfaceTriIds[i];
        int id1 = vismeshData.tetSurfaceTriIds[i+1];
        int id2 = vismeshData.tetSurfaceTriIds[i+2];

        glm::vec3 p0(vis_positions[id0*3], vis_positions[id0*3+1], vis_positions[id0*3+2]);
        glm::vec3 p1(vis_positions[id1*3], vis_positions[id1*3+1], vis_positions[id1*3+2]);
        glm::vec3 p2(vis_positions[id2*3], vis_positions[id2*3+1], vis_positions[id2*3+2]);

        glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        normals[id0] += n;
        normals[id1] += n;
        normals[id2] += n;
    }

    vis_normals.resize(numVisVerts * 3);
    for (size_t i = 0; i < numVisVerts; i++) {
        glm::vec3 n = glm::length(normals[i]) > 0.0f ? glm::normalize(normals[i]) : normals[i];
        vis_normals[i*3]   = n.x;
        vis_normals[i*3+1] = n.y;
        vis_normals[i*3+2] = n.z;
    }
}

void SoftBodyPhysics::startGrab(const glm::vec3& pos) {
    float minD2 = std::numeric_limits<float>::max();
    grabId = -1;

    struct PD { int id; float d; };
    std::vector<PD> sorted;
    activeParticles.clear();

    for (size_t i = 0; i < numParticles; i++) {
        glm::vec3 p(positions[i*3], positions[i*3+1], positions[i*3+2]);
        float d2 = glm::dot(p - pos, p - pos);
        if (d2 < minD2) { minD2 = d2; grabId = i; }
        sorted.push_back({static_cast<int>(i), d2});
    }

    std::sort(sorted.begin(), sorted.end(), [](const PD& a, const PD& b){ return a.d < b.d; });

    int n = std::max(1, static_cast<int>(sorted.size()));
    for (int i = 0; i < n; i++) activeParticles.push_back(sorted[i].id);

    oldInvMasses = invMasses;
    std::fill(invMasses.begin(), invMasses.end(), 0.0f);
    for (int id : activeParticles) invMasses[id] = oldInvMasses[id];

    invMasses[grabId]         = 0.0f;
    positions[grabId*3]       = pos.x;
    positions[grabId*3+1]     = pos.y;
    positions[grabId*3+2]     = pos.z;
}

void SoftBodyPhysics::startGrab(float x, float y, float z) {
    startGrab(glm::vec3(x, y, z));
}

void SoftBodyPhysics::moveGrabbed(const glm::vec3& pos, const glm::vec3& vel) {
    if (grabId >= 0) {
        positions[grabId*3]   = pos.x;
        positions[grabId*3+1] = pos.y;
        positions[grabId*3+2] = pos.z;
    }
}

void SoftBodyPhysics::moveGrabbed(float x, float y, float z, float vx, float vy, float vz) {
    moveGrabbed(glm::vec3(x, y, z), glm::vec3(vx, vy, vz));
}

void SoftBodyPhysics::endGrab(const glm::vec3& pos, const glm::vec3& vel) {
    if (grabId >= 0) {
        for (int id : activeParticles) invMasses[id] = oldInvMasses[id];
        velocities[grabId*3]   = vel.x;
        velocities[grabId*3+1] = vel.y;
        velocities[grabId*3+2] = vel.z;
        grabId = -1;
        activeParticles.clear();
    }
}

void SoftBodyPhysics::endGrab(float x, float y, float z, float vx, float vy, float vz) {
    endGrab(glm::vec3(x, y, z), glm::vec3(vx, vy, vz));
}

void SoftBodyPhysics::applyShapeRestoration(float strength) {
    for (size_t i = 0; i < numParticles; i++) {
        glm::vec3 rest(meshData.verts[i*3], meshData.verts[i*3+1], meshData.verts[i*3+2]);
        glm::vec3 cur(positions[i*3], positions[i*3+1], positions[i*3+2]);
        glm::vec3 c = (rest - cur) * strength;
        positions[i*3]   += c.x;
        positions[i*3+1] += c.y;
        positions[i*3+2] += c.z;
    }

    for (size_t i = 0; i < numVisParticles; i++) {
        glm::vec3 rest(vismeshData.verts[i*3], vismeshData.verts[i*3+1], vismeshData.verts[i*3+2]);
        glm::vec3 cur(vis_positions[i*3], vis_positions[i*3+1], vis_positions[i*3+2]);
        glm::vec3 c = (rest - cur) * strength;
        vis_positions[i*3]   += c.x;
        vis_positions[i*3+1] += c.y;
        vis_positions[i*3+2] += c.z;
    }

    std::fill(velocities.begin(), velocities.end(), 0.0f);
    initPhysics();
}

void SoftBodyPhysics::clear() {
    positions.clear();      prevPositions.clear();   velocities.clear();
    tetIds.clear();         edgeIds.clear();          tetSurfaceTriIds.clear();
    restVols.clear();       edgeLengths.clear();      invMasses.clear();
    vis_positions.clear();  vis_normals.clear();      vis_uvs.clear();
    visSurfaceTriIds.clear(); skinningInfo.clear();   tetEdgeVertices.clear();
    tempBuffer.clear();     grads.clear();
    activeParticles.clear(); oldInvMasses.clear();
    sphereColliders_.clear();
    grabId = -1;  grabInvMass = 0.0f;
    numParticles = 0;  numTets = 0;  numVisVerts = 0;  numVisParticles = 0;
}

SoftBodyPhysics::MeshData SoftBodyPhysics::loadTetMesh(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: cannot open " << filename << std::endl;
        return MeshData();
    }
    std::stringstream buf;
    buf << file.rdbuf();
    return loadTetMeshFromString(buf.str());
}

SoftBodyPhysics::MeshData SoftBodyPhysics::loadTetMeshFromString(const std::string& data) {
    MeshData result;
    std::istringstream stream(data);
    std::string line;
    bool rv = false, rt = false, re = false, rs = false;

    while (std::getline(stream, line)) {
        if      (line == "VERTICES")          { rv=true;  rt=re=rs=false; continue; }
        else if (line == "TETRAHEDRA")        { rt=true;  rv=re=rs=false; continue; }
        else if (line == "EDGES")             { re=true;  rv=rt=rs=false; continue; }
        else if (line == "SURFACE_TRIANGLES") { rs=true;  rv=rt=re=false; continue; }

        std::istringstream ss(line);
        if (rv) {
            float x, y, z; ss >> x >> y >> z;
            result.verts.push_back(x); result.verts.push_back(y); result.verts.push_back(z);
        } else if (rt) {
            int v0,v1,v2,v3; ss >> v0 >> v1 >> v2 >> v3;
            result.tetIds.push_back(v0); result.tetIds.push_back(v1);
            result.tetIds.push_back(v2); result.tetIds.push_back(v3);
        } else if (re) {
            int e0,e1; ss >> e0 >> e1;
            result.tetEdgeIds.push_back(e0); result.tetEdgeIds.push_back(e1);
        } else if (rs) {
            int t0,t1,t2; ss >> t0 >> t1 >> t2;
            result.tetSurfaceTriIds.push_back(t0);
            result.tetSurfaceTriIds.push_back(t1);
            result.tetSurfaceTriIds.push_back(t2);
        }
    }

    std::cout << "loadTetMesh: " << result.verts.size()/3 << " verts, "
              << result.tetIds.size()/4 << " tets" << std::endl;
    return result;
}

SoftBodyPhysics::MeshData SoftBodyPhysics::ReadVertexAndFace(const std::string& objPath) {
    std::string baseDir = objPath.substr(0, objPath.find_last_of("/\\") + 1);

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                               objPath.c_str(), baseDir.c_str(), true);
    if (!warn.empty()) std::cerr << "tinyobj warn: " << warn << std::endl;
    if (!ok)           { std::cerr << "tinyobj error: " << err << std::endl; return MeshData(); }

    return buildMeshData(attrib, shapes);
}

SoftBodyPhysics::MeshData SoftBodyPhysics::ReadVertexAndFaceFromString(const std::string& data) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::istringstream iss(data);
    bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                               &iss, nullptr, true);
    if (!warn.empty()) std::cerr << "tinyobj warn: " << warn << std::endl;
    if (!ok)           { std::cerr << "tinyobj error: " << err << std::endl; return MeshData(); }

    return buildMeshData(attrib, shapes);
}

static SoftBodyPhysics::MeshData buildMeshData(
    const tinyobj::attrib_t& attrib,
    const std::vector<tinyobj::shape_t>& shapes)
{
    SoftBodyPhysics::MeshData result;
    const bool hasUV = !attrib.texcoords.empty();

    std::map<std::pair<int,int>, int> indexMap;

    for (const auto& shape : shapes) {
        size_t offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
            int fv = static_cast<int>(shape.mesh.num_face_vertices[f]);
            std::vector<int> faceVerts;
            faceVerts.reserve(fv);

            for (int v = 0; v < fv; ++v) {
                tinyobj::index_t idx = shape.mesh.indices[offset + v];
                int posIdx = idx.vertex_index;
                int uvIdx  = (hasUV && idx.texcoord_index >= 0) ? idx.texcoord_index : -1;

                auto key = std::make_pair(posIdx, uvIdx);
                auto it  = indexMap.find(key);
                if (it == indexMap.end()) {
                    int newIdx = static_cast<int>(result.verts.size() / 3);
                    result.verts.push_back(attrib.vertices[3*posIdx]);
                    result.verts.push_back(attrib.vertices[3*posIdx+1]);
                    result.verts.push_back(attrib.vertices[3*posIdx+2]);

                    if (hasUV && uvIdx >= 0) {
                        result.uvs.push_back(attrib.texcoords[2*uvIdx]);
                        result.uvs.push_back(1.0f - attrib.texcoords[2*uvIdx+1]);
                    } else {
                        result.uvs.push_back(0.0f);
                        result.uvs.push_back(0.0f);
                    }

                    indexMap[key] = newIdx;
                    faceVerts.push_back(newIdx);
                } else {
                    faceVerts.push_back(it->second);
                }
            }

            for (int v = 1; v < fv - 1; ++v) {
                result.tetSurfaceTriIds.push_back(faceVerts[0]);
                result.tetSurfaceTriIds.push_back(faceVerts[v]);
                result.tetSurfaceTriIds.push_back(faceVerts[v+1]);
            }
            offset += fv;
        }
    }

    std::cout << "OBJ loaded: " << result.verts.size()/3 << " verts, "
              << result.tetSurfaceTriIds.size()/3 << " tris, UV="
              << (hasUV ? "yes" : "no") << std::endl;
    return result;
}
