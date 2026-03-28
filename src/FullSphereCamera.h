// FullSphereCamera.h
// 球面カメラシステム（AR内部パラメータ対応）
#ifndef FULLSPHERECAMERA_H
#define FULLSPHERECAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <iostream>

class FullSphereCamera {
public:
    // ========================================
    // カメラ状態
    // ========================================
    glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    // 計算されたカメラベクトル
    glm::vec3 cameraPos;
    glm::vec3 cameraDirection;
    glm::vec3 cameraRight;
    glm::vec3 cameraUp;

    // 行列を保持
    glm::mat4 view;
    glm::mat4 projection;

    // ========================================
    // カメラパラメータ
    // ========================================
    float initialRadius = 11.35f;
    float gRadius = 11.35f;
    
    // 感度設定
    float MOUSE_SENSITIVITY = 0.005f;       // 回転感度
    float LIGHT_MOUSE_SENSITIVITY = 0.01f;  // パン・軽い移動の感度
    float ZOOM_SENSITIVITY = -1.0f;         // ズーム感度
    float SCALE_SPEED = 1.1f;               // ★追加: スケール変更速度（カッター等）

    // 距離制限
    float minRadius = 2.0f;
    float maxRadius = 80.0f;

    // ========================================
    // AR内部パラメータ（カメラキャリブレーション）
    // ========================================
    float fx = 800.0f;      // 焦点距離X（ピクセル単位）
    float fy = 800.0f;      // 焦点距離Y（ピクセル単位）
    float cx = 512.0f;      // 主点X（画像中心）
    float cy = 384.0f;      // 主点Y（画像中心）
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    // FOVとの互換性
    float gFOV = 45.0f;
    bool useIntrinsics = false;  // 内部パラメータを使うかFOVを使うか

    // ========================================
    // コンストラクタ
    // ========================================
    FullSphereCamera() = default;
    
    // ウィンドウサイズを指定して初期化
    FullSphereCamera(int windowWidth, int windowHeight) {
        cx = windowWidth / 2.0f;
        cy = windowHeight / 2.0f;
        pWindowWidth = nullptr;
        pWindowHeight = nullptr;
    }

    // ========================================
    // 初期化・設定
    // ========================================
    
    // ウィンドウサイズのポインタを設定（グローバル変数との連携用）
    void setWindowSizePointers(int* width, int* height) {
        pWindowWidth = width;
        pWindowHeight = height;
        if (width && height) {
            cx = *width / 2.0f;
            cy = *height / 2.0f;
        }
    }

    // グローバル行列への参照を設定（互換性のため）
    void setGlobalMatrixPointers(glm::mat4* viewPtr, glm::mat4* projPtr, glm::mat4* modelPtr, glm::vec3* objPosPtr) {
        pGlobalView = viewPtr;
        pGlobalProjection = projPtr;
        pGlobalModel = modelPtr;
        pObjPos = objPosPtr;
    }

    // 初期状態にリセット
    void resetToInitialState() {
        cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
        worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
        cameraDirection = glm::vec3(0.0f, 0.0f, 1.0f);
        rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        gRadius = initialRadius;
        std::cout << "Camera reset to initial state" << std::endl;
    }

    // 完全リセット
    void Reset() {
        rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        gRadius = 10.0f;
        cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    }

    // ========================================
    // AR内部パラメータ設定
    // ========================================
    
    // 内部パラメータからFOVを計算（デバッグ表示用）
    float calculateFOVFromIntrinsics() const {
        int height = getWindowHeight();
        return glm::degrees(2.0f * atan(height / (2.0f * fy)));
    }

    // FOVから内部パラメータを設定（後方互換性用）
    void setIntrinsicsFromFOV(float fov) {
        gFOV = fov;
        float fovRad = glm::radians(fov);
        int width = getWindowWidth();
        int height = getWindowHeight();
        fy = height / (2.0f * tan(fovRad / 2.0f));
        fx = fy;  // アスペクト比を保つ
        cx = width / 2.0f;
        cy = height / 2.0f;
    }

    // カメラキャリブレーションから内部パラメータを設定
    void setIntrinsics(float calibFx, float calibFy, float calibCx, float calibCy) {
        fx = calibFx;
        fy = calibFy;
        cx = calibCx;
        cy = calibCy;
        useIntrinsics = true;
        gFOV = calculateFOVFromIntrinsics();  // デバッグ用
    }

    // fx, fyベースのプロジェクション行列を作成
    glm::mat4 createProjectionFromIntrinsics() const {
        int width = getWindowWidth();
        int height = getWindowHeight();
        
        glm::mat4 proj(0.0f);

        // OpenGL NDC座標系への変換を含むプロジェクション行列
        proj[0][0] = 2.0f * fx / width;
        proj[1][1] = 2.0f * fy / height;
        proj[2][0] = 1.0f - 2.0f * cx / width;
        proj[2][1] = 1.0f - 2.0f * cy / height;
        proj[2][2] = -(farPlane + nearPlane) / (farPlane - nearPlane);
        proj[2][3] = -1.0f;
        proj[3][2] = -2.0f * farPlane * nearPlane / (farPlane - nearPlane);

        return proj;
    }

    // ========================================
    // カメラ操作
    // ========================================
    
    // 回転
    void Rotate(float deltaX, float deltaY) {
        // スクリーン空間での回転を計算
        glm::mat4 viewMatrix = glm::lookAt(cameraPos, cameraTarget, cameraUp);
        glm::mat4 invView = glm::inverse(viewMatrix);

        // スクリーンの右方向と上方向をワールド空間に変換
        glm::vec3 screenRight = glm::vec3(invView[0]);
        glm::vec3 screenUp = glm::vec3(invView[1]);

        // スクリーン基準で回転
        glm::quat pitchQuat = glm::angleAxis(-deltaY * MOUSE_SENSITIVITY, screenRight);
        glm::quat yawQuat = glm::angleAxis(-deltaX * MOUSE_SENSITIVITY, screenUp);

        // 回転を適用
        rotation = glm::normalize(yawQuat * pitchQuat * rotation);
    }

    // パン
    void Pan(float dx, float dy) {
        glm::vec3 moveRight = cameraRight * dx * LIGHT_MOUSE_SENSITIVITY;
        moveRight *= -1.0;
        glm::vec3 moveUp = cameraUp * dy * LIGHT_MOUSE_SENSITIVITY;
        glm::vec3 movement = moveRight + moveUp;  // マイナスを削除
        cameraTarget += movement;
    }

    // ズーム
    void Zoom(float delta) {
        gRadius += delta * ZOOM_SENSITIVITY;
        gRadius = glm::clamp(gRadius, minRadius, maxRadius);
    }

    // カメラ更新（毎フレーム呼び出し）
    void UpdateCamera(float deltaTime = 0.016f) {
        // 四元数から回転行列を作成
        glm::mat4 rotMatrix = glm::mat4_cast(rotation);

        // カメラ位置を計算
        glm::vec4 initialPos = glm::vec4(0.0f, 0.0f, gRadius, 1.0f);
        cameraPos = cameraTarget + glm::vec3(rotMatrix * initialPos);

        // カメラの向きベクトルを回転行列から直接取得
        cameraDirection = glm::normalize(cameraPos - cameraTarget);

        // 右ベクトルと上ベクトルも回転行列から取得
        glm::vec4 rightVec = rotMatrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec4 upVec = rotMatrix * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);

        cameraRight = glm::normalize(glm::vec3(rightVec));
        cameraUp = glm::normalize(glm::vec3(upVec));

        // ビュー行列とプロジェクション行列を生成して保存
        this->view = glm::lookAt(cameraPos, cameraTarget, cameraUp);

        // 内部パラメータまたはFOVベースでプロジェクション行列を生成
        int width = getWindowWidth();
        int height = getWindowHeight();
        
        if (useIntrinsics) {
            this->projection = createProjectionFromIntrinsics();
        } else {
            this->projection = glm::perspective(glm::radians(gFOV),
                                                float(width) / float(height),
                                                nearPlane, farPlane);
        }

        // グローバル変数も更新（既存コードとの互換性のため）
        updateGlobalMatrices();
    }

    // ========================================
    // ウィンドウリサイズ処理
    // ========================================
    void onWindowResize(int width, int height) {
        // 主点を新しい画像中心に更新（内部パラメータ使用時のみ）
        if (useIntrinsics) {
            cx = width / 2.0f;
            cy = height / 2.0f;
        }
    }

    // ========================================
    // ユーティリティ
    // ========================================
    
    // メッシュの中心を計算
    static glm::vec3 calculateMeshCenter(const std::vector<float>& vertices) {
        glm::vec3 center(0.0f);
        size_t vertexCount = vertices.size() / 3;
        if (vertexCount == 0) return center;

        for (size_t i = 0; i < vertices.size(); i += 3) {
            center.x += vertices[i];
            center.y += vertices[i + 1];
            center.z += vertices[i + 2];
        }
        center /= static_cast<float>(vertexCount);
        return center;
    }

    // デバッグ情報の出力
    void printCameraInfo() const {
        std::cout << "=== Full Sphere Camera Info ===" << std::endl;
        if (useIntrinsics) {
            std::cout << "Using Intrinsics: fx=" << fx << ", fy=" << fy
                      << ", cx=" << cx << ", cy=" << cy << std::endl;
            std::cout << "Equivalent FOV: " << calculateFOVFromIntrinsics() << " degrees" << std::endl;
        } else {
            std::cout << "Using FOV: " << gFOV << " degrees" << std::endl;
        }
        std::cout << "Position: (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")" << std::endl;
        std::cout << "Target: (" << cameraTarget.x << ", " << cameraTarget.y << ", " << cameraTarget.z << ")" << std::endl;
        std::cout << "Radius: " << gRadius << std::endl;
        std::cout << "Sensitivities: Mouse=" << MOUSE_SENSITIVITY 
                  << ", Light=" << LIGHT_MOUSE_SENSITIVITY
                  << ", Zoom=" << ZOOM_SENSITIVITY 
                  << ", Scale=" << SCALE_SPEED << std::endl;
    }

    // ========================================
    // ゲッター
    // ========================================
    int getWindowWidth() const {
        return pWindowWidth ? *pWindowWidth : 1024;
    }
    
    int getWindowHeight() const {
        return pWindowHeight ? *pWindowHeight : 768;
    }

private:
    // ウィンドウサイズへのポインタ
    int* pWindowWidth = nullptr;
    int* pWindowHeight = nullptr;

    // グローバル行列へのポインタ（互換性のため）
    glm::mat4* pGlobalView = nullptr;
    glm::mat4* pGlobalProjection = nullptr;
    glm::mat4* pGlobalModel = nullptr;
    glm::vec3* pObjPos = nullptr;

    // グローバル行列を更新（互換性のため）
    void updateGlobalMatrices() {
        if (pGlobalView) *pGlobalView = this->view;
        if (pGlobalProjection) *pGlobalProjection = this->projection;
        if (pGlobalModel && pObjPos) {
            *pGlobalModel = glm::translate(glm::mat4(1.0f), *pObjPos);
        }
    }
};

#endif // FULLSPHERECAMERA_H
