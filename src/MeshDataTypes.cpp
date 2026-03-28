#include "MeshDataTypes.h"

namespace MeshDataTypes {
    SimpleMeshData loadOBJFile(const char* filePath) {
        SimpleMeshData meshData;
        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open file " << filePath << std::endl;
            return meshData;
        }

        std::vector<glm::vec3> tempVertices;
        std::vector<std::vector<int>> faces;

        std::string line;
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string type;
            iss >> type;

            if (type == "v") {
                float x, y, z;
                iss >> x >> y >> z;
                tempVertices.push_back(glm::vec3(x, y, z));
            }
            else if (type == "f") {
                std::vector<int> face;
                std::string vertex;
                while (iss >> vertex) {
                    size_t pos = vertex.find('/');
                    if (pos != std::string::npos) {
                        vertex = vertex.substr(0, pos);
                    }
                    face.push_back(std::stoi(vertex) - 1);
                }
                if (face.size() >= 3) {
                    for (size_t i = 1; i < face.size() - 1; ++i) {
                        faces.push_back({face[0], face[i], face[i + 1]});
                    }
                }
            }
        }
        file.close();

        for (const auto& v : tempVertices) {
            meshData.vertices.push_back(v.x);
            meshData.vertices.push_back(v.y);
            meshData.vertices.push_back(v.z);
        }

        for (const auto& face : faces) {
            for (int idx : face) {
                meshData.indices.push_back(static_cast<unsigned int>(idx));
            }
        }

        return meshData;
    }
}
