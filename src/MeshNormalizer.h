#ifndef MESH_NORMALIZER_H
#define MESH_NORMALIZER_H

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "SoftBodyPhysics.h"  // SoftBodyPhysics::MeshData

// GLB/OBJ メッシュを任意のサイズに正規化するユーティリティ。
//
// 使い方:
//   auto norm = MeshNormalizer::computeParams(verts, targetSize);
//   MeshNormalizer::applyToVerts(verts, norm);          // メモリ上で変換
//   MeshNormalizer::saveAsObj(verts, indices, outPath); // Tetrahedralizer 用 OBJ 出力
//
// center と scale だけ保持すれば GLB 書き出し不要。

struct NormParams {
    glm::vec3 center;   // AABB 中心 (移動量)
    float     scale;    // スケール係数
    float     targetSize; // 指定した目標サイズ (参考保持)
};

class MeshNormalizer {
public:
    // ---------------------------------------------------------
    // 1. 正規化パラメータを計算する
    //    verts    : フラット配列 [x0,y0,z0, x1,y1,z1, ...]
    //    targetSize: 最長辺をこの長さに合わせる (ワールド単位)
    // ---------------------------------------------------------
    static NormParams computeParams(const std::vector<float>& verts,
                                    float targetSize = 2.0f);

    // ---------------------------------------------------------
    // 2. verts にパラメータをインプレース適用する
    //    (center を引いて scale を掛ける)
    // ---------------------------------------------------------
    static void applyToVerts(std::vector<float>& verts,
                             const NormParams& p);

    // ---------------------------------------------------------
    // 3. SoftBodyPhysics::MeshData 全体に適用する
    //    verts だけ変換し UV/indices はそのまま
    // ---------------------------------------------------------
    static void applyToMeshData(SoftBodyPhysics::MeshData& md,
                                const NormParams& p);

    // ---------------------------------------------------------
    // 4. 正規化済みの verts + indices を OBJ ファイルに保存する
    //    CentVoxTetrahedralizerHybrid の portalPath に渡す用
    // ---------------------------------------------------------
    static bool saveAsObj(const std::vector<float>& verts,
                          const std::vector<int>&   indices,
                          const std::string&         outPath);

    // convenience: MeshData から直接 OBJ 保存
    static bool saveAsObj(const SoftBodyPhysics::MeshData& md,
                          const std::string& outPath);
};

#endif // MESH_NORMALIZER_H
