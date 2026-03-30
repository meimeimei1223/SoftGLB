#include <iostream>
#include <sstream>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <string>
#include "ShaderProgram.h"
#include "SoftBody.h"
#include "CentVoxTetrahedralizerHybrid.h"
#include "GlbLoader.h"
#include "MeshNormalizer.h"
#include "FullSphereCamera.h"
#include "SphereCollider.h"   // ★ 追加

// ========================================
// グローバル
// ========================================
int gWindowWidth  = 1024;
int gWindowHeight = 768;
GLFWwindow* gWindow = nullptr;
bool gWireframe = false;
bool restore    = false;

// model は常に単位行列（物体はワールド原点固定）
// カメラだけが動く → レイキャストとの一致が保証される
glm::mat4 model(1.0f), view(1.0f), projection(1.0f);

FullSphereCamera OrbitCam;

glm::vec3 hit_position;
bool isDragging = false;

// ★ 球コライダーグローバルポインタ
SphereCollider*   gSphereCollider = nullptr;
// ★ コリジョンモード切り替え用
SoftBodyPhysics*  gSoftBodyRef    = nullptr;

// ========================================
// RayCast
// ========================================
class RayCast {
public:
    struct Ray {
        glm::vec3 origin;
        glm::vec3 direction;
    };

    struct RayHit {
        bool hit;
        float distance;
        glm::vec3 position;
        SoftBody* hitObject;
    };

    static Ray screenToRay(float screenX, float screenY,
                           const glm::mat4& view,
                           const glm::mat4& projection,
                           const glm::vec4& viewport)
    {
        float ndcX = (2.0f * screenX) / viewport.z - 1.0f;
        float ndcY = 1.0f - (2.0f * screenY) / viewport.w;

        glm::vec4 nearPoint = glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        glm::vec4 farPoint  = glm::vec4(ndcX, ndcY,  1.0f, 1.0f);

        glm::mat4 invVP = glm::inverse(projection * view);
        glm::vec4 worldNear = invVP * nearPoint;
        glm::vec4 worldFar  = invVP * farPoint;
        worldNear /= worldNear.w;
        worldFar  /= worldFar.w;

        Ray ray;
        ray.origin    = glm::vec3(worldNear);
        ray.direction = glm::normalize(glm::vec3(worldFar - worldNear));
        return ray;
    }

    // ワールド空間でそのままインターセクト
    // SoftBody の物理座標はワールド原点基準なのでモデル変換不要
    static RayHit intersectMesh(const Ray& ray, SoftBody& mesh)
    {
        RayHit result = { false, std::numeric_limits<float>::max(), glm::vec3(0), nullptr };

        const auto& positions     = mesh.getPositions();
        const auto& surfaceTriIds = mesh.getMeshData().tetSurfaceTriIds;

        float t, u, v;
        for (size_t i = 0; i < surfaceTriIds.size(); i += 3) {
            int idx1 = surfaceTriIds[i];
            int idx2 = surfaceTriIds[i + 1];
            int idx3 = surfaceTriIds[i + 2];

            glm::vec3 v1(positions[idx1*3], positions[idx1*3+1], positions[idx1*3+2]);
            glm::vec3 v2(positions[idx2*3], positions[idx2*3+1], positions[idx2*3+2]);
            glm::vec3 v3(positions[idx3*3], positions[idx3*3+1], positions[idx3*3+2]);

            if (rayTriangleIntersect(ray.origin, ray.direction, v1, v2, v3, t, u, v)) {
                if (t < result.distance) {
                    result.hit       = true;
                    result.distance  = t;
                    result.position  = ray.origin + ray.direction * t;
                    result.hitObject = &mesh;
                }
            }
        }
        return result;
    }

private:
    static bool rayTriangleIntersect(
        const glm::vec3& rayOrigin, const glm::vec3& rayDir,
        const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
        float& t, float& u, float& v)
    {
        const float EPSILON = 1e-7f;
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 h = glm::cross(rayDir, edge2);
        float a = glm::dot(edge1, h);
        if (a > -EPSILON && a < EPSILON) return false;

        float f = 1.0f / a;
        glm::vec3 s = rayOrigin - v0;
        u = f * glm::dot(s, h);
        if (u < 0.0f || u > 1.0f) return false;

        glm::vec3 q = glm::cross(s, edge1);
        v = f * glm::dot(rayDir, q);
        if (v < 0.0f || u + v > 1.0f) return false;

        t = f * glm::dot(edge2, q);
        return t > EPSILON;
    }
};

// ========================================
// Grabber
// ========================================
class Grabber {
public:
    Grabber() : physicsObject(nullptr), grabDistance(0.0f),
        prevPosition(0.0f), velocity(0.0f), time(0.0f) {}

    void setPhysicsObject(SoftBody* obj) { physicsObject = obj; }

    void startGrab(float screenX, float screenY) {
        if (!physicsObject) return;

        // ワールド空間でレイを生成
        RayCast::Ray worldRay = RayCast::screenToRay(
            screenX, screenY, view, projection,
            glm::vec4(0, 0, gWindowWidth, gWindowHeight));

        // model = 単位行列なのでそのままワールド空間でインターセクト
        RayCast::RayHit hit = RayCast::intersectMesh(worldRay, *physicsObject);

        if (hit.hit) {
            hit_position = hit.position;  // ワールド空間の交点
            grabDistance = hit.distance;
            prevPosition = hit_position;
            velocity     = glm::vec3(0.0f);
            time         = 0.0f;
            physicsObject->startGrab(hit_position);
            isDragging = true;
        }
    }

    void moveGrab(float screenX, float screenY, float deltaTime) {
        if (!physicsObject || !isDragging) return;

        RayCast::Ray worldRay = RayCast::screenToRay(
            screenX, screenY, view, projection,
            glm::vec4(0, 0, gWindowWidth, gWindowHeight));

        glm::vec3 newPosition = worldRay.origin + worldRay.direction * grabDistance;

        if (time > 0.0f)
            velocity = (newPosition - prevPosition) / time;

        hit_position = newPosition;
        physicsObject->moveGrabbed(newPosition, velocity);
        prevPosition = newPosition;
        time = deltaTime;
    }

    void endGrab() {
        if (physicsObject && isDragging) {
            physicsObject->endGrab(hit_position, velocity);
            isDragging = false;
        }
    }

    void update(float deltaTime) { time += deltaTime; }

private:
    SoftBody* physicsObject;
    float     grabDistance;
    glm::vec3 prevPosition;
    glm::vec3 velocity;
    float     time;
};

Grabber* gGrabber = nullptr;

// ========================================
// Function prototypes
// ========================================
bool initOpenGL();
void glfw_onKey(GLFWwindow*, int, int, int, int);
void glfw_OnFramebufferSize(GLFWwindow*, int, int);
void glfw_onMouseMove(GLFWwindow*, double, double);
void glfw_onMouseScroll(GLFWwindow*, double, double);
void mouse_button_callback(GLFWwindow*, int, int, int);
void showFPS(GLFWwindow*);

// ========================================
// mCutMesh
// ========================================
struct mCutMesh {
    GLuint VAO, VBO, EBO, NBO;
    std::vector<GLfloat> mVertices;
    std::vector<GLfloat> mNormals;
    std::vector<GLuint>  mIndices;
    int numFaces;
    glm::vec3 mColor;

    void draw(ShaderProgram& shader, float alpha) {
        shader.use();
        shader.setUniform("model",      model);
        shader.setUniform("lightPos",   OrbitCam.cameraPos);
        shader.setUniform("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));
        shader.setUniform("view",       view);
        shader.setUniform("projection", projection);
        shader.setUniform("vertColor",  glm::vec4(mColor, alpha));
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, mIndices.size(), GL_UNSIGNED_INT, 0);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) std::cerr << "OpenGL error during drawing: " << err << std::endl;
        glBindVertexArray(0);
    }

    void cleanup() {
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glDeleteBuffers(1, &NBO);
        glDeleteBuffers(1, &EBO);
    }

    struct BVHNode {
        glm::vec3 min, max;
        std::vector<uint32_t> triangleIndices;
        BVHNode* left; BVHNode* right;
        BVHNode() : min(FLT_MAX), max(-FLT_MAX), left(nullptr), right(nullptr) {}
        ~BVHNode() { delete left; delete right; }
    };

    BVHNode* buildBVH(const mCutMesh& mesh, const std::vector<uint32_t>& tris, int depth = 0) {
        const int MAX_DEPTH = 10, MIN_TRI = 10;
        BVHNode* node = new BVHNode();
        for (uint32_t ti : tris) {
            for (int i = 0; i < 3; i++) {
                uint32_t vi = mesh.mIndices[ti*3+i];
                glm::vec3 p(mesh.mVertices[vi*3], mesh.mVertices[vi*3+1], mesh.mVertices[vi*3+2]);
                node->min = glm::min(node->min, p); node->max = glm::max(node->max, p);
            }
        }
        if (depth >= MAX_DEPTH || tris.size() <= MIN_TRI) { node->triangleIndices = tris; return node; }
        int axis = 0; float al = node->max.x - node->min.x;
        if (node->max.y - node->min.y > al) { axis = 1; al = node->max.y - node->min.y; }
        if (node->max.z - node->min.z > al)   axis = 2;
        float sp = (node->min[axis] + node->max[axis]) * 0.5f;
        std::vector<uint32_t> lt, rt;
        for (uint32_t ti : tris) {
            glm::vec3 c(0); for (int i=0;i<3;i++){uint32_t vi=mesh.mIndices[ti*3+i];c+=glm::vec3(mesh.mVertices[vi*3],mesh.mVertices[vi*3+1],mesh.mVertices[vi*3+2]);}
            c/=3.0f; if(c[axis]<sp) lt.push_back(ti); else rt.push_back(ti);
        }
        if (lt.empty() || rt.empty()) { node->triangleIndices = tris; return node; }
        node->left = buildBVH(mesh,lt,depth+1); node->right = buildBVH(mesh,rt,depth+1);
        return node;
    }

    bool rayBoxIntersect(const glm::vec3& o, const glm::vec3& d, const glm::vec3& mn, const glm::vec3& mx) {
        float tmin=-FLT_MAX, tmax=FLT_MAX;
        for(int i=0;i<3;i++){
            if(std::abs(d[i])<1e-6f){ if(o[i]<mn[i]||o[i]>mx[i]) return false; }
            else{ float inv=1/d[i],t1=(mn[i]-o[i])*inv,t2=(mx[i]-o[i])*inv; if(t1>t2)std::swap(t1,t2); tmin=std::max(tmin,t1); tmax=std::min(tmax,t2); if(tmin>tmax)return false; }
        }
        return true;
    }

    int rayMeshIntersect(const glm::vec3& o, const glm::vec3& d, const mCutMesh& mesh, BVHNode* node) {
        if(!rayBoxIntersect(o,d,node->min,node->max)) return 0;
        if(!node->left&&!node->right){
            int cnt=0;
            for(uint32_t ti:node->triangleIndices){
                uint32_t i0=mesh.mIndices[ti*3],i1=mesh.mIndices[ti*3+1],i2=mesh.mIndices[ti*3+2];
                glm::vec3 v0(mesh.mVertices[i0*3],mesh.mVertices[i0*3+1],mesh.mVertices[i0*3+2]);
                glm::vec3 v1(mesh.mVertices[i1*3],mesh.mVertices[i1*3+1],mesh.mVertices[i1*3+2]);
                glm::vec3 v2(mesh.mVertices[i2*3],mesh.mVertices[i2*3+1],mesh.mVertices[i2*3+2]);
                glm::vec3 e1=v1-v0,e2=v2-v0,h=glm::cross(d,e2); float a=glm::dot(e1,h);
                if(std::abs(a)<1e-6f) continue;
                float f=1/a; glm::vec3 s=o-v0; float u=f*glm::dot(s,h);
                if(u<0||u>1) continue;
                glm::vec3 q=glm::cross(s,e1); float v=f*glm::dot(d,q);
                if(v<0||u+v>1) continue;
                if(f*glm::dot(e2,q)>1e-6f) cnt++;
            }
            return cnt;
        }
        return (node->left?rayMeshIntersect(o,d,mesh,node->left):0)+(node->right?rayMeshIntersect(o,d,mesh,node->right):0);
    }

    bool isPointInside(const glm::vec3& p, const mCutMesh& mesh, BVHNode* bvh) {
        const glm::vec3 dirs[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        int cnt=0; for(int i=0;i<6;i++) if(rayMeshIntersect(p,dirs[i],mesh,bvh)%2==1) cnt++;
        return cnt>3;
    }

    void drawsegment(ShaderProgram& shader, mCutMesh& mesh, mCutMesh& ref, float alpha=1.0f) {
        glm::vec4 ic=glm::vec4(0.2f,0.8f,0.2f,alpha), oc=glm::vec4(0.8f,0.2f,0.2f,alpha);
        static GLuint lmid=0,lrid=0; static BVHNode* bvh=nullptr;
        static std::vector<GLuint> itri,otri;
        if(lmid!=mesh.VAO||lrid!=ref.VAO){
            delete bvh; const float SF=1.02f; mCutMesh sr=ref;
            glm::vec3 c(0); for(size_t i=0;i<ref.mVertices.size();i+=3){c.x+=ref.mVertices[i];c.y+=ref.mVertices[i+1];c.z+=ref.mVertices[i+2];}
            c/=(ref.mVertices.size()/3);
            for(size_t i=0;i<sr.mVertices.size();i+=3){glm::vec3 d(sr.mVertices[i]-c.x,sr.mVertices[i+1]-c.y,sr.mVertices[i+2]-c.z);sr.mVertices[i]=c.x+d.x*SF;sr.mVertices[i+1]=c.y+d.y*SF;sr.mVertices[i+2]=c.z+d.z*SF;}
            std::vector<uint32_t> at; for(uint32_t i=0;i<sr.mIndices.size()/3;i++) at.push_back(i);
            bvh=buildBVH(sr,at); itri.clear(); otri.clear();
            for(uint32_t i=0;i<mesh.mIndices.size()/3;i++){
                glm::vec3 cc(0); for(int j=0;j<3;j++){uint32_t vi=mesh.mIndices[i*3+j];cc+=glm::vec3(mesh.mVertices[vi*3],mesh.mVertices[vi*3+1],mesh.mVertices[vi*3+2]);}
                cc/=3.0f; if(isPointInside(cc,sr,bvh)) itri.push_back(i); else otri.push_back(i);
            }
            lmid=mesh.VAO; lrid=ref.VAO;
        }
        glBindVertexArray(mesh.VAO);
        auto ds=[&](const std::vector<GLuint>& t, const glm::vec4& col){
            if(t.empty()) return;
            std::vector<GLuint> idx; for(uint32_t ti:t){idx.push_back(mesh.mIndices[ti*3]);idx.push_back(mesh.mIndices[ti*3+1]);idx.push_back(mesh.mIndices[ti*3+2]);}
            GLuint e; glGenBuffers(1,&e); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,e);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,idx.size()*sizeof(GLuint),idx.data(),GL_STATIC_DRAW);
            shader.setUniform("vertColor",col); glDrawElements(GL_TRIANGLES,idx.size(),GL_UNSIGNED_INT,0); glDeleteBuffers(1,&e);
        };
        ds(itri,ic); ds(otri,oc); glBindVertexArray(0);
    }

    void drawsegment(ShaderProgram& shader, mCutMesh& mesh, mCutMesh& ref, const glm::vec3& camPos, float alpha=1.0f) {
        glm::vec4 ic=glm::vec4(0.2f,0.8f,0.2f,alpha), oc=glm::vec4(0.8f,0.2f,0.2f,alpha);
        static GLuint lmid=0,lrid=0; static BVHNode* bvh=nullptr;
        static std::vector<GLuint> itri,otri;
        if(lmid!=mesh.VAO||lrid!=ref.VAO){
            delete bvh; const float SF=1.02f; mCutMesh sr=ref;
            glm::vec3 c(0); for(size_t i=0;i<ref.mVertices.size();i+=3){c.x+=ref.mVertices[i];c.y+=ref.mVertices[i+1];c.z+=ref.mVertices[i+2];}
            c/=(ref.mVertices.size()/3);
            for(size_t i=0;i<sr.mVertices.size();i+=3){glm::vec3 d(sr.mVertices[i]-c.x,sr.mVertices[i+1]-c.y,sr.mVertices[i+2]-c.z);sr.mVertices[i]=c.x+d.x*SF;sr.mVertices[i+1]=c.y+d.y*SF;sr.mVertices[i+2]=c.z+d.z*SF;}
            std::vector<uint32_t> at; for(uint32_t i=0;i<sr.mIndices.size()/3;i++) at.push_back(i);
            bvh=buildBVH(sr,at); itri.clear(); otri.clear();
            for(uint32_t i=0;i<mesh.mIndices.size()/3;i++){
                glm::vec3 cc(0); for(int j=0;j<3;j++){uint32_t vi=mesh.mIndices[i*3+j];cc+=glm::vec3(mesh.mVertices[vi*3],mesh.mVertices[vi*3+1],mesh.mVertices[vi*3+2]);}
                cc/=3.0f; if(isPointInside(cc,sr,bvh)) itri.push_back(i); else otri.push_back(i);
            }
            lmid=mesh.VAO; lrid=ref.VAO;
        }
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
        glBindVertexArray(mesh.VAO);
        struct TDI{bool in; uint32_t ti; float d;};
        std::vector<TDI> sv;
        auto add=[&](const std::vector<GLuint>& t,bool in){
            for(uint32_t ti:t){glm::vec3 c(0);for(int j=0;j<3;j++){uint32_t vi=mesh.mIndices[ti*3+j];c+=glm::vec3(mesh.mVertices[vi*3],mesh.mVertices[vi*3+1],mesh.mVertices[vi*3+2]);}c/=3.0f;sv.push_back({in,ti,glm::length(camPos-c)});}
        };
        add(itri,true); add(otri,false);
        std::sort(sv.begin(),sv.end(),[](const TDI&a,const TDI&b){return a.d>b.d;});
        GLuint e; glGenBuffers(1,&e); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,e);
        bool ci=false,ci2=false;
        for(const auto& t:sv){
            if(!ci2||t.in!=ci){shader.setUniform("vertColor",t.in?ic:oc);ci=t.in;ci2=true;}
            std::vector<GLuint> idx={mesh.mIndices[t.ti*3],mesh.mIndices[t.ti*3+1],mesh.mIndices[t.ti*3+2]};
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,idx.size()*sizeof(GLuint),idx.data(),GL_STATIC_DRAW);
            glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_INT,0);
        }
        glDeleteBuffers(1,&e); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glBindVertexArray(0);
    }

    mCutMesh loadMeshFromFile(const char* fp) {
        mCutMesh mesh; std::ifstream file(fp);
        if(!file.is_open()){std::cerr<<"Error: Could not open file "<<fp<<std::endl;return mesh;}
        std::vector<glm::vec3> verts; std::vector<std::vector<int>> faces;
        std::string line;
        while(std::getline(file,line)){
            std::istringstream iss(line); std::string t; iss>>t;
            if(t=="v"){float x,y,z;iss>>x>>y>>z;verts.push_back(glm::vec3(x,y,z));}
            else if(t=="f"){
                std::vector<int> face; std::string vstr;
                while(iss>>vstr){size_t p=vstr.find('/');if(p!=std::string::npos)vstr=vstr.substr(0,p);face.push_back(std::stoi(vstr)-1);}
                if(face.size()>=3) for(size_t i=1;i<face.size()-1;++i) faces.push_back({face[0],face[i],face[i+1]});
            }
        }
        file.close();
        mesh.mVertices.reserve(verts.size()*3);
        for(const auto& v:verts){mesh.mVertices.push_back(v.x);mesh.mVertices.push_back(v.y);mesh.mVertices.push_back(v.z);}
        mesh.mIndices.reserve(faces.size()*3);
        for(const auto& f:faces) for(int i:f) mesh.mIndices.push_back(static_cast<GLuint>(i));
        mesh.numFaces=faces.size(); mesh.mColor=glm::vec3(0.7f,0.7f,0.7f);
        return mesh;
    }
};

// ========================================
// draw_AllmCutMeshes / setUp
// ========================================
void draw_AllmCutMeshes(const std::vector<mCutMesh>& meshes, ShaderProgram& shader, const glm::vec3& camPos) {
    shader.use();
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    std::vector<glm::vec4> cols={{0.8f,0.2f,0.2f,1.0f},{0.9f,0.6f,0.6f,0.9f},{0.2f,0.8f,0.8f,0.9f},{0.8f,0.2f,0.8f,0.9f},{0.2f,0.8f,0.2f,0.8f},{0.8f,0.8f,0.2f,0.8f}};
    struct TI{size_t mi;uint32_t ti;float d;};
    std::vector<TI> all;
    for(size_t mi=0;mi<meshes.size();mi++){
        const auto& m=meshes[mi];
        for(size_t ti=0;ti<m.mIndices.size()/3;ti++){
            uint32_t i1=m.mIndices[ti*3],i2=m.mIndices[ti*3+1],i3=m.mIndices[ti*3+2];
            glm::vec3 v1(m.mVertices[i1*3],m.mVertices[i1*3+1],m.mVertices[i1*3+2]);
            glm::vec3 v2(m.mVertices[i2*3],m.mVertices[i2*3+1],m.mVertices[i2*3+2]);
            glm::vec3 v3(m.mVertices[i3*3],m.mVertices[i3*3+1],m.mVertices[i3*3+2]);
            all.push_back({mi,static_cast<uint32_t>(ti),glm::length(camPos-(v1+v2+v3)/3.0f)});
        }
    }
    std::sort(all.begin(),all.end(),[](const TI&a,const TI&b){return a.d>b.d;});
    GLuint lv=(GLuint)-1; glm::vec4 lc; bool ci=false;
    GLuint e; glGenBuffers(1,&e);
    for(const auto& t:all){
        const auto& m=meshes[t.mi];
        if(lv!=m.VAO){glBindVertexArray(m.VAO);lv=m.VAO;}
        glm::vec4 col=cols[t.mi%cols.size()];
        if(!ci||lc!=col){shader.setUniform("vertColor",col);lc=col;ci=true;}
        std::vector<GLuint> idx={m.mIndices[t.ti*3],m.mIndices[t.ti*3+1],m.mIndices[t.ti*3+2]};
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,e);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,idx.size()*sizeof(GLuint),idx.data(),GL_STATIC_DRAW);
        glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_INT,0);
    }
    glDeleteBuffers(1,&e); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glBindVertexArray(0);
}

void setUp(mCutMesh& src) {
    while(glGetError()!=GL_NO_ERROR){}
    if(src.mVertices.empty()||src.mIndices.empty()){std::cerr<<"Error: Empty mesh data"<<std::endl;return;}
    std::vector<float> verts(src.mVertices.size());
    std::vector<GLuint> idx(src.mIndices.size());
    for(size_t i=0;i<src.mVertices.size();i++) verts[i]=src.mVertices[i];
    for(size_t i=0;i<src.mIndices.size();i++) idx[i]=static_cast<GLuint>(src.mIndices[i]);
    size_t vc=verts.size()/3;
    for(size_t i=0;i<idx.size();i++) if(idx[i]>=vc){std::cerr<<"Error: Index out of range at "<<i<<": "<<idx[i]<<std::endl;return;}
    std::vector<float> n(verts.size(),0.0f);
    for(size_t i=0;i<idx.size();i+=3){
        GLuint i0=idx[i],i1=idx[i+1],i2=idx[i+2];
        glm::vec3 v0(verts[i0*3],verts[i0*3+1],verts[i0*3+2]);
        glm::vec3 v1(verts[i1*3],verts[i1*3+1],verts[i1*3+2]);
        glm::vec3 v2(verts[i2*3],verts[i2*3+1],verts[i2*3+2]);
        glm::vec3 nn=glm::normalize(glm::cross(v1-v0,v2-v0));
        for(GLuint ii:{i0,i1,i2}){n[ii*3]+=nn.x;n[ii*3+1]+=nn.y;n[ii*3+2]+=nn.z;}
    }
    for(size_t i=0;i<n.size();i+=3){
        glm::vec3 nn(n[i],n[i+1],n[i+2]); float l=glm::length(nn);
        nn=(l>0.0001f)?nn/l:glm::vec3(0,1,0); n[i]=nn.x;n[i+1]=nn.y;n[i+2]=nn.z;
    }
    glGenVertexArrays(1,&src.VAO); glGenBuffers(1,&src.VBO); glGenBuffers(1,&src.EBO); glGenBuffers(1,&src.NBO);
    glBindVertexArray(src.VAO);
    glBindBuffer(GL_ARRAY_BUFFER,src.VBO); glBufferData(GL_ARRAY_BUFFER,verts.size()*sizeof(GLfloat),verts.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,(void*)0); glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,src.NBO); glBufferData(GL_ARRAY_BUFFER,n.size()*sizeof(GLfloat),n.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,0,(void*)0); glEnableVertexAttribArray(1);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,src.EBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER,idx.size()*sizeof(GLuint),idx.data(),GL_STATIC_DRAW);
    glBindVertexArray(0);
}

mCutMesh* cutterMesh = nullptr;

// ========================================
// main
// ========================================
int main() {
    if (!initOpenGL()) {
        std::cerr << "GLFW initialization failed" << std::endl;
        return -1;
    }

    // ========================================
    // ★ カメラ設定
    // ========================================
    OrbitCam.initialRadius     = 6.0f;
    OrbitCam.gRadius           = 6.0f;
    OrbitCam.minRadius         = 1.0f;
    OrbitCam.maxRadius         = 20.0f;
    OrbitCam.MOUSE_SENSITIVITY = 0.005f;
    OrbitCam.ZOOM_SENSITIVITY  = -0.5f;
    OrbitCam.cameraTarget      = glm::vec3(0.0f, 0.0f, 0.0f);
    OrbitCam.setWindowSizePointers(&gWindowWidth, &gWindowHeight);
    OrbitCam.setGlobalMatrixPointers(&view, &projection, &model, nullptr);

    ShaderProgram shaderProgram;
    shaderProgram.loadShaders("../../../shader/basic.vert",
                              "../../../shader/basic.frag");

    // ========================================
    // ★ サイズ設定
    // ========================================
    const std::string glbPath        = "../../../model/liver.glb";
    const std::string resizedObjPath = "../../../model/liver_resized.obj";
    const std::string tetTxtPath     = "../../../model/liver_tetrahedral_mesh.txt";
    const float targetSize           = 2.0f;

    GlbResult glb = GlbLoader::load(glbPath);
    NormParams normParams = MeshNormalizer::computeParams(glb.meshData.verts, targetSize);
    MeshNormalizer::applyToMeshData(glb.meshData, normParams);
    MeshNormalizer::saveAsObj(glb.meshData, resizedObjPath);

    CentVoxTetrahedralizerHybrid tetrahedralizer2(
        20, resizedObjPath, tetTxtPath,
        CentVoxTetrahedralizerHybrid::DetectionMode::HYBRID, 1, 1);
    CentVoxTetrahedralizerHybrid::SmoothingSettings ss;
    ss.enabled=true; ss.iterations=0; ss.smoothFactor=0.9f; ss.preserveVolume=true; ss.rescaleToOriginal=true;
    tetrahedralizer2.setSmoothingSettings(ss);
    tetrahedralizer2.execute();

    SoftBody::MeshData tetmesh = SoftBody::loadTetMesh(tetTxtPath);
    SoftBody bunny(tetmesh, glb.meshData, 1.0f, 0.0f);
    if (glb.hasTexture)
        bunny.getRendering().loadTextureFromData(glb.pixels.data(), glb.textureWidth, glb.textureHeight, glb.textureChannels);

    // ========================================
    // ★ 下位 1/3 の頂点を固定（invMass = 0）
    // メッシュ全頂点のY座標を収集し、下から1/3の閾値以下を固定
    // ========================================
    {
        const auto& pos = bunny.getPhysics().getPositions();
        size_t numP = bunny.getPhysics().getNumParticles();

        // Y座標を収集してソート
        std::vector<float> ys(numP);
        for (size_t i = 0; i < numP; i++) ys[i] = pos[i*3+1];
        std::vector<float> sorted_ys = ys;
        std::sort(sorted_ys.begin(), sorted_ys.end());

        // 下位1/3の閾値
        float threshold = sorted_ys[numP / 3];

        // 閾値以下の頂点のinvMassを0に設定
        int fixedCount = 0;
        for (size_t i = 0; i < numP; i++) {
            if (ys[i] <= threshold) {
                bunny.getPhysics().setInvMass(i, 0.0f);
                fixedCount++;
            }
        }
        std::cout << "[main] Fixed " << fixedCount << " / " << numP
                  << " particles (bottom 1/3, Y <= " << threshold << ")" << std::endl;
    }

    float dt = 1.0f / 60.0f;
    glm::vec3 gravity(0.0f, 0.0f, 0.0f);

    // ========================================
    // ★ 球コライダー初期化
    // ========================================
    SphereCollider sphereCollider;
    const std::string sphereObjPath = "../../../model/ioSphere.obj";
    bool sphereLoaded = sphereCollider.initFromOBJ(sphereObjPath);
    if (sphereLoaded) {
        sphereCollider.setupGL();
        sphereCollider.setRadiusScale(0.2f);  // ★ ここでサイズ変更（0.5=半分、2.0=2倍）
        // 初期位置：ソフトボディの少し上（必要に応じて調整）
        sphereCollider.setCenter(glm::vec3(0.0f, 2.0f, 0.0f));
        bunny.getPhysics().addSphereCollider(&sphereCollider);
        std::cout << "[main] SphereCollider ready. Middle-click to drag." << std::endl;
    } else {
        std::cerr << "[main] Failed to load ioSphere.obj — collision disabled." << std::endl;
    }
    gSphereCollider = &sphereCollider;
    gSoftBodyRef    = &bunny.getPhysics();

    std::cout << "=== Keys ===" << std::endl;
    std::cout << "  1: Teleport         (強制テレポート・デバッグ用)" << std::endl;
    std::cout << "  2: Velocity         (速度クランプ)" << std::endl;
    std::cout << "  3: Vel+Slide        (Collide&Slide)" << std::endl;
    std::cout << "  4: Vel+Slide+Safe   (Collide&Slide + 内部停止)" << std::endl;
    std::cout << "  V: sphere表示  +/-: サイズ  R: カメラリセット" << std::endl;
    std::cout << "  ※コリジョン判定: レイキャスト内外判定 + CCD (固定)" << std::endl;
    std::cout << "============" << std::endl;

    // ========================================
    // ★ Grabber 初期化
    // ========================================
    Grabber grabber;
    gGrabber = &grabber;
    gGrabber->setPhysicsObject(&bunny);
    glfwSetMouseButtonCallback(gWindow, mouse_button_callback);

    // ---- Main loop ----
    while (!glfwWindowShouldClose(gWindow)) {
        showFPS(gWindow);
        glfwPollEvents();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        gGrabber->update(dt);

        // ★ 球コライダー速度更新（毎フレーム）
        sphereCollider.update(dt);

        // カメラ更新
        OrbitCam.UpdateCamera(dt);
        view       = OrbitCam.view;
        projection = OrbitCam.projection;

        model = glm::mat4(1.0f);
        bunny.setModelMatrix(model);

        shaderProgram.use();
        shaderProgram.setUniform("model",      model);
        shaderProgram.setUniform("lightPos",   OrbitCam.cameraPos);
        shaderProgram.setUniform("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));
        shaderProgram.setUniform("viewPos",    OrbitCam.cameraPos);
        shaderProgram.setUniform("view",       view);
        shaderProgram.setUniform("projection", projection);
        shaderProgram.setUniform("vertColor",  glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

        // ★ Small Steps（numSubsteps=10）
        int numSubsteps = 10;
        float stepDt = dt / float(numSubsteps);
        for (int i = 0; i < numSubsteps; i++) {
            bunny.preSolve(stepDt, gravity);
            bunny.solve(stepDt);
            bunny.postSolve(stepDt);
        }

        bunny.updateAllMeshes();
        bunny.drawVisMesh(shaderProgram);
        //bunny.drawTetMeshWireframe(shaderProgram);

        // ★ 球を描画
        if (sphereLoaded) {
            sphereCollider.draw(shaderProgram, view, projection, OrbitCam.cameraPos);
        }

        glfwSwapBuffers(gWindow);
    }

    glfwTerminate();
    return 0;
}

// ========================================
// OpenGL 初期化
// ========================================
bool initOpenGL() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    gWindow = glfwCreateWindow(gWindowWidth, gWindowHeight, "SoftBody Simulation", NULL, NULL);
    if (!gWindow) return false;
    glfwMakeContextCurrent(gWindow);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return false;
    glfwSetKeyCallback(gWindow, glfw_onKey);
    glfwSetFramebufferSizeCallback(gWindow, glfw_OnFramebufferSize);
    glfwSetCursorPosCallback(gWindow, glfw_onMouseMove);
    glfwSetScrollCallback(gWindow, glfw_onMouseScroll);
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glViewport(0, 0, gWindowWidth, gWindowHeight);
    glEnable(GL_DEPTH_TEST);
    return true;
}

// ========================================
// コールバック
// ========================================
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    // 左クリック：ソフトボディグラブ
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS)        gGrabber->startGrab(xpos, ypos);
        else if (action == GLFW_RELEASE) gGrabber->endGrab();
    }

    // ★ 中クリック：球ドラッグ
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && gSphereCollider) {
        if (action == GLFW_PRESS)
            gSphereCollider->startDrag(
                static_cast<float>(xpos), static_cast<float>(ypos),
                view, projection, gWindowWidth, gWindowHeight);
        else if (action == GLFW_RELEASE)
            gSphereCollider->endDrag();
    }
}

void glfw_onKey(GLFWwindow* window, int key, int scancode, int action, int mode) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(window, GL_TRUE);
    if (key == GLFW_KEY_F1    && action == GLFW_PRESS) { gWireframe=!gWireframe; glPolygonMode(GL_FRONT_AND_BACK,gWireframe?GL_LINE:GL_FILL); }
    if (key == GLFW_KEY_F2    && action == GLFW_PRESS) restore = true;
    if (key == GLFW_KEY_R     && action == GLFW_PRESS) OrbitCam.resetToInitialState();

    // ★ Vキー：球の表示切り替え
    if (key == GLFW_KEY_V && action == GLFW_PRESS && gSphereCollider)
        gSphereCollider->visible = !gSphereCollider->visible;
    // ★ +/-キー：球のサイズ変更
    if ((action == GLFW_PRESS || action == GLFW_REPEAT) && gSphereCollider) {
        if (key == GLFW_KEY_EQUAL) gSphereCollider->changeRadiusScale(+0.05f);
        if (key == GLFW_KEY_MINUS) gSphereCollider->changeRadiusScale(-0.05f);
    }

    // ★ Key1〜4：球の MoveMode 切り替え
    //   1: Teleport           （強制テレポート・デバッグ用）
    //   2: Velocity           （速度クランプ）
    //   3: Vel+Slide          （Collide&Slide）
    //   4: Vel+Slide+Safe     （Collide&Slide + 内部停止）
    if (action == GLFW_PRESS && gSphereCollider) {
        using MM = SphereCollider::MoveMode;
        bool changed = true;
        MM newMove = MM::TELEPORT;
        switch (key) {
        case GLFW_KEY_1: newMove = MM::TELEPORT;            break;
        case GLFW_KEY_2: newMove = MM::VELOCITY;            break;
        case GLFW_KEY_3: newMove = MM::VELOCITY_SLIDE;      break;
        case GLFW_KEY_4: newMove = MM::VELOCITY_SLIDE_SAFE; break;
        default: changed = false; break;
        }
        if (changed) {
            gSphereCollider->moveMode = newMove;
            std::cout << "[MoveMode] " << SphereCollider::getMoveName(newMove) << std::endl;
        }
    }
}

void glfw_OnFramebufferSize(GLFWwindow* window, int width, int height) {
    gWindowWidth=width; gWindowHeight=height;
    glViewport(0,0,width,height);
    OrbitCam.onWindowResize(width,height);
}

void glfw_onMouseMove(GLFWwindow* window, double posX, double posY) {
    static glm::vec2 last(0,0);
    float dx=(float)posX-last.x, dy=(float)posY-last.y;

    // ★ 中クリックドラッグ：球を移動（1〜4 移動モード）
    if (gSphereCollider && gSphereCollider->isDragging() &&
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
    {
        gSphereCollider->moveDrag(
            static_cast<float>(posX), static_cast<float>(posY),
            view, projection, gWindowWidth, gWindowHeight,
            1.0f / 60.0f, gSoftBodyRef);  // physics は Slide モード用
    }
    // 左クリックドラッグ：ソフトボディグラブ or カメラ回転
    else if (isDragging && glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS) {
        if (gGrabber) gGrabber->moveGrab(posX,posY,1.0f/60.0f);
    }
    else if (!isDragging && glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS) {
        OrbitCam.Rotate(dx,dy);
    }
    // 右クリック：カメラパン
    else if (glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS) {
        OrbitCam.Pan(dx,dy);
    }

    last.x=(float)posX; last.y=(float)posY;
}

void glfw_onMouseScroll(GLFWwindow* window, double deltaX, double deltaY) {
    OrbitCam.Zoom((float)deltaY);
}

void showFPS(GLFWwindow* window) {
    static double prev=0.0; static int fc=0;
    double cur=glfwGetTime(), el=cur-prev;
    if(el>0.25){
        prev=cur; double fps=fc/el, ms=1000.0/fps;
        std::ostringstream o; o.precision(3);
        const char* moveName = gSphereCollider
                                   ? SphereCollider::getMoveName(gSphereCollider->moveMode) : "-";
        o << std::fixed << "FPS:" << fps << "  " << ms << "ms"
          << "  Move:[" << moveName << "]"
          << "  1-4:mode  V:sph  +/-:size";
        glfwSetWindowTitle(window, o.str().c_str());
        std::cout << "[FPS] " << (int)fps << "  " << ms << "ms"
                  << "  move=" << moveName << std::endl;
        fc=0;
    }
    fc++;
}
