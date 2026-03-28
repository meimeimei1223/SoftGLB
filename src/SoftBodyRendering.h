#ifndef SOFTBODYRENDERING_H
#define SOFTBODYRENDERING_H

// GL 描画クラス — WASM ビルドでは除外
#ifndef __EMSCRIPTEN__

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class SoftBodyPhysics;
class ShaderProgram;

class SoftBodyRendering {
public:
    SoftBodyRendering();
    ~SoftBodyRendering();

    void bind(SoftBodyPhysics* softBody);
    void unbind();
    void update();
    void draw(ShaderProgram& shader);

    bool loadTextureFromData(const unsigned char* data, int width, int height, int channels);

private:
    void setupBuffers();
    void updateBuffers();
    void deleteBuffers();
    void computeNormals(const std::vector<float>& vertices,
                        const std::vector<int>&   indices,
                        std::vector<float>&       normals);

    const std::vector<float>& getVertices() const;
    const std::vector<int>&   getIndices()  const;

    SoftBodyPhysics* softBody_   = nullptr;

    GLuint vao_       = 0;
    GLuint vbo_       = 0;
    GLuint ebo_       = 0;
    GLuint normalVbo_ = 0;
    GLuint uvVbo_     = 0;
    GLuint textureID_ = 0;

    GLsizei indexCount_ = 0;
    std::vector<float> normals_;
    bool initialized_ = false;
};

#endif // __EMSCRIPTEN__
#endif // SOFTBODYRENDERING_H
