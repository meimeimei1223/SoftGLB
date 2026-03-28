#ifndef VECTORMATH_H
#define VECTORMATH_H

#include <vector>
// ベクトル演算のユーティリティクラス
class VectorMath {
public:
    // ベクトルをゼロに設定
    static void vecSetZero(std::vector<float>& a, int anr) {
        anr *= 3;
        a[anr] = 0.0f;
        a[anr + 1] = 0.0f;
        a[anr + 2] = 0.0f;
    }

    // ベクトルのスケーリング
    static void vecScale(std::vector<float>& a, int anr, float scale) {
        anr *= 3;
        a[anr] *= scale;
        a[anr + 1] *= scale;
        a[anr + 2] *= scale;
    }

    // ベクトルのコピー
    static void vecCopy(std::vector<float>& a, int anr,
                       const std::vector<float>& b, int bnr) {
        anr *= 3; bnr *= 3;
        a[anr] = b[bnr];
        a[anr + 1] = b[bnr + 1];
        a[anr + 2] = b[bnr + 2];
    }

    // ベクトルの加算
    static void vecAdd(std::vector<float>& a, int anr,
                      const std::vector<float>& b, int bnr, float scale = 1.0f) {
        anr *= 3; bnr *= 3;
        a[anr] += b[bnr] * scale;
        a[anr + 1] += b[bnr + 1] * scale;
        a[anr + 2] += b[bnr + 2] * scale;
    }

    // ベクトルの差の設定
    static void vecSetDiff(std::vector<float>& dst, int dnr,
                          const std::vector<float>& a, int anr,
                          const std::vector<float>& b, int bnr, float scale = 1.0f) {
        dnr *= 3; anr *= 3; bnr *= 3;
        dst[dnr] = (a[anr] - b[bnr]) * scale;
        dst[dnr + 1] = (a[anr + 1] - b[bnr + 1]) * scale;
        dst[dnr + 2] = (a[anr + 2] - b[bnr + 2]) * scale;
    }

    // ベクトルの長さの二乗
    static float vecLengthSquared(const std::vector<float>& a, int anr) {
        anr *= 3;
        float a0 = a[anr], a1 = a[anr + 1], a2 = a[anr + 2];
        return a0 * a0 + a1 * a1 + a2 * a2;
    }

    // ２点間の距離の二乗
    static float vecDistSquared(const std::vector<float>& a, int anr,
                              const std::vector<float>& b, int bnr) {
        anr *= 3; bnr *= 3;
        float a0 = a[anr] - b[bnr];
        float a1 = a[anr + 1] - b[bnr + 1];
        float a2 = a[anr + 2] - b[bnr + 2];
        return a0 * a0 + a1 * a1 + a2 * a2;
    }

    // ドット積
    static float vecDot(const std::vector<float>& a, int anr,
                       const std::vector<float>& b, int bnr) {
        anr *= 3; bnr *= 3;
        return a[anr] * b[bnr] +
               a[anr + 1] * b[bnr + 1] +
               a[anr + 2] * b[bnr + 2];
    }

    // クロス積の設定
    static void vecSetCross(std::vector<float>& a, int anr,
                           const std::vector<float>& b, int bnr,
                           const std::vector<float>& c, int cnr) {
        anr *= 3; bnr *= 3; cnr *= 3;
        a[anr] = b[bnr + 1] * c[cnr + 2] - b[bnr + 2] * c[cnr + 1];
        a[anr + 1] = b[bnr + 2] * c[cnr] - b[bnr] * c[cnr + 2];
        a[anr + 2] = b[bnr] * c[cnr + 1] - b[bnr + 1] * c[cnr];
    }

    // 3x3行列の行列式を計算
    static float matGetDeterminant(const std::vector<float>& A) {
        float a11 = A[0], a12 = A[3], a13 = A[6];
        float a21 = A[1], a22 = A[4], a23 = A[7];
        float a31 = A[2], a32 = A[5], a33 = A[8];

        return a11 * a22 * a33 + a12 * a23 * a31 + a13 * a21 * a32
             - a13 * a22 * a31 - a12 * a21 * a33 - a11 * a23 * a32;
    }

    // 行列とベクトルの積を設定
    static void matSetMult(const std::vector<float>& A, std::vector<float>& a, int anr,
                           const std::vector<float>& b, int bnr) {
        bnr *= 3;
        float bx = b[bnr++];
        float by = b[bnr++];
        float bz = b[bnr];

        VectorMath::vecSetZero(a, anr);
        VectorMath::vecAdd(a, anr, A, 0, bx);
        VectorMath::vecAdd(a, anr, A, 1, by);
        VectorMath::vecAdd(a, anr, A, 2, bz);
    }

    // 逆行列を計算
    static void matSetInverse(std::vector<float>& A) {
        float det = matGetDeterminant(A);
        if (det == 0.0f) {
            for (int i = 0; i < 9; i++) {
                A[i] = 0.0f;
            }
            return;
        }

        float invDet = 1.0f / det;
        float a11 = A[0], a12 = A[3], a13 = A[6];
        float a21 = A[1], a22 = A[4], a23 = A[7];
        float a31 = A[2], a32 = A[5], a33 = A[8];

        A[0] =  (a22 * a33 - a23 * a32) * invDet;
        A[3] = -(a12 * a33 - a13 * a32) * invDet;
        A[6] =  (a12 * a23 - a13 * a22) * invDet;
        A[1] = -(a21 * a33 - a23 * a31) * invDet;
        A[4] =  (a11 * a33 - a13 * a31) * invDet;
        A[7] = -(a11 * a23 - a13 * a21) * invDet;
        A[2] =  (a21 * a32 - a22 * a31) * invDet;
        A[5] = -(a11 * a32 - a12 * a31) * invDet;
        A[8] =  (a11 * a22 - a12 * a21) * invDet;
    }
};

#endif // VECTORMATH_H
