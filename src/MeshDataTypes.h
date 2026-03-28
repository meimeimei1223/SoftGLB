#ifndef MESH_DATA_TYPES_H
#define MESH_DATA_TYPES_H

#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <glm/glm.hpp>

namespace MeshDataTypes {
    struct SimpleMeshData {
        std::vector<float> vertices;
        std::vector<unsigned int> indices;
    };

    // OBJ
    SimpleMeshData loadOBJFile(const char* filePath);
}

#endif // MESH_DATA_TYPES_H
