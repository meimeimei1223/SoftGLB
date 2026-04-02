// input-touch.js - Mobile touch input handling
// Web3 SoftBody Physics - Touch input module

//=========================================================================
// Touch Input Handler Class  
//=========================================================================
class TouchInputHandler {
    constructor(core) {
        this.core = core;
        this.canvas = core.canvas;
        
        // Touch state tracking
        this.touches = {};
        this.lastTouch = {x: 0, y: 0};
        this.pinchDistance = 0;
        this.isPinching = false;
        this.isRotating = false;
        
        // Grab state
        this.grabActive = false;
        this.grabStartPos = {x: 0, y: 0};
        this.grabTimer = 0;
        
        // Touch gesture detection (platform-optimized)
        this.tapTimeout = null;
        this.longPressTimeout = null;
        this.gestureThreshold = 10; // pixels
        
        // Platform-specific settings
        const config = getPlatformConfig('touch');
        this.touchSensitivity = config.touchSensitivity || 0.002;
        this.longPressTime = config.longPressTime || 600;
        this.cameraSpeed = config.cameraSpeed || 0.2;
        
        console.log('[TouchInputHandler] Initialized for mobile/tablet');
    }

    //=========================================================================
    // Event Listener Setup
    //=========================================================================
    setupEventListeners() {
        // Touch events with arrow functions (avoid bind issues)
        this.canvas.addEventListener('touchstart', (e) => this.onTouchStart(e), {passive: false});
        this.canvas.addEventListener('touchmove', (e) => this.onTouchMove(e), {passive: false});
        this.canvas.addEventListener('touchend', (e) => this.onTouchEnd(e), {passive: false});
        this.canvas.addEventListener('touchcancel', (e) => this.onTouchEnd(e));
        
        // Prevent context menu on mobile
        this.canvas.addEventListener('contextmenu', e => e.preventDefault());
        
        // Window events
        window.addEventListener('resize', () => this.onResize());
        window.addEventListener('orientationchange', () => {
            setTimeout(() => this.onResize(), 100); // Delay for orientation change
        });
        
        console.log('[TouchInputHandler] Touch event listeners attached');
    }

    //=========================================================================
    // Touch Event Handlers
    //=========================================================================
    onTouchStart(e) {
        e.preventDefault();
        
        const touches = Array.from(e.touches);
        this.updateTouchState(touches);
        
        if (touches.length === 1) {
            // Single touch - potential tap or drag
            this.handleSingleTouchStart(touches[0]);
        } else if (touches.length === 2) {
            // Two finger - pinch zoom
            this.handlePinchStart(touches);
        }
    }

    onTouchMove(e) {
        e.preventDefault();
        
        const touches = Array.from(e.touches);
        this.updateTouchState(touches);
        
        if (touches.length === 1) {
            this.handleSingleTouchMove(touches[0]);
        } else if (touches.length === 2) {
            this.handlePinchMove(touches);
        }
    }

    onTouchEnd(e) {
        e.preventDefault();
        
        const touches = Array.from(e.touches);
        
        if (touches.length === 0) {
            // All touches ended
            this.handleAllTouchesEnd();
        }
        
        this.updateTouchState(touches);
    }

    //=========================================================================
    // Touch Gesture Handlers
    //=========================================================================
    handleSingleTouchStart(touch) {
        this.lastTouch = {x: touch.clientX, y: touch.clientY};
        this.grabStartPos = {x: touch.clientX, y: touch.clientY};
        this.grabTimer = performance.now();
        
        // Clear previous timeouts
        if (this.tapTimeout) clearTimeout(this.tapTimeout);
        if (this.longPressTimeout) clearTimeout(this.longPressTimeout);
        
        // ★ 即座にメッシュヒットテスト（自然なタッチ操作）
        const hit = this.raycastMesh(touch.clientX, touch.clientY);
        if (hit) {
            // メッシュヒット → 即座にグラブ開始
            this.startMeshGrab(hit);
            this.showTouchFeedback(touch.clientX, touch.clientY, 'Grabbed!');
            console.log('[TouchInput] Immediate mesh grab started');
        } else {
            // メッシュなし → カメラ操作準備
            this.isRotating = true;
            console.log('[TouchInput] Camera rotation mode');
        }
        
        // ロングプレス検出（弾丸射撃用）
        this.longPressTimeout = setTimeout(() => {
            if (!this.grabActive) { // グラブ中でなければ弾丸射撃
                this.handleLongPress(touch.clientX, touch.clientY);
            }
        }, this.longPressTime);
    }

    handleSingleTouchMove(touch) {
        const dx = touch.clientX - this.lastTouch.x;
        const dy = touch.clientY - this.lastTouch.y;
        const distance = Math.sqrt(dx*dx + dy*dy);
        
        // Cancel long press if moved too much
        if (distance > this.gestureThreshold && this.longPressTimeout) {
            clearTimeout(this.longPressTimeout);
            this.longPressTimeout = null;
        }
        
        if (this.grabActive) {
            // Mesh grabbing (touch-optimized)
            this.moveMeshGrab(touch.clientX, touch.clientY);
        } else if (!this.isPinching) {
            // Camera rotation (single finger drag)
            this.handleCameraRotation(dx, dy);
        }
        
        this.lastTouch = {x: touch.clientX, y: touch.clientY};
    }

    handlePinchStart(touches) {
        this.isPinching = true;
        this.pinchDistance = this.getTouchDistance(touches[0], touches[1]);
        
        // Cancel other gestures
        if (this.longPressTimeout) {
            clearTimeout(this.longPressTimeout);
            this.longPressTimeout = null;
        }
    }

    handlePinchMove(touches) {
        if (!this.isPinching) return;
        
        const newDistance = this.getTouchDistance(touches[0], touches[1]);
        const scale = newDistance / this.pinchDistance;
        
        // Zoom based on pinch
        const zoomDelta = (1 - scale) * this.core.camera.radius * 0.1;
        this.core.camera.radius = Math.max(1, Math.min(30, this.core.camera.radius + zoomDelta));
        
        this.pinchDistance = newDistance;
    }

    handleAllTouchesEnd() {
        // Clear all timeouts
        if (this.longPressTimeout) {
            clearTimeout(this.longPressTimeout);
            this.longPressTimeout = null;
        }
        if (this.tapTimeout) {
            clearTimeout(this.tapTimeout);
            this.tapTimeout = null;
        }
        
        // ★ 指を離したら確実にグラブ終了（自然なタッチ操作）
        if (this.grabActive) {
            this.endMeshGrab();
            this.showTouchFeedback(this.lastTouch.x, this.lastTouch.y, 'Released');
            console.log('[TouchInput] Touch ended - mesh grab released');
        }
        
        // Reset all states
        this.isPinching = false;
        this.isRotating = false;
        this.grabActive = false;
        
        console.log('[TouchInput] All touches ended - states reset');
    }

    //=========================================================================
    // Gesture Recognition (Natural Touch Operations)
    //=========================================================================

    handleLongPress(x, y) {
        console.log('[TouchInput] Long press detected - sphere operation');
        
        // Long press for sphere shooting (mobile alternative to right-click)
        if (this.core.shootSphere) {
            const ray = this.screenToWorldRay(x, y);
            if (ray) {
                const target = this.rayAtDist(ray, this.core.camera.radius * 1.5);
                this.core.fireShootSphere(target[0], target[1], target[2]);
                this.showTouchFeedback(x, y, 'Shot Fired!');
            }
        } else {
            this.showTouchFeedback(x, y, 'Long Press');
        }
    }

    handleCameraRotation(dx, dy) {
        // Platform-optimized rotation sensitivity
        this.core.camera.yaw += dx * this.cameraSpeed;
        this.core.camera.pitch = Math.max(-89, Math.min(89, this.core.camera.pitch + dy * this.cameraSpeed));
        
        console.log('[TouchInput] Camera rotation - Yaw:', this.core.camera.yaw.toFixed(1), 'Pitch:', this.core.camera.pitch.toFixed(1));
    }

    //=========================================================================
    // Utility Functions
    //=========================================================================
    getTouchDistance(touch1, touch2) {
        const dx = touch1.clientX - touch2.clientX;
        const dy = touch1.clientY - touch2.clientY;
        return Math.sqrt(dx*dx + dy*dy);
    }

    updateTouchState(touches) {
        this.touches = {};
        touches.forEach((touch, index) => {
            this.touches[touch.identifier] = {
                x: touch.clientX,
                y: touch.clientY,
                index: index
            };
        });
    }

    showTouchFeedback(x, y, message) {
        // Visual feedback for touch gestures
        const feedback = document.createElement('div');
        feedback.textContent = message;
        feedback.style.cssText = `
            position: fixed; left: ${x}px; top: ${y}px;
            background: rgba(255,255,255,0.9); color: black;
            padding: 6px 12px; border-radius: 20px;
            font-size: 12px; font-weight: bold;
            pointer-events: none; z-index: 9999;
            transform: translate(-50%, -50%);
        `;
        
        document.body.appendChild(feedback);
        
        setTimeout(() => {
            if (feedback.parentNode) {
                document.body.removeChild(feedback);
            }
        }, 1000);
    }

    //=========================================================================
    // Physics Operations (Touch-adapted from PC version)
    //=========================================================================
    screenToWorldRay(sx, sy) {
        const rect = this.canvas.getBoundingClientRect();
        const x = ((sx - rect.left) / rect.width) * 2 - 1;
        const y = 1 - ((sy - rect.top) / rect.height) * 2;
        
        const viewMat = this.core.updateViewMatrix();
        const projMat = this.core.updateProjMatrix();
        
        // Use PC version's matrix multiplication
        const vpMat = new Float32Array(16);
        this.multiplyMatrices(vpMat, projMat, viewMat);
        const invVP = this.invertMatrix(vpMat);
        
        if (!invVP) return null;
        
        const transformPoint = (m, p) => {
            const w = m[3]*p[0] + m[7]*p[1] + m[11]*p[2] + m[15];
            return [
                (m[0]*p[0] + m[4]*p[1] + m[8]*p[2] + m[12]) / w,
                (m[1]*p[0] + m[5]*p[1] + m[9]*p[2] + m[13]) / w,
                (m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14]) / w
            ];
        };
        
        const near = transformPoint(invVP, [x, y, -1]);
        const far = transformPoint(invVP, [x, y, 1]);
        
        return { 
            origin: near, 
            direction: SoftBodyCore.normalize(SoftBodyCore.sub(far, near)) 
        };
    }

    rayAtDist(ray, dist) {
        return [
            ray.origin[0] + ray.direction[0] * dist,
            ray.origin[1] + ray.direction[1] * dist,
            ray.origin[2] + ray.direction[2] * dist
        ];
    }

    raycastMesh(x, y) {
        if (!this.core.softBody) return null;
        
        const ray = this.screenToWorldRay(x, y);
        if (!ray) return null;
        
        const positions = this.core.softBody.getVisPositions();
        const indices = this.core.softBody.getVisIndices();
        
        let closestHit = null;
        
        for (let i = 0; i < indices.length; i += 3) {
            const i0 = indices[i] * 3;
            const i1 = indices[i + 1] * 3;
            const i2 = indices[i + 2] * 3;
            
            const v0 = [positions[i0], positions[i0 + 1], positions[i0 + 2]];
            const v1 = [positions[i1], positions[i1 + 1], positions[i1 + 2]];
            const v2 = [positions[i2], positions[i2 + 1], positions[i2 + 2]];
            
            // Skip ungrabbable fixed region (same as PC)
            if (v0[1] <= this.core.ungrabbableThreshold && 
                v1[1] <= this.core.ungrabbableThreshold && 
                v2[1] <= this.core.ungrabbableThreshold) {
                continue;
            }
            
            const hit = this.rayTriangleIntersect(ray.origin, ray.direction, v0, v1, v2);
            if (hit && (!closestHit || hit.distance < closestHit.distance)) {
                closestHit = hit;
            }
        }
        
        return closestHit;
    }

    rayTriangleIntersect(rayOrigin, rayDirection, v0, v1, v2) {
        const EPSILON = 0.0000001;
        const edge1 = SoftBodyCore.sub(v1, v0);
        const edge2 = SoftBodyCore.sub(v2, v0);
        const h = SoftBodyCore.cross(rayDirection, edge2);
        const a = SoftBodyCore.dot(edge1, h);
        
        if (a > -EPSILON && a < EPSILON) return null;
        
        const f = 1.0 / a;
        const s = SoftBodyCore.sub(rayOrigin, v0);
        const u = f * SoftBodyCore.dot(s, h);
        
        if (u < 0.0 || u > 1.0) return null;
        
        const q = SoftBodyCore.cross(s, edge1);
        const v = f * SoftBodyCore.dot(rayDirection, q);
        
        if (v < 0.0 || u + v > 1.0) return null;
        
        const t = f * SoftBodyCore.dot(edge2, q);
        
        if (t > EPSILON) {
            return {
                distance: t,
                point: [
                    rayOrigin[0] + rayDirection[0] * t,
                    rayOrigin[1] + rayDirection[1] * t,
                    rayOrigin[2] + rayDirection[2] * t
                ]
            };
        }
        
        return null;
    }

    startMeshGrab(hit) {
        this.core.softBody.startGrab(hit.point[0], hit.point[1], hit.point[2]);
        this.grabActive = true;
        this.grabStartPos = hit.point;
        this.grabTimer = performance.now();
        
        // Touch feedback
        this.showTouchFeedback(this.lastTouch.x, this.lastTouch.y, 'Grabbed!');
        console.log('[TouchInput] Mesh grab started at:', hit.point);
    }

    moveMeshGrab(x, y) {
        if (!this.grabActive) return;
        
        const now = performance.now();
        const dt = Math.max((now - this.grabTimer) / 1000, 1e-4);
        
        // ★ タッチ向け改良：画面座標の変化をカメラ座標系に変換
        const touchDx = x - this.lastTouch.x;
        const touchDy = y - this.lastTouch.y;
        
        // カメラの向きに基づいて3D移動方向を計算
        const camPos = this.core.camera.getPosition();
        const camYaw = this.core.camera.yaw * Math.PI / 180;
        
        // カメラのright方向とup方向を計算
        const rightX = Math.cos(camYaw + Math.PI/2);  // カメラの右方向
        const rightZ = Math.sin(camYaw + Math.PI/2);
        const upY = 1.0; // Y軸は常に上方向
        
        // タッチの移動量をスケール（プラットフォーム最適化済み感度）
        const sensitivity = this.core.camera.radius * this.touchSensitivity;
        
        // 画面のX移動 → 3D空間のright方向、Y移動 → 3D空間のup方向  
        const worldDx = touchDx * sensitivity;
        const worldDy = touchDy * sensitivity; // ★ Y軸反転を削除（自然な上下移動）
        
        // 現在のグラブ位置からの移動
        const newX = this.grabStartPos[0] + rightX * worldDx;
        const newY = this.grabStartPos[1] + upY * worldDy;
        const newZ = this.grabStartPos[2] + rightZ * worldDx;
        
        // 速度計算
        const vx = (newX - this.grabStartPos[0]) / dt;
        const vy = (newY - this.grabStartPos[1]) / dt;
        const vz = (newZ - this.grabStartPos[2]) / dt;
        
        this.core.softBody.moveGrabbed(newX, newY, newZ, vx, vy, vz);
        this.grabStartPos = [newX, newY, newZ];
        this.grabTimer = now;
        
        console.log('[TouchInput] Mesh drag - TouchDelta:', touchDx.toFixed(1), touchDy.toFixed(1), 
                   'WorldDelta:', worldDx.toFixed(3), worldDy.toFixed(3),
                   'CamYaw:', (this.core.camera.yaw).toFixed(1),
                   'RightVec:', rightX.toFixed(2), rightZ.toFixed(2));
    }

    endMeshGrab() {
        if (this.grabActive && this.core.softBody) {
            this.core.softBody.endGrab(this.grabStartPos[0], this.grabStartPos[1], this.grabStartPos[2], 0, 0, 0);
            this.grabActive = false;
            console.log('[TouchInput] Mesh grab ended');
        }
    }

    // Matrix helpers (same as PC version)
    multiplyMatrices(out, a, b) {
        for (let i = 0; i < 4; i++) {
            for (let j = 0; j < 4; j++) {
                out[i*4 + j] = a[j]*b[i*4] + a[4+j]*b[i*4+1] + a[8+j]*b[i*4+2] + a[12+j]*b[i*4+3];
            }
        }
    }

    invertMatrix(m) {
        const out = new Float32Array(16);
        const [m00,m01,m02,m03,m10,m11,m12,m13,m20,m21,m22,m23,m30,m31,m32,m33] = [...m];
        
        const b00=m00*m11-m01*m10, b01=m00*m12-m02*m10, b02=m00*m13-m03*m10;
        const b03=m01*m12-m02*m11, b04=m01*m13-m03*m11, b05=m02*m13-m03*m12;
        const b06=m20*m31-m21*m30, b07=m20*m32-m22*m30, b08=m20*m33-m23*m30;
        const b09=m21*m32-m22*m31, b10=m21*m33-m23*m31, b11=m22*m33-m23*m32;
        
        let det = b00*b11 - b01*b10 + b02*b09 + b03*b08 - b04*b07 + b05*b06;
        if (!det) return null;
        det = 1 / det;
        
        out[0]=(m11*b11-m12*b10+m13*b09)*det;
        out[1]=(m02*b10-m01*b11-m03*b09)*det;
        out[2]=(m31*b05-m32*b04+m33*b03)*det;
        out[3]=(m22*b04-m21*b05-m23*b03)*det;
        out[4]=(m12*b08-m10*b11-m13*b07)*det;
        out[5]=(m00*b11-m02*b08+m03*b07)*det;
        out[6]=(m32*b02-m30*b05-m33*b01)*det;
        out[7]=(m20*b05-m22*b02+m23*b01)*det;
        out[8]=(m10*b10-m11*b08+m13*b06)*det;
        out[9]=(m01*b08-m00*b10-m03*b06)*det;
        out[10]=(m30*b04-m31*b02+m33*b00)*det;
        out[11]=(m21*b02-m20*b04-m23*b00)*det;
        out[12]=(m11*b07-m10*b09-m12*b06)*det;
        out[13]=(m00*b09-m01*b07+m02*b06)*det;
        out[14]=(m31*b01-m30*b03-m32*b00)*det;
        out[15]=(m20*b03-m21*b01+m22*b00)*det;
        
        return out;
    }
}

//=========================================================================
// Export
//=========================================================================
if (typeof module !== 'undefined') {
    module.exports = TouchInputHandler;
}