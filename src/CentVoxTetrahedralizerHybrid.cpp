#include "CentVoxTetrahedralizerHybrid.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <limits>
#include <iomanip>
#include <omp.h>
#include <unordered_map>  // ← 追加！
// ===============================
// SimpleBVH実装
// ===============================

SimpleBVH::SimpleBVH(const std::vector<GLfloat>& vertices, const std::vector<GLuint>& indices)
    : vertices_(vertices), indices_(indices) {
    build();
}

void SimpleBVH::build() {
    if (indices_.empty()) return;
    std::vector<size_t> allTriangles;
    allTriangles.reserve(indices_.size() / 3);
    for (size_t i = 0; i < indices_.size(); i += 3) {
        allTriangles.push_back(i);
    }
    nodes_.reserve(allTriangles.size() * 2);
    buildRecursive(allTriangles, 0);
}

int SimpleBVH::countIntersections(const glm::vec3& origin, const glm::vec3& direction) const {
    if (nodes_.empty()) return 0;
    int count = 0;
    traverseAndCount(0, origin, direction, count);
    return count;
}

int SimpleBVH::buildRecursive(std::vector<size_t>& triangles, int depth) {
    BVHNode node;
    node.leftChild = -1;
    node.rightChild = -1;
    node.min = glm::vec3(std::numeric_limits<float>::max());
    node.max = glm::vec3(std::numeric_limits<float>::lowest());

    for (size_t triIdx : triangles) {
        glm::vec3 v0 = getVertex(triIdx, 0);
        glm::vec3 v1 = getVertex(triIdx, 1);
        glm::vec3 v2 = getVertex(triIdx, 2);
        node.min = glm::min(node.min, glm::min(glm::min(v0, v1), v2));
        node.max = glm::max(node.max, glm::max(glm::max(v0, v1), v2));
    }

    if (triangles.size() <= MAX_TRIANGLES_PER_LEAF || depth > 20) {
        node.triangles = triangles;
        int nodeIdx = nodes_.size();
        nodes_.push_back(node);
        return nodeIdx;
    }

    glm::vec3 extent = node.max - node.min;
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > extent[axis]) axis = 2;
    float splitPos = (node.min[axis] + node.max[axis]) * 0.5f;

    std::vector<size_t> leftTriangles;
    std::vector<size_t> rightTriangles;
    for (size_t triIdx : triangles) {
        glm::vec3 center = getTriangleCenter(triIdx);
        if (center[axis] < splitPos) {
            leftTriangles.push_back(triIdx);
        } else {
            rightTriangles.push_back(triIdx);
        }
    }

    if (leftTriangles.empty() || rightTriangles.empty()) {
        node.triangles = triangles;
        int nodeIdx = nodes_.size();
        nodes_.push_back(node);
        return nodeIdx;
    }

    int nodeIdx = nodes_.size();
    nodes_.push_back(node);
    int leftIdx = buildRecursive(leftTriangles, depth + 1);
    int rightIdx = buildRecursive(rightTriangles, depth + 1);
    nodes_[nodeIdx].leftChild = leftIdx;
    nodes_[nodeIdx].rightChild = rightIdx;
    return nodeIdx;
}

void SimpleBVH::traverseAndCount(int nodeIdx, const glm::vec3& origin,
                                 const glm::vec3& direction, int& count) const {
    const BVHNode& node = nodes_[nodeIdx];
    if (!rayBoxIntersect(origin, direction, node.min, node.max)) {
        return;
    }

    if (node.leftChild == -1) {
        for (size_t triIdx : node.triangles) {
            if (rayTriangleIntersect(origin, direction, triIdx)) {
                count++;
            }
        }
        return;
    }

    traverseAndCount(node.leftChild, origin, direction, count);
    traverseAndCount(node.rightChild, origin, direction, count);
}

bool SimpleBVH::rayBoxIntersect(const glm::vec3& origin, const glm::vec3& direction,
                                const glm::vec3& boxMin, const glm::vec3& boxMax) const {
    float tmin = (boxMin.x - origin.x) / direction.x;
    float tmax = (boxMax.x - origin.x) / direction.x;
    if (tmin > tmax) std::swap(tmin, tmax);

    float tymin = (boxMin.y - origin.y) / direction.y;
    float tymax = (boxMax.y - origin.y) / direction.y;
    if (tymin > tymax) std::swap(tymin, tymax);

    if (tmin > tymax || tymin > tmax) return false;
    tmin = std::max(tmin, tymin);
    tmax = std::min(tmax, tymax);

    float tzmin = (boxMin.z - origin.z) / direction.z;
    float tzmax = (boxMax.z - origin.z) / direction.z;
    if (tzmin > tzmax) std::swap(tzmin, tzmax);

    if (tmin > tzmax || tzmin > tmax) return false;
    return tmax > 0;
}

bool SimpleBVH::rayTriangleIntersect(const glm::vec3& origin, const glm::vec3& direction,
                                     size_t triIdx) const {
    glm::vec3 v0 = getVertex(triIdx, 0);
    glm::vec3 v1 = getVertex(triIdx, 1);
    glm::vec3 v2 = getVertex(triIdx, 2);

    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 h = glm::cross(direction, edge2);
    float a = glm::dot(edge1, h);

    if (std::abs(a) < 0.000001f) return false;

    float f = 1.0f / a;
    glm::vec3 s = origin - v0;
    float u = f * glm::dot(s, h);

    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(direction, q);

    if (v < 0.0f || u + v > 1.0f) return false;

    float t = f * glm::dot(edge2, q);
    return t > 0.000001f;
}

glm::vec3 SimpleBVH::getVertex(size_t triIdx, int vertexIdx) const {
    GLuint idx = indices_[triIdx + vertexIdx];
    return glm::vec3(vertices_[idx * 3], vertices_[idx * 3 + 1], vertices_[idx * 3 + 2]);
}

glm::vec3 SimpleBVH::getTriangleCenter(size_t triIdx) const {
    glm::vec3 v0 = getVertex(triIdx, 0);
    glm::vec3 v1 = getVertex(triIdx, 1);
    glm::vec3 v2 = getVertex(triIdx, 2);
    return (v0 + v1 + v2) / 3.0f;
}

// ===============================
// CentVoxTetrahedralizerHybrid実装
// ===============================

CentVoxTetrahedralizerHybrid::CentVoxTetrahedralizerHybrid(
    int gridSize,
    const std::string& portalPath,
    const std::string& outputTetPath,
    DetectionMode mode,
    int mergeBlockSize,
    int protectionLayers)
    : gridSize_(gridSize)
    , portalPath_(portalPath)
    , outputTetPath_(outputTetPath)
    , detectionMode_(mode)
    , mergeBlockSize_(mergeBlockSize)
    , protectionLayers_(protectionLayers) {

    voxels_.resize(gridSize_);
    for (int i = 0; i < gridSize_; i++) {
        voxels_[i].resize(gridSize_);
        for (int j = 0; j < gridSize_; j++) {
            voxels_[i][j].resize(gridSize_);
        }
    }
    initializeCube();
}

CentVoxTetrahedralizerHybrid::~CentVoxTetrahedralizerHybrid() {
    // unique_ptrが自動的に解放される
}

const CentVoxTetrahedralizerHybrid::MeshData& CentVoxTetrahedralizerHybrid::getMeshData() const {
    return meshData_;
}

void CentVoxTetrahedralizerHybrid::setSmoothingSettings(const SmoothingSettings& settings) {
    smoothingSettings_ = settings;
}

void CentVoxTetrahedralizerHybrid::execute() {
    std::cout << "\n=== CentVoxTetrahedralizerHybrid Pipeline ===" << std::endl;
    std::cout << "Detection mode: " << getModeString() << std::endl;
    std::cout << "Grid size: " << gridSize_ << "x" << gridSize_ << "x" << gridSize_ << std::endl;

    prepareCube();
    initializeVoxelGrid();
    calculateVoxelOccupancy();
    carveExternalVoxels();
    markInteriorVoxels();
    generateTetrahedra();

    if (smoothingSettings_.enabled) {
        applySurfaceSmoothing();
        if (smoothingSettings_.rescaleToOriginal) {
            rescaleToOriginalSize();
        }
    }

    writeOutputFile();

    std::cout << "[OK] Pipeline complete" << std::endl;
}

MeshDataTypes::SimpleMeshData CentVoxTetrahedralizerHybrid::generateVoxelDisplayMesh() const {
    MeshDataTypes::SimpleMeshData meshData;

    std::vector<glm::vec3> vertices;
    std::vector<GLuint> indices;

    for (int x = 0; x < gridSize_; x++) {
        for (int y = 0; y < gridSize_; y++) {
            for (int z = 0; z < gridSize_; z++) {
                if (voxels_[x][y][z].isRemoved) continue;

                const auto& voxel = voxels_[x][y][z];
                glm::vec3 vMin = voxel.min;
                glm::vec3 vMax = voxel.max;

                glm::vec3 v[8] = {
                    glm::vec3(vMin.x, vMin.y, vMin.z), glm::vec3(vMax.x, vMin.y, vMin.z),
                    glm::vec3(vMax.x, vMax.y, vMin.z), glm::vec3(vMin.x, vMax.y, vMin.z),
                    glm::vec3(vMin.x, vMin.y, vMax.z), glm::vec3(vMax.x, vMin.y, vMax.z),
                    glm::vec3(vMax.x, vMax.y, vMax.z), glm::vec3(vMin.x, vMax.y, vMax.z)
                };

                auto addQuad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
                    GLuint base = vertices.size();
                    vertices.push_back(a);
                    vertices.push_back(b);
                    vertices.push_back(c);
                    vertices.push_back(d);
                    indices.push_back(base + 0);
                    indices.push_back(base + 1);
                    indices.push_back(base + 2);
                    indices.push_back(base + 0);
                    indices.push_back(base + 2);
                    indices.push_back(base + 3);
                };

                if (x == 0 || voxels_[x-1][y][z].isRemoved) addQuad(v[0], v[3], v[7], v[4]);
                if (x == gridSize_-1 || voxels_[x+1][y][z].isRemoved) addQuad(v[1], v[5], v[6], v[2]);
                if (y == 0 || voxels_[x][y-1][z].isRemoved) addQuad(v[0], v[4], v[5], v[1]);
                if (y == gridSize_-1 || voxels_[x][y+1][z].isRemoved) addQuad(v[3], v[2], v[6], v[7]);
                if (z == 0 || voxels_[x][y][z-1].isRemoved) addQuad(v[0], v[1], v[2], v[3]);
                if (z == gridSize_-1 || voxels_[x][y][z+1].isRemoved) addQuad(v[4], v[7], v[6], v[5]);
            }
        }
    }

    // 頂点の重複を削除
    std::map<std::tuple<float, float, float>, GLuint> vertexMap;
    std::vector<glm::vec3> uniqueVertices;
    std::vector<GLuint> newIndices;

    for (size_t i = 0; i < vertices.size(); i++) {
        auto key = std::make_tuple(
            std::round(vertices[i].x * 1000) / 1000,
            std::round(vertices[i].y * 1000) / 1000,
            std::round(vertices[i].z * 1000) / 1000
            );

        auto it = vertexMap.find(key);
        if (it == vertexMap.end()) {
            GLuint newIndex = uniqueVertices.size();
            vertexMap[key] = newIndex;
            uniqueVertices.push_back(vertices[i]);
        }
    }

    for (size_t i = 0; i < indices.size(); i++) {
        glm::vec3 v = vertices[indices[i]];
        auto key = std::make_tuple(
            std::round(v.x * 1000) / 1000,
            std::round(v.y * 1000) / 1000,
            std::round(v.z * 1000) / 1000
            );
        newIndices.push_back(vertexMap[key]);
    }

    // SimpleMeshDataに変換
    meshData.vertices.reserve(uniqueVertices.size() * 3);
    for (const auto& v : uniqueVertices) {
        meshData.vertices.push_back(v.x);
        meshData.vertices.push_back(v.y);
        meshData.vertices.push_back(v.z);
    }

    meshData.indices = newIndices;

    return meshData;
}

std::string CentVoxTetrahedralizerHybrid::getModeString() const {
    switch (detectionMode_) {
    case DetectionMode::INTERSECTION_ONLY: return "INTERSECTION_ONLY";
    case DetectionMode::RAYCAST_ONLY: return "RAYCAST_ONLY";
    case DetectionMode::HYBRID: return "HYBRID";
    default: return "UNKNOWN";
    }
}

void CentVoxTetrahedralizerHybrid::initializeCube() {
    cubeVertices_ = {
        -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
        1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f
    };
    cubeIndices_ = {
        4, 2, 0,  4, 6, 2,  2, 7, 3,  2, 6, 7,  6, 5, 7,  6, 4, 5,
        1, 7, 5,  1, 3, 7,  0, 3, 1,  0, 2, 3,  4, 1, 5,  4, 0, 1
    };
}

CentVoxTetrahedralizerHybrid::BoundingBox
CentVoxTetrahedralizerHybrid::getBoundingBoxFromVertices(const std::vector<GLfloat>& vertices) {
    BoundingBox bbox;
    if (vertices.empty()) {
        bbox.min = bbox.max = bbox.size = bbox.center = glm::vec3(0.0f);
        return bbox;
    }
    bbox.min = glm::vec3(vertices[0], vertices[1], vertices[2]);
    bbox.max = bbox.min;
    for (size_t i = 0; i < vertices.size(); i += 3) {
        glm::vec3 vertex(vertices[i], vertices[i + 1], vertices[i + 2]);
        bbox.min = glm::min(bbox.min, vertex);
        bbox.max = glm::max(bbox.max, vertex);
    }
    bbox.size = bbox.max - bbox.min;
    bbox.center = (bbox.min + bbox.max) * 0.5f;
    return bbox;
}

CentVoxTetrahedralizerHybrid::BoundingBox
CentVoxTetrahedralizerHybrid::getBoundingBoxFromFloatArray(const std::vector<float>& vertices) {
    BoundingBox bbox;
    if (vertices.empty()) {
        bbox.min = bbox.max = bbox.size = bbox.center = glm::vec3(0.0f);
        return bbox;
    }
    bbox.min = glm::vec3(vertices[0], vertices[1], vertices[2]);
    bbox.max = bbox.min;
    for (size_t i = 0; i < vertices.size(); i += 3) {
        glm::vec3 vertex(vertices[i], vertices[i + 1], vertices[i + 2]);
        bbox.min = glm::min(bbox.min, vertex);
        bbox.max = glm::max(bbox.max, vertex);
    }
    bbox.size = bbox.max - bbox.min;
    bbox.center = (bbox.min + bbox.max) * 0.5f;
    return bbox;
}

bool CentVoxTetrahedralizerHybrid::loadPortalMesh() {
    MeshDataTypes::SimpleMeshData tempData = MeshDataTypes::loadOBJFile(portalPath_.c_str());
    if (tempData.vertices.empty()) return false;

    portalVertices_ = tempData.vertices;
    portalIndices_.clear();
    for (unsigned int idx : tempData.indices) {
        portalIndices_.push_back(idx);
    }
    return true;
}

void CentVoxTetrahedralizerHybrid::prepareCube() {
    glm::vec3 center(0.0f);
    size_t vertexCount = cubeVertices_.size() / 3;
    for (size_t i = 0; i < cubeVertices_.size(); i += 3) {
        center.x += cubeVertices_[i];
        center.y += cubeVertices_[i + 1];
        center.z += cubeVertices_[i + 2];
    }
    center /= static_cast<float>(vertexCount);

    for (size_t i = 0; i < cubeVertices_.size(); i += 3) {
        cubeVertices_[i] -= center.x;
        cubeVertices_[i + 1] -= center.y;
        cubeVertices_[i + 2] -= center.z;
    }

    BoundingBox cubeBBox = getBoundingBoxFromVertices(cubeVertices_);
    float maxDim = std::max({cubeBBox.size.x, cubeBBox.size.y, cubeBBox.size.z});
    if (maxDim > 0.0001f) {
        float scale = 2.0f / maxDim;
        for (auto& v : cubeVertices_) v *= scale;
    }

    if (!loadPortalMesh()) {
        throw std::runtime_error("Failed to load portal mesh");
    }

    originalPortalBBox_ = getBoundingBoxFromVertices(portalVertices_);
    BoundingBox portalBBox = originalPortalBBox_;
    cubeBBox = getBoundingBoxFromVertices(cubeVertices_);

    float portalMaxDim = std::max({portalBBox.size.x, portalBBox.size.y, portalBBox.size.z});
    float cubeMaxDim = std::max({cubeBBox.size.x, cubeBBox.size.y, cubeBBox.size.z});
    float targetSize = portalMaxDim * 1.1f;
    float scaleFactor = targetSize / cubeMaxDim;

    for (auto& v : cubeVertices_) v *= scaleFactor;

    cubeBBox = getBoundingBoxFromVertices(cubeVertices_);
    glm::vec3 offset = portalBBox.center - cubeBBox.center;
    for (size_t i = 0; i < cubeVertices_.size(); i += 3) {
        cubeVertices_[i] += offset.x;
        cubeVertices_[i + 1] += offset.y;
        cubeVertices_[i + 2] += offset.z;
    }
}

void CentVoxTetrahedralizerHybrid::initializeVoxelGrid() {
    BoundingBox bbox = getBoundingBoxFromVertices(cubeVertices_);
    gridMin_ = bbox.min;
    gridMax_ = bbox.max;
    voxelSize_ = (gridMax_ - gridMin_) / static_cast<float>(gridSize_);

    for (int x = 0; x < gridSize_; x++) {
        for (int y = 0; y < gridSize_; y++) {
            for (int z = 0; z < gridSize_; z++) {
                Voxel& v = voxels_[x][y][z];
                v.min.x = gridMin_.x + x * voxelSize_.x;
                v.min.y = gridMin_.y + y * voxelSize_.y;
                v.min.z = gridMin_.z + z * voxelSize_.z;
                v.max = v.min + voxelSize_;
                v.center = (v.min + v.max) * 0.5f;
                v.portalTriangleCount = 0;
                v.isRemoved = false;
                v.isInterior = false;
                v.isDeepInterior = false;
                v.triangleIndices.clear();
            }
        }
    }
}

bool CentVoxTetrahedralizerHybrid::triangleIntersectsVoxel(const glm::vec3& v0, const glm::vec3& v1,
                                                           const glm::vec3& v2, const Voxel& voxel) {
    glm::vec3 triMin = glm::min(glm::min(v0, v1), v2);
    glm::vec3 triMax = glm::max(glm::max(v0, v1), v2);
    if (triMax.x < voxel.min.x || triMin.x > voxel.max.x) return false;
    if (triMax.y < voxel.min.y || triMin.y > voxel.max.y) return false;
    if (triMax.z < voxel.min.z || triMin.z > voxel.max.z) return false;

    auto pointInVoxel = [&](const glm::vec3& p) {
        return p.x >= voxel.min.x && p.x <= voxel.max.x &&
               p.y >= voxel.min.y && p.y <= voxel.max.y &&
               p.z >= voxel.min.z && p.z <= voxel.max.z;
    };

    if (pointInVoxel(v0) || pointInVoxel(v1) || pointInVoxel(v2)) return true;

    glm::vec3 centroid = (v0 + v1 + v2) / 3.0f;
    return pointInVoxel(centroid);
}

void CentVoxTetrahedralizerHybrid::calculateVoxelOccupancy() {
    switch (detectionMode_) {
    case DetectionMode::INTERSECTION_ONLY:
        calculateIntersectionOnly();
        break;
    case DetectionMode::RAYCAST_ONLY:
        calculateRaycastOnly();
        break;
    case DetectionMode::HYBRID:
        calculateHybrid();
        break;
    }
}

void CentVoxTetrahedralizerHybrid::calculateRaycastOnly() {
    bvh_ = std::make_unique<SimpleBVH>(portalVertices_, portalIndices_);
    int totalVoxels = gridSize_ * gridSize_ * gridSize_;
    glm::vec3 rayDir(1.0f, 0.0001f, 0.0f);

#pragma omp parallel for schedule(dynamic, 256)
    for (int idx = 0; idx < totalVoxels; idx++) {
        int z = idx / (gridSize_ * gridSize_);
        int y = (idx % (gridSize_ * gridSize_)) / gridSize_;
        int x = idx % gridSize_;

        glm::vec3 point = voxels_[x][y][z].center;
        int intersectionCount = bvh_->countIntersections(point, rayDir);
        if ((intersectionCount % 2) == 1) {
            voxels_[x][y][z].portalTriangleCount = 1;
        }
    }
}

void CentVoxTetrahedralizerHybrid::carveExternalVoxels() {
    bool changed = true;
    int iterations = 0;
    int removedCount = 0;

    while (changed) {
        changed = false;
        iterations++;

        for (int x = 0; x < gridSize_; x++) {
            for (int y = 0; y < gridSize_; y++) {
                for (int z = 0; z < gridSize_; z++) {
                    if (voxels_[x][y][z].isRemoved) continue;
                    if (voxels_[x][y][z].portalTriangleCount > 0) continue;

                    bool exposed = (x == 0 || x == gridSize_-1 ||
                                    y == 0 || y == gridSize_-1 ||
                                    z == 0 || z == gridSize_-1);

                    if (!exposed) {
                        exposed = (x > 0 && voxels_[x-1][y][z].isRemoved) ||
                                  (x < gridSize_-1 && voxels_[x+1][y][z].isRemoved) ||
                                  (y > 0 && voxels_[x][y-1][z].isRemoved) ||
                                  (y < gridSize_-1 && voxels_[x][y+1][z].isRemoved) ||
                                  (z > 0 && voxels_[x][y][z-1].isRemoved) ||
                                  (z < gridSize_-1 && voxels_[x][y][z+1].isRemoved);
                    }

                    if (exposed) {
                        voxels_[x][y][z].isRemoved = true;
                        changed = true;
                        removedCount++;
                    }
                }
            }
        }
    }
}

void CentVoxTetrahedralizerHybrid::markInteriorVoxels() {
    std::vector<std::vector<std::vector<std::vector<bool>>>> layers;
    layers.resize(protectionLayers_);
    for (int layer = 0; layer < protectionLayers_; layer++) {
        layers[layer].resize(gridSize_);
        for (int i = 0; i < gridSize_; i++) {
            layers[layer][i].resize(gridSize_);
            for (int j = 0; j < gridSize_; j++) {
                layers[layer][i][j].resize(gridSize_, false);
            }
        }
    }

    for (int x = 1; x < gridSize_-1; x++) {
        for (int y = 1; y < gridSize_-1; y++) {
            for (int z = 1; z < gridSize_-1; z++) {
                if (voxels_[x][y][z].isRemoved) continue;

                bool allNeighborsActive =
                    !voxels_[x-1][y][z].isRemoved && !voxels_[x+1][y][z].isRemoved &&
                    !voxels_[x][y-1][z].isRemoved && !voxels_[x][y+1][z].isRemoved &&
                    !voxels_[x][y][z-1].isRemoved && !voxels_[x][y][z+1].isRemoved;

                if (allNeighborsActive) {
                    layers[0][x][y][z] = true;
                }
            }
        }
    }

    for (int layer = 1; layer < protectionLayers_; layer++) {
        for (int x = 1; x < gridSize_-1; x++) {
            for (int y = 1; y < gridSize_-1; y++) {
                for (int z = 1; z < gridSize_-1; z++) {
                    if (!layers[layer-1][x][y][z]) continue;

                    bool allNeighborsPrevLayer =
                        layers[layer-1][x-1][y][z] && layers[layer-1][x+1][y][z] &&
                        layers[layer-1][x][y-1][z] && layers[layer-1][x][y+1][z] &&
                        layers[layer-1][x][y][z-1] && layers[layer-1][x][y][z+1];

                    if (allNeighborsPrevLayer) {
                        layers[layer][x][y][z] = true;
                    }
                }
            }
        }
    }

    for (int x = 0; x < gridSize_; x++) {
        for (int y = 0; y < gridSize_; y++) {
            for (int z = 0; z < gridSize_; z++) {
                voxels_[x][y][z].isDeepInterior = layers[protectionLayers_-1][x][y][z];
            }
        }
    }
}


void CentVoxTetrahedralizerHybrid::applySurfaceSmoothing() {
    if (!smoothingSettings_.enabled) return;

    meshData_.smoothedVerts = meshData_.verts;
    meshData_.smoothedSurfaceTriIds = meshData_.tetSurfaceTriIds;

    std::set<int> surfaceVertexSet;
    for (size_t i = 0; i < meshData_.tetSurfaceTriIds.size(); i++) {
        surfaceVertexSet.insert(meshData_.tetSurfaceTriIds[i]);
    }

    std::map<int, std::set<int>> vertexNeighbors;
    for (size_t i = 0; i < meshData_.tetSurfaceTriIds.size(); i += 3) {
        int v0 = meshData_.tetSurfaceTriIds[i];
        int v1 = meshData_.tetSurfaceTriIds[i + 1];
        int v2 = meshData_.tetSurfaceTriIds[i + 2];

        vertexNeighbors[v0].insert(v1);
        vertexNeighbors[v0].insert(v2);
        vertexNeighbors[v1].insert(v0);
        vertexNeighbors[v1].insert(v2);
        vertexNeighbors[v2].insert(v0);
        vertexNeighbors[v2].insert(v1);
    }

    glm::vec3 originalCentroid(0.0f);
    for (int vid : surfaceVertexSet) {
        originalCentroid.x += meshData_.smoothedVerts[vid * 3];
        originalCentroid.y += meshData_.smoothedVerts[vid * 3 + 1];
        originalCentroid.z += meshData_.smoothedVerts[vid * 3 + 2];
    }
    originalCentroid /= static_cast<float>(surfaceVertexSet.size());

    for (int iter = 0; iter < smoothingSettings_.iterations; iter++) {
        std::vector<float> newPositions = meshData_.smoothedVerts;

        for (int vid : surfaceVertexSet) {
            const auto& neighbors = vertexNeighbors[vid];
            if (neighbors.empty()) continue;

            glm::vec3 avgPos(0.0f);
            for (int neighborId : neighbors) {
                avgPos.x += meshData_.smoothedVerts[neighborId * 3];
                avgPos.y += meshData_.smoothedVerts[neighborId * 3 + 1];
                avgPos.z += meshData_.smoothedVerts[neighborId * 3 + 2];
            }
            avgPos /= static_cast<float>(neighbors.size());

            glm::vec3 currentPos(meshData_.smoothedVerts[vid * 3],
                                 meshData_.smoothedVerts[vid * 3 + 1],
                                 meshData_.smoothedVerts[vid * 3 + 2]);
            glm::vec3 newPos = glm::mix(currentPos, avgPos, smoothingSettings_.smoothFactor);

            newPositions[vid * 3] = newPos.x;
            newPositions[vid * 3 + 1] = newPos.y;
            newPositions[vid * 3 + 2] = newPos.z;
        }

        meshData_.smoothedVerts = newPositions;

        if (smoothingSettings_.preserveVolume) {
            glm::vec3 newCentroid(0.0f);
            for (int vid : surfaceVertexSet) {
                newCentroid.x += meshData_.smoothedVerts[vid * 3];
                newCentroid.y += meshData_.smoothedVerts[vid * 3 + 1];
                newCentroid.z += meshData_.smoothedVerts[vid * 3 + 2];
            }
            newCentroid /= static_cast<float>(surfaceVertexSet.size());

            glm::vec3 offset = originalCentroid - newCentroid;
            for (int vid : surfaceVertexSet) {
                meshData_.smoothedVerts[vid * 3] += offset.x;
                meshData_.smoothedVerts[vid * 3 + 1] += offset.y;
                meshData_.smoothedVerts[vid * 3 + 2] += offset.z;
            }
        }
    }

    meshData_.isSmoothed = true;
}

void CentVoxTetrahedralizerHybrid::rescaleToOriginalSize() {
    if (!meshData_.isSmoothed || meshData_.smoothedVerts.empty()) return;

    BoundingBox currentBBox = getBoundingBoxFromFloatArray(meshData_.smoothedVerts);

    glm::vec3 currentSize = currentBBox.size;
    glm::vec3 originalSize = originalPortalBBox_.size;

    glm::vec3 scaleFactors = originalSize / currentSize;
    float uniformScale = (scaleFactors.x + scaleFactors.y + scaleFactors.z) / 3.0f;

    glm::vec3 currentCenter = currentBBox.center;
    glm::vec3 originalCenter = originalPortalBBox_.center;

    for (size_t i = 0; i < meshData_.smoothedVerts.size(); i += 3) {
        glm::vec3 pos(meshData_.smoothedVerts[i],
                      meshData_.smoothedVerts[i + 1],
                      meshData_.smoothedVerts[i + 2]);

        pos -= currentCenter;
        pos *= uniformScale;
        pos += originalCenter;

        meshData_.smoothedVerts[i] = pos.x;
        meshData_.smoothedVerts[i + 1] = pos.y;
        meshData_.smoothedVerts[i + 2] = pos.z;
    }
}

void CentVoxTetrahedralizerHybrid::buildMeshData(const std::vector<glm::vec3>& vertices,
                                                 const std::vector<std::vector<int>>& tetrahedra,
                                                 const std::vector<std::vector<int>>& surfaceTriangles) {
    meshData_.verts.clear();
    meshData_.tetIds.clear();
    meshData_.tetSurfaceTriIds.clear();
    meshData_.smoothedVerts.clear();
    meshData_.smoothedSurfaceTriIds.clear();
    meshData_.isSmoothed = false;

    meshData_.verts.reserve(vertices.size() * 3);
    for (const auto& v : vertices) {
        meshData_.verts.push_back(v.x);
        meshData_.verts.push_back(v.y);
        meshData_.verts.push_back(v.z);
    }

    meshData_.tetIds.reserve(tetrahedra.size() * 4);
    for (const auto& tet : tetrahedra) {
        for (int i = 0; i < 4; i++) {
            meshData_.tetIds.push_back(tet[i]);
        }
    }

    meshData_.tetSurfaceTriIds.reserve(surfaceTriangles.size() * 3);
    for (const auto& tri : surfaceTriangles) {
        for (int i = 0; i < 3; i++) {
            meshData_.tetSurfaceTriIds.push_back(tri[i]);
        }
    }
}

void CentVoxTetrahedralizerHybrid::writeOutputFile() {
    std::ofstream out_file(outputTetPath_);
    if (!out_file.is_open()) {
        std::cerr << "Error: Could not create output file" << std::endl;
        return;
    }

    const std::vector<float>* outputVerts = &meshData_.verts;
    if (meshData_.isSmoothed && !meshData_.smoothedVerts.empty()) {
        outputVerts = &meshData_.smoothedVerts;
    }

    int numVertices = outputVerts->size() / 3;
    int numTetrahedra = meshData_.tetIds.size() / 4;

    out_file << "# CentVoxTetrahedralizerHybrid tetrahedral mesh\n";
    out_file << "# Detection mode: " << getModeString() << "\n";
    out_file << "# Grid: " << gridSize_ << "^3\n";
    out_file << "# Merge block size: " << mergeBlockSize_ << "\n";
    out_file << "# Protection layers: " << protectionLayers_ << "\n";
    out_file << "# Smoothed: " << (meshData_.isSmoothed ? "Yes" : "No") << "\n";
    out_file << "# Vertices: " << numVertices << "\n";
    out_file << "# Tetrahedra: " << numTetrahedra << "\n\n";

    out_file << "VERTICES\n";
    for (int i = 0; i < numVertices; i++) {
        out_file << std::fixed << std::setprecision(6)
                 << (*outputVerts)[i * 3] << " "
                 << (*outputVerts)[i * 3 + 1] << " "
                 << (*outputVerts)[i * 3 + 2] << "\n";
    }

    out_file << "\nTETRAHEDRA\n";
    for (size_t i = 0; i < meshData_.tetIds.size(); i += 4) {
        out_file << meshData_.tetIds[i] << " "
                 << meshData_.tetIds[i + 1] << " "
                 << meshData_.tetIds[i + 2] << " "
                 << meshData_.tetIds[i + 3] << "\n";
    }

    if (gridSize_ <= 200) {
        std::set<std::pair<int, int>> edges;
        for (size_t i = 0; i < meshData_.tetIds.size(); i += 4) {
            int v[4] = {meshData_.tetIds[i], meshData_.tetIds[i+1],
                        meshData_.tetIds[i+2], meshData_.tetIds[i+3]};
            for (int j = 0; j < 4; j++) {
                for (int k = j + 1; k < 4; k++) {
                    edges.insert({std::min(v[j], v[k]), std::max(v[j], v[k])});
                }
            }
        }

        out_file << "\nEDGES\n";
        for (const auto& edge : edges) {
            out_file << edge.first << " " << edge.second << "\n";
        }
    }

    const std::vector<int>* outputSurfaceTris = &meshData_.tetSurfaceTriIds;
    if (meshData_.isSmoothed && !meshData_.smoothedSurfaceTriIds.empty()) {
        outputSurfaceTris = &meshData_.smoothedSurfaceTriIds;
    }

    out_file << "\nSURFACE_TRIANGLES\n";
    for (size_t i = 0; i < outputSurfaceTris->size(); i += 3) {
        out_file << (*outputSurfaceTris)[i] << " "
                 << (*outputSurfaceTris)[i + 1] << " "
                 << (*outputSurfaceTris)[i + 2] << "\n";
    }

    out_file.close();
}



// =============================================================================
// calculateIntersectionOnly() 関数 - 完全版（MSVC対応）
// =============================================================================
void CentVoxTetrahedralizerHybrid::calculateIntersectionOnly() {
    size_t totalTriangles = portalIndices_.size() / 3;
    std::vector<std::vector<std::vector<std::vector<size_t>>>> perVoxelTriangles(gridSize_);
    for (int x = 0; x < gridSize_; x++) {
        perVoxelTriangles[x].resize(gridSize_);
        for (int y = 0; y < gridSize_; y++) {
            perVoxelTriangles[x][y].resize(gridSize_);
        }
    }

#pragma omp parallel
    {
        std::vector<std::vector<std::vector<std::vector<size_t>>>> localTriangles(gridSize_);
        for (int x = 0; x < gridSize_; x++) {
            localTriangles[x].resize(gridSize_);
            for (int y = 0; y < gridSize_; y++) {
                localTriangles[x][y].resize(gridSize_);
            }
        }

#pragma omp for schedule(dynamic, 100) nowait
        for (int64_t triIdx = 0; triIdx < (int64_t)totalTriangles; triIdx++) {
            size_t i = triIdx * 3;
            GLuint idx0 = portalIndices_[i];
            GLuint idx1 = portalIndices_[i + 1];
            GLuint idx2 = portalIndices_[i + 2];

            glm::vec3 v0(portalVertices_[idx0 * 3], portalVertices_[idx0 * 3 + 1], portalVertices_[idx0 * 3 + 2]);
            glm::vec3 v1(portalVertices_[idx1 * 3], portalVertices_[idx1 * 3 + 1], portalVertices_[idx1 * 3 + 2]);
            glm::vec3 v2(portalVertices_[idx2 * 3], portalVertices_[idx2 * 3 + 1], portalVertices_[idx2 * 3 + 2]);

            glm::vec3 triMin = glm::min(glm::min(v0, v1), v2);
            glm::vec3 triMax = glm::max(glm::max(v0, v1), v2);

            int xMin = std::max(0, (int)std::floor((triMin.x - gridMin_.x) / voxelSize_.x));
            int xMax = std::min(gridSize_ - 1, (int)std::floor((triMax.x - gridMin_.x) / voxelSize_.x));
            int yMin = std::max(0, (int)std::floor((triMin.y - gridMin_.y) / voxelSize_.y));
            int yMax = std::min(gridSize_ - 1, (int)std::floor((triMax.y - gridMin_.y) / voxelSize_.y));
            int zMin = std::max(0, (int)std::floor((triMin.z - gridMin_.z) / voxelSize_.z));
            int zMax = std::min(gridSize_ - 1, (int)std::floor((triMax.z - gridMin_.z) / voxelSize_.z));

            for (int x = xMin; x <= xMax; x++) {
                for (int y = yMin; y <= yMax; y++) {
                    for (int z = zMin; z <= zMax; z++) {
                        if (triangleIntersectsVoxel(v0, v1, v2, voxels_[x][y][z])) {
                            localTriangles[x][y][z].push_back(triIdx);
                        }
                    }
                }
            }
        }

#pragma omp critical
        {
            for (int x = 0; x < gridSize_; x++) {
                for (int y = 0; y < gridSize_; y++) {
                    for (int z = 0; z < gridSize_; z++) {
                        if (!localTriangles[x][y][z].empty()) {
                            perVoxelTriangles[x][y][z].insert(
                                perVoxelTriangles[x][y][z].end(),
                                localTriangles[x][y][z].begin(),
                                localTriangles[x][y][z].end()
                                );
                        }
                    }
                }
            }
        }
    }

    for (int x = 0; x < gridSize_; x++) {
        for (int y = 0; y < gridSize_; y++) {
            for (int z = 0; z < gridSize_; z++) {
                voxels_[x][y][z].portalTriangleCount = perVoxelTriangles[x][y][z].size();
            }
        }
    }

    SimpleBVH bvh(portalVertices_, portalIndices_);

    // ★修正: collapse(3)を削除し、1次元ループに変換
    int totalVoxelCount = gridSize_ * gridSize_ * gridSize_;
#pragma omp parallel for
    for (int64_t idx = 0; idx < (int64_t)totalVoxelCount; idx++) {
        int x = idx / (gridSize_ * gridSize_);
        int y = (idx / gridSize_) % gridSize_;
        int z = idx % gridSize_;

        if (voxels_[x][y][z].portalTriangleCount > 0) continue;
        glm::vec3 voxelCenter = voxels_[x][y][z].center;
        glm::vec3 rayDir(1.0f, 0.0f, 0.0f);
        int intersectionCount = bvh.countIntersections(voxelCenter, rayDir);
        if ((intersectionCount % 2) == 1) {
            voxels_[x][y][z].portalTriangleCount = 1;
        }
    }
}


// =============================================================================
// calculateHybrid() 関数 - 完全版（MSVC対応）
// =============================================================================
void CentVoxTetrahedralizerHybrid::calculateHybrid() {
    int totalVoxels = gridSize_ * gridSize_ * gridSize_;
    size_t totalTriangles = portalIndices_.size() / 3;

    std::vector<std::vector<std::vector<std::vector<size_t>>>> perVoxelTriangles(gridSize_);
    std::vector<std::vector<std::vector<int>>> perVoxelCounts(gridSize_);

    for (int x = 0; x < gridSize_; x++) {
        perVoxelTriangles[x].resize(gridSize_);
        perVoxelCounts[x].resize(gridSize_);
        for (int y = 0; y < gridSize_; y++) {
            perVoxelTriangles[x][y].resize(gridSize_);
            perVoxelCounts[x][y].resize(gridSize_, 0);
        }
    }

#pragma omp parallel
    {
        std::vector<std::vector<std::vector<std::vector<size_t>>>> localTriangles(gridSize_);
        for (int x = 0; x < gridSize_; x++) {
            localTriangles[x].resize(gridSize_);
            for (int y = 0; y < gridSize_; y++) {
                localTriangles[x][y].resize(gridSize_);
            }
        }

#pragma omp for schedule(dynamic, 100) nowait
        for (int64_t triIdx = 0; triIdx < (int64_t)totalTriangles; triIdx++) {
            size_t i = triIdx * 3;
            GLuint idx0 = portalIndices_[i];
            GLuint idx1 = portalIndices_[i + 1];
            GLuint idx2 = portalIndices_[i + 2];

            glm::vec3 v0(portalVertices_[idx0 * 3], portalVertices_[idx0 * 3 + 1], portalVertices_[idx0 * 3 + 2]);
            glm::vec3 v1(portalVertices_[idx1 * 3], portalVertices_[idx1 * 3 + 1], portalVertices_[idx1 * 3 + 2]);
            glm::vec3 v2(portalVertices_[idx2 * 3], portalVertices_[idx2 * 3 + 1], portalVertices_[idx2 * 3 + 2]);

            glm::vec3 triMin = glm::min(glm::min(v0, v1), v2);
            glm::vec3 triMax = glm::max(glm::max(v0, v1), v2);

            int xMin = std::max(0, (int)std::floor((triMin.x - gridMin_.x) / voxelSize_.x));
            int xMax = std::min(gridSize_ - 1, (int)std::floor((triMax.x - gridMin_.x) / voxelSize_.x));
            int yMin = std::max(0, (int)std::floor((triMin.y - gridMin_.y) / voxelSize_.y));
            int yMax = std::min(gridSize_ - 1, (int)std::floor((triMax.y - gridMin_.y) / voxelSize_.y));
            int zMin = std::max(0, (int)std::floor((triMin.z - gridMin_.z) / voxelSize_.z));
            int zMax = std::min(gridSize_ - 1, (int)std::floor((triMax.z - gridMin_.z) / voxelSize_.z));

            for (int x = xMin; x <= xMax; x++) {
                for (int y = yMin; y <= yMax; y++) {
                    for (int z = zMin; z <= zMax; z++) {
                        if (triangleIntersectsVoxel(v0, v1, v2, voxels_[x][y][z])) {
                            localTriangles[x][y][z].push_back(triIdx);
                        }
                    }
                }
            }
        }

#pragma omp critical
        {
            for (int x = 0; x < gridSize_; x++) {
                for (int y = 0; y < gridSize_; y++) {
                    for (int z = 0; z < gridSize_; z++) {
                        if (!localTriangles[x][y][z].empty()) {
                            perVoxelTriangles[x][y][z].insert(
                                perVoxelTriangles[x][y][z].end(),
                                localTriangles[x][y][z].begin(),
                                localTriangles[x][y][z].end()
                                );
                            perVoxelCounts[x][y][z] += localTriangles[x][y][z].size();
                        }
                    }
                }
            }
        }
    }

    for (int x = 0; x < gridSize_; x++) {
        for (int y = 0; y < gridSize_; y++) {
            for (int z = 0; z < gridSize_; z++) {
                voxels_[x][y][z].portalTriangleCount = perVoxelCounts[x][y][z];
            }
        }
    }

    SimpleBVH bvh(portalVertices_, portalIndices_);

    // ★修正: collapse(3)を削除し、1次元ループに変換
    int totalVoxelCount = gridSize_ * gridSize_ * gridSize_;
#pragma omp parallel for
    for (int64_t idx = 0; idx < (int64_t)totalVoxelCount; idx++) {
        int x = idx / (gridSize_ * gridSize_);
        int y = (idx / gridSize_) % gridSize_;
        int z = idx % gridSize_;

        if (voxels_[x][y][z].portalTriangleCount > 0) continue;
        glm::vec3 voxelCenter = voxels_[x][y][z].center;
        glm::vec3 rayDir(1.0f, 0.0f, 0.0f);
        int intersectionCount = bvh.countIntersections(voxelCenter, rayDir);
        if ((intersectionCount % 2) == 1) {
            voxels_[x][y][z].portalTriangleCount = 1;
        }
    }
}


// =============================================================================
// generateTetrahedra() 関数 - 完全版（MSVC対応）
// =============================================================================
void CentVoxTetrahedralizerHybrid::generateTetrahedra() {
    std::vector<std::tuple<int, int, int>> voxelList;
    voxelList.reserve(gridSize_ * gridSize_ * gridSize_ / 8);

    for (int z = 0; z < gridSize_; z++) {
        for (int y = 0; y < gridSize_; y++) {
            for (int x = 0; x < gridSize_; x++) {
                if (!voxels_[x][y][z].isRemoved) {
                    voxelList.push_back({x, y, z});
                }
            }
        }
    }

    int cubeCount = voxelList.size();
    std::unordered_map<size_t, int> gridVertexMap;
    std::vector<glm::vec3> vertices;
    vertices.reserve(cubeCount * 8);

    auto gridHash = [this](int x, int y, int z) -> size_t {
        return ((size_t)z * (gridSize_+1) + y) * (gridSize_+1) + x;
    };

    for (const auto& [vx, vy, vz] : voxelList) {
        for (int dz = 0; dz <= 1; dz++) {
            for (int dy = 0; dy <= 1; dy++) {
                for (int dx = 0; dx <= 1; dx++) {
                    int gx = vx + dx;
                    int gy = vy + dy;
                    int gz = vz + dz;
                    size_t hash = gridHash(gx, gy, gz);

                    if (gridVertexMap.find(hash) == gridVertexMap.end()) {
                        glm::vec3 pos = gridMin_ + glm::vec3(gx * voxelSize_.x, gy * voxelSize_.y, gz * voxelSize_.z);
                        gridVertexMap[hash] = vertices.size();
                        vertices.push_back(pos);
                    }
                }
            }
        }
    }

    std::set<int> deepInteriorVertexSet;
    for (const auto& [vx, vy, vz] : voxelList) {
        if (!voxels_[vx][vy][vz].isDeepInterior) continue;

        for (int dz = 0; dz <= 1; dz++) {
            for (int dy = 0; dy <= 1; dy++) {
                for (int dx = 0; dx <= 1; dx++) {
                    int gx = vx + dx;
                    int gy = vy + dy;
                    int gz = vz + dz;
                    size_t hash = gridHash(gx, gy, gz);
                    deepInteriorVertexSet.insert(gridVertexMap[hash]);
                }
            }
        }
    }

    auto getBlockCoord = [this](int coord) -> int {
        return coord / mergeBlockSize_;
    };

    std::map<std::tuple<int, int, int>, std::vector<int>> blockVertexGroups;
    for (int vid : deepInteriorVertexSet) {
        glm::vec3 pos = vertices[vid];
        glm::vec3 gridCoord = (pos - gridMin_) / voxelSize_;
        int gx = static_cast<int>(std::round(gridCoord.x));
        int gy = static_cast<int>(std::round(gridCoord.y));
        int gz = static_cast<int>(std::round(gridCoord.z));
        int bx = getBlockCoord(gx);
        int by = getBlockCoord(gy);
        int bz = getBlockCoord(gz);
        blockVertexGroups[{bx, by, bz}].push_back(vid);
    }

    std::map<int, int> vertexMergeMap;
    int totalMerged = 0;

    for (const auto& [blockCoord, vertexGroup] : blockVertexGroups) {
        if (vertexGroup.size() <= 1) continue;

        glm::vec3 centroid(0.0f);
        for (int vid : vertexGroup) {
            centroid += vertices[vid];
        }
        centroid /= static_cast<float>(vertexGroup.size());

        int representativeId = vertexGroup[0];
        float minDist = glm::length(vertices[vertexGroup[0]] - centroid);
        for (int vid : vertexGroup) {
            float dist = glm::length(vertices[vid] - centroid);
            if (dist < minDist) {
                minDist = dist;
                representativeId = vid;
            }
        }

        for (int vid : vertexGroup) {
            if (vid != representativeId) {
                vertexMergeMap[vid] = representativeId;
                totalMerged++;
            }
        }
    }

    std::set<int> usedVertices;
    for (size_t i = 0; i < vertices.size(); i++) {
        if (vertexMergeMap.find(i) != vertexMergeMap.end()) {
            usedVertices.insert(vertexMergeMap[i]);
        } else {
            usedVertices.insert(i);
        }
    }

    std::map<int, int> vertexRemap;
    std::vector<glm::vec3> compactedVertices;
    for (int oldId : usedVertices) {
        vertexRemap[oldId] = compactedVertices.size();
        compactedVertices.push_back(vertices[oldId]);
    }

    std::vector<std::vector<int>> tetrahedra;
    tetrahedra.reserve(cubeCount * 5);

    for (const auto& [x, y, z] : voxelList) {
        int v[8];
        v[0] = gridVertexMap[gridHash(x, y, z)];
        v[1] = gridVertexMap[gridHash(x+1, y, z)];
        v[2] = gridVertexMap[gridHash(x+1, y+1, z)];
        v[3] = gridVertexMap[gridHash(x, y+1, z)];
        v[4] = gridVertexMap[gridHash(x, y, z+1)];
        v[5] = gridVertexMap[gridHash(x+1, y, z+1)];
        v[6] = gridVertexMap[gridHash(x+1, y+1, z+1)];
        v[7] = gridVertexMap[gridHash(x, y+1, z+1)];

        for (int i = 0; i < 8; i++) {
            if (vertexMergeMap.find(v[i]) != vertexMergeMap.end()) {
                v[i] = vertexMergeMap[v[i]];
            }
            v[i] = vertexRemap[v[i]];
        }

        bool parity = ((x + y + z) % 2) == 0;
        std::vector<std::vector<int>> tets;

        if (parity) {
            tets = {
                {v[0], v[1], v[3], v[4]},
                {v[1], v[2], v[3], v[6]},
                {v[1], v[3], v[4], v[6]},
                {v[1], v[4], v[5], v[6]},
                {v[3], v[4], v[6], v[7]}
            };
        } else {
            tets = {
                {v[0], v[1], v[2], v[5]},
                {v[0], v[2], v[3], v[7]},
                {v[0], v[4], v[5], v[7]},
                {v[2], v[5], v[6], v[7]},
                {v[0], v[2], v[5], v[7]}
            };
        }

        for (auto& tet : tets) {
            std::set<int> uniqueVerts(tet.begin(), tet.end());
            if (uniqueVerts.size() == 4) {
                tetrahedra.push_back(tet);
            }
        }
    }

#pragma omp parallel for
    for (int64_t i = 0; i < (int64_t)tetrahedra.size(); i++) {
        auto& tet = tetrahedra[i];
        glm::vec3 v0 = compactedVertices[tet[0]];
        glm::vec3 v1 = compactedVertices[tet[1]];
        glm::vec3 v2 = compactedVertices[tet[2]];
        glm::vec3 v3 = compactedVertices[tet[3]];

        glm::vec3 e1 = v1 - v0;
        glm::vec3 e2 = v2 - v0;
        glm::vec3 e3 = v3 - v0;

        float det = glm::dot(e1, glm::cross(e2, e3));

        if (det < 0) {
            std::swap(tet[1], tet[2]);
        }
    }

    std::unordered_map<std::string, int> faceCount;
    std::unordered_map<std::string, int> faceToTetMap;

    auto faceKey = [](int a, int b, int c) -> std::string {
        std::vector<int> v = {a, b, c};
        std::sort(v.begin(), v.end());
        return std::to_string(v[0]) + "_" + std::to_string(v[1]) + "_" + std::to_string(v[2]);
    };

    for (size_t tetIdx = 0; tetIdx < tetrahedra.size(); tetIdx++) {
        const auto& tet = tetrahedra[tetIdx];

        std::vector<std::string> faces = {
            faceKey(tet[0], tet[1], tet[2]),
            faceKey(tet[0], tet[1], tet[3]),
            faceKey(tet[0], tet[2], tet[3]),
            faceKey(tet[1], tet[2], tet[3])
        };

        for (const auto& face : faces) {
            faceCount[face]++;
            if (faceToTetMap.find(face) == faceToTetMap.end()) {
                faceToTetMap[face] = tetIdx;
            }
        }
    }

    std::vector<std::vector<int>> surfaceTriangles;

    for (const auto& [faceStr, count] : faceCount) {
        if (count == 1) {
            std::vector<int> verts;
            std::stringstream ss(faceStr);
            std::string token;
            while (std::getline(ss, token, '_')) {
                verts.push_back(std::stoi(token));
            }

            int tetIdx = faceToTetMap[faceStr];

            glm::vec3 tetCenter(0.0f);
            for (int vidx : tetrahedra[tetIdx]) {
                tetCenter += compactedVertices[vidx];
            }
            tetCenter /= 4.0f;

            glm::vec3 faceCenter = (compactedVertices[verts[0]] + compactedVertices[verts[1]] + compactedVertices[verts[2]]) / 3.0f;

            glm::vec3 edge1 = compactedVertices[verts[1]] - compactedVertices[verts[0]];
            glm::vec3 edge2 = compactedVertices[verts[2]] - compactedVertices[verts[0]];
            glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

            glm::vec3 outward = faceCenter - tetCenter;

            if (glm::dot(normal, outward) < 0.0f) {
                std::swap(verts[1], verts[2]);
            }

            surfaceTriangles.push_back(verts);
        }
    }

    buildMeshData(compactedVertices, tetrahedra, surfaceTriangles);
}
