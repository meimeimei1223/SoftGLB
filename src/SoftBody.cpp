// SoftBody.cpp
// ネイティブビルド専用（WASM では SoftBodyPhysics を直接使う）
#ifndef __EMSCRIPTEN__

#include "SoftBody.h"
#include "ShaderProgram.h"

SoftBody::SoftBody(const MeshData& tetMesh, const MeshData& visMesh,
                   float edgeCompliance, float volCompliance)
    : physics_(tetMesh, visMesh, edgeCompliance, volCompliance)
{
    rendering_.bind(&physics_);
    setupTetWireframe();
}

SoftBody::~SoftBody() {
    rendering_.unbind();
    deleteTetBuffers();
}

void SoftBody::preSolve(float dt, const glm::vec3& gravity) {
    physics_.preSolve(dt, gravity);
}

void SoftBody::solve(float dt) {
    physics_.solve(dt);
}

void SoftBody::postSolve(float dt) {
    physics_.postSolve(dt);
}

void SoftBody::initPhysics() {
    physics_.initPhysics();
}

void SoftBody::updateAllMeshes() {
    physics_.updateAllMeshes();
    rendering_.update();
    updateTetWireframe();
}

void SoftBody::drawVisMesh(ShaderProgram& shader) {
    rendering_.draw(shader);
}

void SoftBody::drawTetMeshWireframe(ShaderProgram& shader) {
    if (!showTetMesh_ || tetVAO_ == 0) return;
    shader.use();
    glBindVertexArray(tetVAO_);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDrawArrays(GL_LINES, 0, physics_.getTetEdgeVerticesSize() / 3);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBindVertexArray(0);
}

void SoftBody::startGrab(const glm::vec3& pos) {
    physics_.startGrab(pos);
}

void SoftBody::moveGrabbed(const glm::vec3& pos, const glm::vec3& vel) {
    physics_.moveGrabbed(pos, vel);
}

void SoftBody::endGrab(const glm::vec3& pos, const glm::vec3& vel) {
    physics_.endGrab(pos, vel);
}

void SoftBody::setupTetWireframe() {
    deleteTetBuffers();
    glGenVertexArrays(1, &tetVAO_);
    glGenBuffers(1, &tetVBO_);
    glBindVertexArray(tetVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, tetVBO_);
    const auto& verts = physics_.getTetEdgeVertices();
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void SoftBody::updateTetWireframe() {
    if (tetVBO_ == 0) return;
    const auto& verts = physics_.getTetEdgeVertices();
    glBindBuffer(GL_ARRAY_BUFFER, tetVBO_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
}

void SoftBody::deleteTetBuffers() {
    if (tetVAO_) { glDeleteVertexArrays(1, &tetVAO_); tetVAO_ = 0; }
    if (tetVBO_) { glDeleteBuffers(1, &tetVBO_); tetVBO_ = 0; }
}

#endif // __EMSCRIPTEN__
