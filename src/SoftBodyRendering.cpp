// ネイティブビルド専用（WASM では除外）
#ifndef __EMSCRIPTEN__

#include "SoftBodyRendering.h"
#include "SoftBodyPhysics.h"
#include "ShaderProgram.h"
#include <iostream>
#include "stb_image.h"

SoftBodyRendering::SoftBodyRendering() {}

SoftBodyRendering::~SoftBodyRendering() {
    deleteBuffers();
    if (textureID_) { glDeleteTextures(1, &textureID_); textureID_ = 0; }
}

void SoftBodyRendering::bind(SoftBodyPhysics* softBody) {
    softBody_    = softBody;
    initialized_ = false;
}

void SoftBodyRendering::unbind() {
    deleteBuffers();
    if (textureID_) { glDeleteTextures(1, &textureID_); textureID_ = 0; }
    softBody_    = nullptr;
    initialized_ = false;
}

void SoftBodyRendering::update() {
    if (!softBody_) return;
    if (!initialized_) setupBuffers();
    else               updateBuffers();
}

bool SoftBodyRendering::loadTextureFromData(const unsigned char* data,
                                             int width, int height, int channels) {
    if (!data || width == 0 || height == 0) {
        std::cerr << "[SoftBodyRendering] Invalid texture data" << std::endl;
        return false;
    }
    if (textureID_) { glDeleteTextures(1, &textureID_); textureID_ = 0; }

    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;

    glGenTextures(1, &textureID_);
    glBindTexture(GL_TEXTURE_2D, textureID_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    std::cout << "[SoftBodyRendering] Texture loaded ("
              << width << "x" << height << " ch=" << channels << ")" << std::endl;
    return true;
}

void SoftBodyRendering::draw(ShaderProgram& shader) {
    if (!softBody_ || vao_ == 0) return;
    shader.use();

    if (textureID_) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID_);
        shader.setUniform("texture_map", 0);
        shader.setUniform("useTexture",  true);
    } else {
        shader.setUniform("useTexture", false);
    }

    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    if (textureID_) glBindTexture(GL_TEXTURE_2D, 0);
}

void SoftBodyRendering::setupBuffers() {
    if (!softBody_) return;
    deleteBuffers();

    const std::vector<float>& vertices = getVertices();
    const std::vector<int>&   indices  = getIndices();
    if (vertices.empty() || indices.empty()) return;

    indexCount_ = static_cast<GLsizei>(indices.size());
    computeNormals(vertices, indices, normals_);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glGenBuffers(1, &normalVbo_);
    glGenBuffers(1, &uvVbo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, normalVbo_);
    glBufferData(GL_ARRAY_BUFFER, normals_.size() * sizeof(float), normals_.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(1);

    {
        const std::vector<float>& uvs = softBody_->getVisUVs();
        glBindBuffer(GL_ARRAY_BUFFER, uvVbo_);
        if (!uvs.empty()) {
            glBufferData(GL_ARRAY_BUFFER, uvs.size() * sizeof(float), uvs.data(), GL_STATIC_DRAW);
        } else {
            std::vector<float> zeros(vertices.size() / 3 * 2, 0.0f);
            glBufferData(GL_ARRAY_BUFFER, zeros.size() * sizeof(float), zeros.data(), GL_STATIC_DRAW);
        }
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(2);
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(int), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
    initialized_ = true;
}

void SoftBodyRendering::updateBuffers() {
    if (!softBody_ || vao_ == 0) return;

    const std::vector<float>& vertices = getVertices();
    const std::vector<int>&   indices  = getIndices();
    if (vertices.empty()) return;

    computeNormals(vertices, indices, normals_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(float), vertices.data());

    glBindBuffer(GL_ARRAY_BUFFER, normalVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, normals_.size() * sizeof(float), normals_.data());
}

void SoftBodyRendering::deleteBuffers() {
    if (vao_)       { glDeleteVertexArrays(1, &vao_);   vao_       = 0; }
    if (vbo_)       { glDeleteBuffers(1, &vbo_);         vbo_       = 0; }
    if (ebo_)       { glDeleteBuffers(1, &ebo_);         ebo_       = 0; }
    if (normalVbo_) { glDeleteBuffers(1, &normalVbo_);   normalVbo_ = 0; }
    if (uvVbo_)     { glDeleteBuffers(1, &uvVbo_);       uvVbo_     = 0; }
    initialized_ = false;
}

const std::vector<float>& SoftBodyRendering::getVertices() const {
    return softBody_->getSmoothVertices();
}

const std::vector<int>& SoftBodyRendering::getIndices() const {
    return softBody_->getSmoothTriIds();
}

void SoftBodyRendering::computeNormals(const std::vector<float>& vertices,
                                       const std::vector<int>&   indices,
                                       std::vector<float>&       normals) {
    size_t numVerts = vertices.size() / 3;
    normals.assign(numVerts * 3, 0.0f);

    for (size_t i = 0; i < indices.size(); i += 3) {
        int i0 = indices[i], i1 = indices[i+1], i2 = indices[i+2];
        glm::vec3 v0(vertices[i0*3], vertices[i0*3+1], vertices[i0*3+2]);
        glm::vec3 v1(vertices[i1*3], vertices[i1*3+1], vertices[i1*3+2]);
        glm::vec3 v2(vertices[i2*3], vertices[i2*3+1], vertices[i2*3+2]);
        glm::vec3 fn = glm::cross(v1 - v0, v2 - v0);
        for (int idx : {i0, i1, i2}) {
            normals[idx*3]   += fn.x;
            normals[idx*3+1] += fn.y;
            normals[idx*3+2] += fn.z;
        }
    }

    for (size_t i = 0; i < numVerts; ++i) {
        glm::vec3 n(normals[i*3], normals[i*3+1], normals[i*3+2]);
        float len = glm::length(n);
        if (len > 0.0001f) n /= len;
        normals[i*3] = n.x; normals[i*3+1] = n.y; normals[i*3+2] = n.z;
    }
}

#endif // __EMSCRIPTEN__
