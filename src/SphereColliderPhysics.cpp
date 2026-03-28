#include "SphereColliderPhysics.h"

// ============================================================
// parseOBJ  （共通パース処理）
// ============================================================
bool SphereColliderPhysics::parseOBJ(std::istream& stream) {
    vertices_.clear();
    indices_.clear();

    std::vector<float> rawVerts;
    std::string line;

    while (std::getline(stream, line)) {
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
            std::vector<int> faceIdx;
            std::string vStr;
            while (ss >> vStr) {
                int vIdx = std::stoi(vStr.substr(0, vStr.find('/'))) - 1;
                faceIdx.push_back(vIdx);
            }
            for (size_t i = 1; i + 1 < faceIdx.size(); ++i) {
                indices_.push_back(static_cast<unsigned>(faceIdx[0]));
                indices_.push_back(static_cast<unsigned>(faceIdx[i]));
                indices_.push_back(static_cast<unsigned>(faceIdx[i + 1]));
            }
        }
    }

    if (rawVerts.empty()) {
        std::cerr << "[SphereColliderPhysics] No vertices found in OBJ." << std::endl;
        return false;
    }

    // AABB から中心・半径を計算
    glm::vec3 aabbMin( std::numeric_limits<float>::max());
    glm::vec3 aabbMax(-std::numeric_limits<float>::max());

    for (size_t i = 0; i < rawVerts.size(); i += 3) {
        glm::vec3 p(rawVerts[i], rawVerts[i+1], rawVerts[i+2]);
        aabbMin = glm::min(aabbMin, p);
        aabbMax = glm::max(aabbMax, p);
    }

    glm::vec3 objCenter = (aabbMin + aabbMax) * 0.5f;
    radiusBase_ = glm::length(aabbMax - aabbMin) * 0.5f;
    radius_     = radiusBase_ * radiusScale_;

    vertices_   = rawVerts;
    meshOffset_ = objCenter;
    prevCenter_ = center_;

    std::cout << "[SphereColliderPhysics] Loaded: " << rawVerts.size()/3 << " verts, "
              << indices_.size()/3 << " tris | "
              << "center=" << objCenter.x << "," << objCenter.y << "," << objCenter.z
              << " radius=" << radius_ << std::endl;
    return true;
}

// ============================================================
// initFromOBJ  （ファイルから読み込み）
// ============================================================
bool SphereColliderPhysics::initFromOBJ(const std::string& objPath) {
    std::ifstream file(objPath);
    if (!file.is_open()) {
        std::cerr << "[SphereColliderPhysics] Cannot open: " << objPath << std::endl;
        return false;
    }
    return parseOBJ(file);
}

// ============================================================
// initFromOBJString  （WASM 用：文字列から直接ロード）
// ============================================================
bool SphereColliderPhysics::initFromOBJString(const std::string& objData) {
    std::istringstream stream(objData);
    return parseOBJ(stream);
}

// ============================================================
// update  （毎フレーム：速度を差分で更新）
// ============================================================
void SphereColliderPhysics::update(float dt) {
    if (dt > 0.0f)
        velocity_ = (center_ - prevCenter_) / dt;
    prevCenter_ = center_;
}

// ============================================================
// startDragAt  （ワールド座標・距離は呼び元が計算済みで渡す）
// ============================================================
void SphereColliderPhysics::startDragAt(const glm::vec3& worldPos, float grabDistance) {
    grabDistance_ = grabDistance;
    dragPrev_     = worldPos;
    dragDt_       = 0.0f;
    dragging_     = true;
}

// ============================================================
// moveDragTo
// ============================================================
void SphereColliderPhysics::moveDragTo(const glm::vec3& worldPos, float dt) {
    if (!dragging_) return;

    if (dragDt_ > 0.0f)
        velocity_ = (worldPos - dragPrev_) / dragDt_;

    dragPrev_ = worldPos;
    dragDt_   = dt;
    center_   = worldPos;
}

// ============================================================
// endDrag
// ============================================================
void SphereColliderPhysics::endDrag() {
    dragging_ = false;
}
