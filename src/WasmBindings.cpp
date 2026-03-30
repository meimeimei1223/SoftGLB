#ifdef __EMSCRIPTEN__
// WasmBindings.cpp
// Emscripten バインディング定義
// JavaScript から SoftBodyPhysics クラスにアクセスするためのインターフェース

#include "SoftBodyPhysics.h"
#include "SphereColliderPhysics.h"
#include "GlbLoader.h"
#include "MeshNormalizer.h"
#include "CentVoxTetrahedralizerHybrid.h"
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <sstream>
#include <fstream>
#include <set>

using namespace emscripten;

//=============================================================================
// ★ バグ修正: テクスチャデータをJSに渡すためのグローバルキャッシュ
//    createSoftBodyFromGlbBytes() 後に getLastTextureData() で取得する
//=============================================================================
static std::vector<unsigned char> g_lastTexturePixels;
static int g_lastTextureWidth   = 0;
static int g_lastTextureHeight  = 0;
static int g_lastTextureChannels= 0;
static bool g_lastHasTexture    = false;

val jsGetLastTexturePixels() {
    if (!g_lastHasTexture || g_lastTexturePixels.empty())
        return val::null();
    return val(typed_memory_view(g_lastTexturePixels.size(), g_lastTexturePixels.data()));
}

//=============================================================================
// ヘルパー関数: std::vector<float> を JavaScript Float32Array に変換
//=============================================================================
val getFloat32Array(const std::vector<float>& vec) {
    return val(typed_memory_view(vec.size(), vec.data()));
}

//=============================================================================
// ヘルパー関数: std::vector<int> を JavaScript Int32Array に変換
//=============================================================================
val getInt32Array(const std::vector<int>& vec) {
    return val(typed_memory_view(vec.size(), vec.data()));
}

//=============================================================================
// SoftBodyPhysics 用のラッパー関数
//=============================================================================

// 位置データを取得
val jsGetPositions(SoftBodyPhysics& self) {
    return getFloat32Array(self.getPositions());
}

// 表示用位置データを取得
val jsGetVisPositions(SoftBodyPhysics& self) {
    return getFloat32Array(self.getVisPositions());
}

// 表示用法線データを取得
val jsGetVisNormals(SoftBodyPhysics& self) {
    return getFloat32Array(self.getVisNormals());
}

// 表示用インデックスを取得
val jsGetVisIndices(SoftBodyPhysics& self) {
    return getInt32Array(self.getVisSurfaceTriIds());
}

// ★ 表示用UVを取得
val jsGetVisUVs(SoftBodyPhysics& self) {
    return getFloat32Array(self.getVisUVs());
}

// 四面体エッジ頂点を取得
val jsGetTetEdgeVertices(SoftBodyPhysics& self) {
    return getFloat32Array(self.getTetEdgeVertices());
}

// 四面体インデックスを取得
val jsGetTetIds(SoftBodyPhysics& self) {
    return getInt32Array(self.getTetIds());
}

//=============================================================================
// ファクトリ関数: メッシュデータから SoftBodyPhysics を作成
//=============================================================================
SoftBodyPhysics* createSoftBodyFromData(
    const std::string& tetMeshData,
    const std::string& visMeshData,
    float edgeCompliance,
    float volCompliance
) {
    SoftBodyPhysics::MeshData tetMesh = SoftBodyPhysics::loadTetMeshFromString(tetMeshData);
    SoftBodyPhysics::MeshData visMesh = SoftBodyPhysics::ReadVertexAndFaceFromString(visMeshData);
    
    return new SoftBodyPhysics(tetMesh, visMesh, edgeCompliance, volCompliance);
}

//=============================================================================
// createSoftBodyFromGlbBytes - C++物理エンジンの完全実装を直接呼び出し
//=============================================================================
SoftBodyPhysics* createSoftBodyFromGlbBytes(
    const val& glbUint8Array,   // JS の Uint8Array を直接受け取る
    float targetSize,
    int gridSize,
    float edgeCompliance,
    float volCompliance
) {
    // -----------------------------------------------------------------------
    // (A) JS の Uint8Array → C++ vector<uint8_t> → /tmp に書く
    //     val 経由で受け取ることで UTF-8 変換を完全に回避する
    // -----------------------------------------------------------------------
    const size_t byteLen = glbUint8Array["byteLength"].as<size_t>();
    std::vector<unsigned char> rawBuf(byteLen);
    
    // Uint8Array から直接バイトをコピー（UTF-8変換なし）
    for (size_t i = 0; i < byteLen; ++i) {
        rawBuf[i] = glbUint8Array[i].as<unsigned char>();
    }

    std::cout << "[createSoftBodyFromGlbBytes] Received " << byteLen 
              << " bytes via Uint8Array" << std::endl;
    std::cout << "[createSoftBodyFromGlbBytes] First 4 bytes (hex): " 
              << std::hex << (unsigned)rawBuf[0] << " " << (unsigned)rawBuf[1] << " "
              << (unsigned)rawBuf[2] << " " << (unsigned)rawBuf[3] << std::dec << std::endl;
    
    // /tmp に書く（FILESYSTEM=1 が必要）
    const std::string tmpGlbPath = "/tmp/wasm_input.glb";
    std::ofstream glbFile(tmpGlbPath, std::ios::binary);
    glbFile.write(reinterpret_cast<const char*>(rawBuf.data()), rawBuf.size());
    glbFile.close();

    std::cout << "[createSoftBodyFromGlbBytes] Written " << byteLen 
              << " bytes to " << tmpGlbPath << std::endl;

    // 既存のGlbLoader::loadを使用
    GlbResult glb = GlbLoader::load(tmpGlbPath);
    std::remove(tmpGlbPath.c_str());
    if (glb.meshData.verts.empty()) {
        std::cerr << "[createSoftBodyFromGlbBytes] GLB parse failed" << std::endl;
        return nullptr;
    }
    
    // C++のMeshNormalizerで正規化（main.cpp L515-517と同じ処理）
    NormParams normParams = MeshNormalizer::computeParams(glb.meshData.verts, targetSize);
    MeshNormalizer::applyToMeshData(glb.meshData, normParams);
    
    // ★ 座標系一致：visメッシュとtetメッシュの座標系を揃える（main.cpp L517相当）
    // centerToOrigin関数が無い場合の手動実装
    {
        // visメッシュの中心を計算
        glm::vec3 center(0.0f);
        size_t vertCount = glb.meshData.verts.size() / 3;
        for (size_t i = 0; i < vertCount; ++i) {
            center.x += glb.meshData.verts[i * 3 + 0];
            center.y += glb.meshData.verts[i * 3 + 1];
            center.z += glb.meshData.verts[i * 3 + 2];
        }
        center /= float(vertCount);
        
        // 原点中心に移動
        for (size_t i = 0; i < vertCount; ++i) {
            glb.meshData.verts[i * 3 + 0] -= center.x;
            glb.meshData.verts[i * 3 + 1] -= center.y;  
            glb.meshData.verts[i * 3 + 2] -= center.z;
        }
        
        std::cout << "[createSoftBodyFromGlbBytes] Centered mesh: offset=(" 
                  << center.x << ", " << center.y << ", " << center.z << ")" << std::endl;
    }
    
    // 四面体化用にOBJファイルに出力
    const std::string tmpObjPath = "/tmp/wasm_input.obj";
    const std::string tmpTetPath = "/tmp/wasm_output.txt";
    
    bool objSaved = MeshNormalizer::saveAsObj(glb.meshData, tmpObjPath);
    if (!objSaved) {
        std::cerr << "[createSoftBodyFromGlbBytes] Failed to write OBJ" << std::endl;
        return nullptr;
    }
    
    // C++の四面体化エンジンを直接呼び出し
    CentVoxTetrahedralizerHybrid tetrahedralizer(
        gridSize, tmpObjPath, tmpTetPath,
        CentVoxTetrahedralizerHybrid::DetectionMode::HYBRID, 1, 1
    );
    
    CentVoxTetrahedralizerHybrid::SmoothingSettings ss;
    ss.enabled = true;
    ss.iterations = 0;
    ss.smoothFactor = 0.9f;
    ss.preserveVolume = true;
    ss.rescaleToOriginal = false;  // ★ main.cppと同じ：正規化済みスケール維持
    tetrahedralizer.setSmoothingSettings(ss);
    tetrahedralizer.execute();
    
    // C++から四面体データを直接取得
    const CentVoxTetrahedralizerHybrid::MeshData& centVoxMesh = tetrahedralizer.getMeshData();
    
    if (centVoxMesh.tetIds.empty()) {
        std::cerr << "[createSoftBodyFromGlbBytes] Tetrahedralization failed" << std::endl;
        std::remove(tmpObjPath.c_str());
        std::remove(tmpTetPath.c_str());
        return nullptr;
    }
    
    // C++の物理エンジン用データ構造に変換
    SoftBodyPhysics::MeshData tetMesh;
    if (centVoxMesh.isSmoothed && !centVoxMesh.smoothedVerts.empty()) {
        tetMesh.verts = centVoxMesh.smoothedVerts;
        tetMesh.tetSurfaceTriIds = centVoxMesh.smoothedSurfaceTriIds.empty() 
                                  ? centVoxMesh.tetSurfaceTriIds 
                                  : centVoxMesh.smoothedSurfaceTriIds;
    } else {
        tetMesh.verts = centVoxMesh.verts;
        tetMesh.tetSurfaceTriIds = centVoxMesh.tetSurfaceTriIds;
    }
    tetMesh.tetIds = centVoxMesh.tetIds;

    // ★ バグ修正: CentVoxTetrahedralizerHybrid::MeshData には tetEdgeIds が無い。
    //    tetIds（4頂点/四面体）から6辺×重複排除でエッジリストを生成する。
    //    tetEdgeIds が空のまま渡すと solveEdges() が一切動かず物理が潰れる。
    {
        // 四面体の6辺パターン (v0,v1,v2,v3) → (0-1,0-2,0-3,1-2,1-3,2-3)
        static const int edgePairs[6][2] = {{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}};
        std::set<std::pair<int,int>> edgeSet;
        const size_t numTetsLocal = centVoxMesh.tetIds.size() / 4;
        for (size_t i = 0; i < numTetsLocal; i++) {
            int v[4] = {
                centVoxMesh.tetIds[4*i+0],
                centVoxMesh.tetIds[4*i+1],
                centVoxMesh.tetIds[4*i+2],
                centVoxMesh.tetIds[4*i+3]
            };
            for (const auto& ep : edgePairs) {
                int a = v[ep[0]], b = v[ep[1]];
                if (a > b) std::swap(a, b);
                edgeSet.insert({a, b});
            }
        }
        tetMesh.tetEdgeIds.reserve(edgeSet.size() * 2);
        for (const auto& e : edgeSet) {
            tetMesh.tetEdgeIds.push_back(e.first);
            tetMesh.tetEdgeIds.push_back(e.second);
        }
        std::cout << "[createSoftBodyFromGlbBytes] Generated " 
                  << edgeSet.size() << " edges from " << numTetsLocal << " tets" << std::endl;
    }
    
    // C++の物理エンジンを直接呼び出してSoftBodyPhysicsオブジェクト作成
    SoftBodyPhysics::MeshData& visMesh = glb.meshData;
    
    // 一時ファイル削除
    std::remove(tmpObjPath.c_str());
    std::remove(tmpTetPath.c_str());
    
    // ★ テクスチャデータをグローバルキャッシュに保存（JSが後で取得）
    g_lastHasTexture     = glb.hasTexture;
    g_lastTextureWidth   = glb.textureWidth;
    g_lastTextureHeight  = glb.textureHeight;
    g_lastTextureChannels= glb.textureChannels;
    if (glb.hasTexture) {
        g_lastTexturePixels = glb.pixels;
        std::cout << "[createSoftBodyFromGlbBytes] Texture cached: "
                  << glb.textureWidth << "x" << glb.textureHeight
                  << " ch=" << glb.textureChannels << std::endl;
    } else {
        g_lastTexturePixels.clear();
    }

    std::cout << "[createSoftBodyFromGlbBytes] Success: " 
              << tetMesh.verts.size() / 3 << " particles, "
              << tetMesh.tetIds.size() / 4 << " tets" << std::endl;
    
    return new SoftBodyPhysics(tetMesh, visMesh, edgeCompliance, volCompliance);
}

//=============================================================================
// Emscripten バインディング定義
//=============================================================================
EMSCRIPTEN_BINDINGS(softbody_module) {
    
    // SoftBodyPhysics クラス
    class_<SoftBodyPhysics>("SoftBodyPhysics")
        
        // 物理シミュレーション
        .function("step", select_overload<void(float, int)>(&SoftBodyPhysics::step))
        .function("initPhysics", &SoftBodyPhysics::initPhysics)
        .function("updateAllMeshes", &SoftBodyPhysics::updateAllMeshes)
        
        // インタラクション (float版)
        .function("startGrab", select_overload<void(float, float, float)>(&SoftBodyPhysics::startGrab))
        .function("moveGrabbed", select_overload<void(float, float, float, float, float, float)>(&SoftBodyPhysics::moveGrabbed))
        .function("endGrab", select_overload<void(float, float, float, float, float, float)>(&SoftBodyPhysics::endGrab))
        
        // 形状復元
        .function("applyShapeRestoration", &SoftBodyPhysics::applyShapeRestoration)
        
        // データアクセサ (TypedArray として)
        .function("getPositions", &jsGetPositions)
        .function("getVisPositions", &jsGetVisPositions)
        .function("getVisNormals", &jsGetVisNormals)
        .function("getVisIndices", &jsGetVisIndices)
        .function("getVisUVs", &jsGetVisUVs)
        .function("hasUVs", &SoftBodyPhysics::hasUVs)
        .function("getTetEdgeVertices", &jsGetTetEdgeVertices)
        .function("getTetIds", &jsGetTetIds)
        
        // サイズ取得
        .function("getNumParticles", &SoftBodyPhysics::getNumParticles)
        .function("getNumTets", &SoftBodyPhysics::getNumTets)
        .function("getNumVisVerts", &SoftBodyPhysics::getNumVisVerts)
        .function("getVisPositionsSize", &SoftBodyPhysics::getVisPositionsSize)
        .function("getVisNormalsSize", &SoftBodyPhysics::getVisNormalsSize)
        .function("getVisSurfaceTriIdsSize", &SoftBodyPhysics::getVisSurfaceTriIdsSize)
        .function("getTetEdgeVerticesSize", &SoftBodyPhysics::getTetEdgeVerticesSize)
        
        // パラメータ設定
        .function("setEdgeCompliance", &SoftBodyPhysics::setEdgeCompliance)
        .function("getEdgeCompliance", &SoftBodyPhysics::getEdgeCompliance)
        .function("setVolCompliance", &SoftBodyPhysics::setVolCompliance)
        .function("getVolCompliance", &SoftBodyPhysics::getVolCompliance)
        .function("setDamping", &SoftBodyPhysics::setDamping)
        .function("getDamping", &SoftBodyPhysics::getDamping)
        .function("setGravity", &SoftBodyPhysics::setGravity)
        .function("setGroundY", &SoftBodyPhysics::setGroundY)
        .function("getGroundY", &SoftBodyPhysics::getGroundY)
        
        // 状態クエリ
        .function("isGrabbing", &SoftBodyPhysics::isGrabbing)
        .function("getGrabId", &SoftBodyPhysics::getGrabId)
        
        // 質量制御（下部固定用）
        .function("setInvMass", &SoftBodyPhysics::setInvMass)
        .function("getInvMass", &SoftBodyPhysics::getInvMass)
        
        // ワイヤーフレーム最適化はJavaScript側で制御（C++にフラグ追加は不要）
        ;
    
    // 静的ファクトリ関数
    function("createSoftBodyFromData", &createSoftBodyFromData, allow_raw_pointers());
    
    // ★ GLB直接ロード機能（Uint8Array → C++ 直接変換）
    function("createSoftBodyFromGlbBytes", optional_override([](
        const val& glbUint8Array,   // ← std::string から val に変更
        float targetSize,
        int gridSize,
        float edgeCompliance,
        float volCompliance
    ) -> SoftBodyPhysics* {
        return createSoftBodyFromGlbBytes(glbUint8Array, targetSize, gridSize, edgeCompliance, volCompliance);
    }), allow_raw_pointers());

    // ★ テクスチャデータ取得 API（createSoftBodyFromGlbBytes 後に呼ぶ）
    function("getLastTexturePixels",   &jsGetLastTexturePixels);
    function("getLastTextureWidth",    optional_override([]()->int{ return g_lastTextureWidth;   }));
    function("getLastTextureHeight",   optional_override([]()->int{ return g_lastTextureHeight;  }));
    function("getLastTextureChannels", optional_override([]()->int{ return g_lastTextureChannels;}));
    function("getLastHasTexture",      optional_override([]()->bool{ return g_lastHasTexture;    }));
    
    // 静的メッシュ読み込み関数
    function("loadTetMeshFromString", &SoftBodyPhysics::loadTetMeshFromString);
    function("readObjFromString", &SoftBodyPhysics::ReadVertexAndFaceFromString);

    // ========================================================================
    // SphereColliderPhysics クラス（球コライダー）
    // ========================================================================
    class_<SphereColliderPhysics>("SphereColliderPhysics")
        .constructor<>()

        // 初期化
        .function("initFromOBJString", &SphereColliderPhysics::initFromOBJString)

        // 毎フレーム更新
        .function("update", &SphereColliderPhysics::update)

        // ドラッグ操作（float×3 で渡す）
        .function("startDragAt", optional_override([](SphereColliderPhysics& s,
                                                      float x, float y, float z,
                                                      float grabDist){
            s.startDragAt(glm::vec3(x, y, z), grabDist);
        }))
        .function("moveDragTo", optional_override([](SphereColliderPhysics& s,
                                                     float x, float y, float z,
                                                     float dt){
            s.moveDragTo(glm::vec3(x, y, z), dt);
        }))
        .function("endDrag", &SphereColliderPhysics::endDrag)

        // 物理状態アクセサ（x/y/z 個別で返す）
        .function("getCenterX",     optional_override([](const SphereColliderPhysics& s){ return s.getCenter().x; }))
        .function("getCenterY",     optional_override([](const SphereColliderPhysics& s){ return s.getCenter().y; }))
        .function("getCenterZ",     optional_override([](const SphereColliderPhysics& s){ return s.getCenter().z; }))
        .function("getRadius",      &SphereColliderPhysics::getRadius)
        .function("getRadiusScale", &SphereColliderPhysics::getRadiusScale)
        .function("isDragging",     &SphereColliderPhysics::isDragging)

        // 設定
        .function("setCenterXYZ", optional_override([](SphereColliderPhysics& s,
                                                       float x, float y, float z){
            s.setCenter(glm::vec3(x, y, z));
        }))
        .function("setRadiusScale",    &SphereColliderPhysics::setRadiusScale)
        .function("changeRadiusScale", &SphereColliderPhysics::changeRadiusScale)

        // 表示フラグ
        .property("visible", &SphereColliderPhysics::visible)
        ;

    // SoftBodyPhysics への SphereColliderPhysics 登録
    function("addSphereCollider", optional_override([](SoftBodyPhysics& body,
                                                       SphereColliderPhysics& col){
        body.addSphereCollider(&col);
    }));
    function("clearSphereColliders", optional_override([](SoftBodyPhysics& body){
        body.clearSphereColliders();
    }));
}
#endif // __EMSCRIPTEN__