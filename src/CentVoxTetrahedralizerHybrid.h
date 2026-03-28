#ifndef CENT_VOX_TETRAHEDRALIZER_HYBRID_H
#define CENT_VOX_TETRAHEDRALIZER_HYBRID_H

#include <vector>
#include <string>
#include <set>
#include <map>
#include <memory>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "MeshDataTypes.h"

class SimpleBVH;

class CentVoxTetrahedralizerHybrid {
public:
    enum class DetectionMode {
        INTERSECTION_ONLY,
        RAYCAST_ONLY,
        HYBRID
    };

    struct MeshData {
        std::vector<float> verts;
        std::vector<int> tetIds;
        std::vector<int> tetSurfaceTriIds;
        std::vector<float> smoothedVerts;
        std::vector<int> smoothedSurfaceTriIds;
        bool isSmoothed = false;
    };

    struct SmoothingSettings {
        bool enabled = false;
        int iterations = 3;
        float smoothFactor = 0.5f;
        bool preserveVolume = true;
        bool rescaleToOriginal = true;
    };

    CentVoxTetrahedralizerHybrid(
        int gridSize,
        const std::string& portalPath,
        const std::string& outputTetPath,
        DetectionMode mode = DetectionMode::HYBRID,
        int mergeBlockSize = 4,
        int protectionLayers = 3);

    ~CentVoxTetrahedralizerHybrid();

    const MeshData& getMeshData() const;

    void setSmoothingSettings(const SmoothingSettings& settings);

    void execute();

    MeshDataTypes::SimpleMeshData generateVoxelDisplayMesh() const;

private:
    struct Voxel {
        glm::vec3 min, max, center;
        int portalTriangleCount;
        bool isRemoved;
        bool isInterior;
        bool isDeepInterior;
        std::vector<size_t> triangleIndices;
    };

    struct BoundingBox {
        glm::vec3 min, max, size, center;
    };

    int gridSize_;
    int mergeBlockSize_;
    int protectionLayers_;
    std::string portalPath_;
    std::string outputTetPath_;
    DetectionMode detectionMode_;

    std::vector<GLfloat> cubeVertices_;
    std::vector<GLuint> cubeIndices_;
    std::vector<GLfloat> portalVertices_;
    std::vector<GLuint> portalIndices_;

    std::vector<std::vector<std::vector<Voxel>>> voxels_;
    glm::vec3 gridMin_, gridMax_, voxelSize_;

    std::unique_ptr<SimpleBVH> bvh_;
    MeshData meshData_;
    SmoothingSettings smoothingSettings_;
    BoundingBox originalPortalBBox_;

    std::string getModeString() const;
    void initializeCube();
    BoundingBox getBoundingBoxFromVertices(const std::vector<GLfloat>& vertices);
    BoundingBox getBoundingBoxFromFloatArray(const std::vector<float>& vertices);
    bool loadPortalMesh();
    void prepareCube();
    void initializeVoxelGrid();
    bool triangleIntersectsVoxel(const glm::vec3& v0, const glm::vec3& v1,
                                 const glm::vec3& v2, const Voxel& voxel);
    void calculateVoxelOccupancy();
    void calculateIntersectionOnly();
    void calculateRaycastOnly();
    void calculateHybrid();
    void carveExternalVoxels();
    void markInteriorVoxels();
    void generateTetrahedra();
    void applySurfaceSmoothing();
    void rescaleToOriginalSize();
    void buildMeshData(const std::vector<glm::vec3>& vertices,
                       const std::vector<std::vector<int>>& tetrahedra,
                       const std::vector<std::vector<int>>& surfaceTriangles);
    void writeOutputFile();
};

// SimpleBVH
class SimpleBVH {
private:
    struct BVHNode {
        glm::vec3 min;
        glm::vec3 max;
        int leftChild;
        int rightChild;
        std::vector<size_t> triangles;
    };

    std::vector<BVHNode> nodes_;
    const std::vector<GLfloat>& vertices_;
    const std::vector<GLuint>& indices_;
    static constexpr int MAX_TRIANGLES_PER_LEAF = 10;

public:
    SimpleBVH(const std::vector<GLfloat>& vertices, const std::vector<GLuint>& indices);

    void build();
    int countIntersections(const glm::vec3& origin, const glm::vec3& direction) const;

private:
    int buildRecursive(std::vector<size_t>& triangles, int depth);
    void traverseAndCount(int nodeIdx, const glm::vec3& origin,
                          const glm::vec3& direction, int& count) const;
    bool rayBoxIntersect(const glm::vec3& origin, const glm::vec3& direction,
                         const glm::vec3& boxMin, const glm::vec3& boxMax) const;
    bool rayTriangleIntersect(const glm::vec3& origin, const glm::vec3& direction,
                              size_t triIdx) const;
    glm::vec3 getVertex(size_t triIdx, int vertexIdx) const;
    glm::vec3 getTriangleCenter(size_t triIdx) const;
};

#endif // CENT_VOX_TETRAHEDRALIZER_HYBRID_H
