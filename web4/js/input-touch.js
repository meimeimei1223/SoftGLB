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
        
        // ★ FIXED_DRAG state (same as PC version)
        this.fixedDragActive = false;
        this.fixedDragDist   = 0;
        this.fixedDragPrevPos = [0, 0, 0];
        
        // ★ ビクつき修正用（デルタ加算方式）
        this.grabVertPos = [0, 0, 0];  // grabId頂点の追跡位置（デルタ加算の基点）
        this.grabSurfaceOffset = [0, 0, 0];
        this.grabPrevRay = [0, 0, 0];
        this.fixedSpherePos = [0, 0, 0];  // 黄色固定スフィア位置
        this.grabSpherePos = [0, 0, 0];   // 緑配置スフィア位置
        
        // ★ スフィアコントロールパネル状態
        this.sphereCtrlActive  = false;   // パネル内ドラッグ中か
        this.sphereCtrlOrigin  = {x: 0, y: 0}; // このタッチ開始点（ジョイスティック原点）
        this.sphereCtrlTouchId = -1;      // 追跡するtouch.identifier
        this.sphereCtrlBasePos = [0, 0, 0]; // パネル操作開始時のグラブ位置（変形リセット用）
        this.grabSuspended     = false;   // ★ グラブ一時停止状態（スフィアタッチ時のみ有効）
        // パネルのレイアウト定数（右下固定）
        this.CTRL_PANEL_R      = 240;     // パネル半径px（2倍に）
        this.CTRL_PANEL_MARGIN = 30;      // 画面端からのマージンpx
        this.CTRL_SENSITIVITY  = 0.001;   // ドラッグpx → 3D移動スケール
        
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
        
        // ★ スフィアコントロールパネルDOM生成
        this._buildSphereCtrlPanel();
        
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
            const t = touches[0];
            // ★ パネル内タッチ判定を最優先（スフィアが存在するときのみ）
            if (this._hasSphere() && this._isInCtrlPanel(t.clientX, t.clientY)) {
                this._startSphereCtrl(t);
            } else {
                this.handleSingleTouchStart(t);
            }
        } else if (touches.length === 2) {
            // パネルドラッグ中に2本目が来たら解除
            if (this.sphereCtrlActive) this._endSphereCtrl();
            this.handlePinchStart(touches);
        }
    }

    onTouchMove(e) {
        e.preventDefault();

        const touches = Array.from(e.touches);
        this.updateTouchState(touches);

        if (this.sphereCtrlActive) {
            // ★ パネルドラッグ：追跡IDのtouchを探す
            const t = Array.from(e.touches).find(
                t => t.identifier === this.sphereCtrlTouchId
            );
            if (t) this._moveSphereCtrl(t.clientX, t.clientY);
            return;
        }

        if (touches.length === 1) {
            this.handleSingleTouchMove(touches[0]);
        } else if (touches.length === 2) {
            this.handlePinchMove(touches);
        }
    }

    onTouchEnd(e) {
        e.preventDefault();

        const remaining = Array.from(e.touches);

        // ★ パネルドラッグが終了したか確認
        const stillTracked = remaining.some(
            t => t.identifier === this.sphereCtrlTouchId
        );
        if (this.sphereCtrlActive && !stillTracked) {
            this._endSphereCtrl();
        }

        if (remaining.length === 0) {
            this.handleAllTouchesEnd();
        }

        this.updateTouchState(remaining);
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
        
        // ★ スフィア自体のタッチ判定（3D空間で近接判定）
        const sphereHit = this._checkSphereHit(touch.clientX, touch.clientY);
        
        if (sphereHit) {
            // ★ スフィアタッチ：そのスフィアの制御を再開
            if (sphereHit.type === 'grab' && this.grabActive) {
                // グラブ中の緑スフィア → 一時停止解除 + パネル表示
                this.grabSuspended = false;
                this._showCtrlPanel();
                console.log('[TouchInput] Grab sphere touched - control reactivated');
                return;
            } else if (sphereHit.type === 'fixed') {
                // 固定スフィア → パネル表示
                this._showCtrlPanel();
                console.log('[TouchInput] Fixed sphere touched - panel shown');
                return;
            }
        }
        
        // ★ メッシュヒットテスト
        const hit = this.raycastMesh(touch.clientX, touch.clientY);
        if (hit) {
            // ★ メッシュヒット：既存スフィアがあれば解除→新規配置
            if (this.grabActive || (this.core.fixedSphere && this.core.fixedSphere.visible)) {
                this._releaseAllSpheres();
                // 解除後、即座に新しい場所にスフィア配置
                this.startMeshGrab(hit);
                this.showTouchFeedback(touch.clientX, touch.clientY, 'Switched!');
                console.log('[TouchInput] Sphere switched to new location');
                return;
            }
            
            // 新規グラブ開始
            this.startMeshGrab(hit);
            this.showTouchFeedback(touch.clientX, touch.clientY, 'Grabbed!');
            console.log('[TouchInput] Immediate mesh grab started');
        } else {
            // ★ メッシュなし → カメラ操作準備（スフィアはそのまま維持）
            this.isRotating = true;
            console.log('[TouchInput] Camera rotation mode - spheres maintained');
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
        
        if (this.fixedDragActive) {
            // ★ FIXED_DRAG（固定点移動）
            this.moveFixedDrag(touch.clientX, touch.clientY);
        } else if (this.grabActive && !this.grabSuspended) {
            // 通常グラブ（一時停止でない場合のみ）
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
        if (this.longPressTimeout) {
            clearTimeout(this.longPressTimeout);
            this.longPressTimeout = null;
        }
        if (this.tapTimeout) {
            clearTimeout(this.tapTimeout);
            this.tapTimeout = null;
        }

        if (this.fixedDragActive) {
            this.fixedDragActive = false;
            this.core.restoreFixedInvMasses();
            console.log('[TouchInput] FIXED_DRAG ended');
        }
        if (this.grabActive) {
            // ★ グラブ一時停止：スフィアタッチ時のみ有効化
            this.grabSuspended = true;
            this._placeGrabSphereAtPosition();
            console.log('[TouchInput] Grab suspended - sphere touch required for control');
        }
        
        // ★ スフィア（配置済みグラブ or 固定）がある場合はパネル表示
        if (this._hasSphere()) {
            this._showCtrlPanel();
            console.log('[TouchInput] Sphere available - control panel shown');
        }

        this.isPinching    = false;
        this.isRotating    = false;
        // ★ grabActive は維持（パネル制御のため）
        this.fixedDragActive = false;

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
            
            const maxY = Math.max(v0[1], v1[1], v2[1]);
            const minY = Math.min(v0[1], v1[1], v2[1]);

            // ★ 修正版判定：境界領域のみスキップ、完全固定域はFIXED_DRAG復活
            const fullyFixed    = (maxY <= this.core.visFixedThreshold);       // 完全固定域
            const touchesBoundary = (minY <= this.core.visUngrabbableThreshold && maxY > this.core.visFixedThreshold); // 境界のみ

            if (touchesBoundary) {
                // 境界またぎ → 完全スキップ（突起防止）
                continue;
            } else if (fullyFixed) {
                // 完全固定域 → FIXED_DRAG（平行移動）
                const hit = this.rayTriangleIntersect(ray.origin, ray.direction, v0, v1, v2);
                if (hit && (!closestHit || hit.distance < closestHit.distance)) {
                    closestHit = { ...hit, isFixed: true };
                }
            } else {
                // 通常グラブ領域
                const hit = this.rayTriangleIntersect(ray.origin, ray.direction, v0, v1, v2);
                if (hit && (!closestHit || hit.distance < closestHit.distance)) {
                    closestHit = { ...hit, isFixed: false };
                }
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
        this.core.restoreFixedInvMasses();

        if (hit.isFixed) {
            // ★ 固定域クリック → 黄色スフィア配置（既存スフィア解除）
            if (this.grabActive && this.core.softBody) {
                this.core.softBody.endGrab(
                    this.grabVertPos[0], this.grabVertPos[1], this.grabVertPos[2], 0, 0, 0
                );
                this.grabActive = false;
                this.grabSuspended = false;
                this.core.updateGrabSphere([0,0,0], false);  // 既存緑スフィア消去
            }
            // 既存固定スフィアも消去（位置移動）
            if (this.core.fixedSphere && this.core.fixedSphere.visible) {
                this.core.updateFixedSphere([0,0,0], false);
                console.log('[TouchInput] Previous fixed sphere cleared for relocation');
            }
            
            // ★ 黄色fixedSphereを配置
            this.core.updateFixedSphere(hit.point, true);
            this.fixedSpherePos = [...hit.point];
            // ★ 物理スフィアとしてドラッグ開始
            if (this.core.fixedSphere) {
                this.core.fixedSphere.startDragAt(hit.point[0], hit.point[1], hit.point[2], hit.distance);
            }
            this.fixedDragActive = false;
            this.grabActive = false;
            // ★ grabSphere非表示
            this.core.updateGrabSphere([0,0,0], false);
            this.showTouchFeedback(this.lastTouch.x, this.lastTouch.y, 'Fixed Sphere!');
            console.log('[TouchInput] Fixed sphere placed and physics enabled');
        } else {
            // ★ 通常グラブ開始前：既存の固定スフィアを消去（頂点制約復元）
            if (this.core.fixedSphere && this.core.fixedSphere.visible) {
                this.core.updateFixedSphere([0,0,0], false);
                this.core.restoreFixedInvMasses(); // ★ 頂点逆制約復元
                console.log('[TouchInput] Previous fixed sphere cleared with constraint restoration');
            }
            
            // 通常グラブ
            const nearestVertPos = this._findNearestVertexPos(hit.point);

            if (typeof this.core.softBody.startGrabWithRadius === 'function') {
                this.core.softBody.startGrabWithRadius(
                    nearestVertPos[0], nearestVertPos[1], nearestVertPos[2],
                    this.core.grabRadius,
                    hit.point[0], hit.point[1], hit.point[2]
                );
            } else {
                this.core.softBody.startGrab(nearestVertPos[0], nearestVertPos[1], nearestVertPos[2]);
            }

            this.grabActive      = true;
            this.fixedDragActive = false;
            this.grabVertPos     = [...hit.point];
            this.grabPrevRay     = [...hit.point];
            this.grabSurfaceOffset = [
                hit.point[0] - nearestVertPos[0],
                hit.point[1] - nearestVertPos[1],
                hit.point[2] - nearestVertPos[2]
            ];
            this.grabDist  = hit.distance;
            this.grabTimer = performance.now();
            this.showTouchFeedback(this.lastTouch.x, this.lastTouch.y, 'Grabbed!');
            console.log('[TouchInput] Normal grab started');
        }
    }

    moveFixedDrag(x, y) {
        const ray = this.screenToWorldRay(x, y);
        if (!ray) return;
        
        const newPos = this.rayAtDist(ray, this.fixedDragDist);
        const delta = [
            newPos[0] - this.fixedDragPrevPos[0],
            newPos[1] - this.fixedDragPrevPos[1],
            newPos[2] - this.fixedDragPrevPos[2]
        ];
        
        // ★ 固定点のみ移動（直接positions操作、PC版と同じ）
        const positions = this.core.softBody.getPositions();  // typed_memory_view
        if (positions && this.core.fixedParticleIds && this.core.fixedParticleIds.length > 0) {
            for (const id of this.core.fixedParticleIds) {
                positions[id * 3 + 0] += delta[0];
                positions[id * 3 + 1] += delta[1];
                positions[id * 3 + 2] += delta[2];
            }
            console.log('[TouchInput] Fixed particles moved by delta:', delta);
        }
        
        this.fixedDragPrevPos = [...newPos];
    }

    moveMeshGrab(x, y) {
        if (!this.grabActive) return;
        const now = performance.now();
        const dt  = Math.max((now - this.grabTimer) / 1000, 1e-4);
        const ray = this.screenToWorldRay(x, y);
        if (!ray) return;
        
        const curRay = this.rayAtDist(ray, this.grabDist);
        const delta = [
            curRay[0] - this.grabPrevRay[0],
            curRay[1] - this.grabPrevRay[1],
            curRay[2] - this.grabPrevRay[2]
        ];
        
        const targetPos = [
            this.grabVertPos[0] + delta[0],
            this.grabVertPos[1] + delta[1],
            this.grabVertPos[2] + delta[2]
        ];
        
        const vx = delta[0] / dt, vy = delta[1] / dt, vz = delta[2] / dt;
        
        this.core.softBody.moveGrabbed(targetPos[0], targetPos[1], targetPos[2], vx, vy, vz);
        this.grabVertPos  = [...targetPos];
        this.grabPrevRay  = [...curRay];
        this.grabTimer    = now;
        
        // ★ グラブ中スフィア位置更新
        this.core.updateGrabSphere(targetPos, true);
    }

    endMeshGrab() {
        if (this.grabActive && this.core.softBody) {
            this.core.softBody.endGrab(this.grabVertPos[0], this.grabVertPos[1], this.grabVertPos[2], 0, 0, 0);
            this.grabActive = false;
            // ★ endGrab後も必ず固定点invMassを再設定
            this.core.restoreFixedInvMasses();
            // ★ グラブスフィア非表示
            this.core.updateGrabSphere([0,0,0], false);
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

    // ★ レイ交点に最も近いテトメッシュ（物理）頂点の現在位置を返す（PC版と同じ）
    _findNearestVertexPos(hitPoint) {
        const positions   = this.core.softBody.getPositions();
        const numParticles = this.core.softBody.getNumParticles();

        let nearestId = 0;
        let minD2 = Infinity;

        for (let i = 0; i < numParticles; i++) {
            const dx = positions[i*3]     - hitPoint[0];
            const dy = positions[i*3 + 1] - hitPoint[1];
            const dz = positions[i*3 + 2] - hitPoint[2];
            const d2 = dx*dx + dy*dy + dz*dz;
            if (d2 < minD2) { minD2 = d2; nearestId = i; }
        }

        return [
            positions[nearestId*3],
            positions[nearestId*3 + 1],
            positions[nearestId*3 + 2]
        ];
    }

    //=========================================================================
    // Sphere Control Panel（仮想ジョイスティック）
    //=========================================================================

    /** スフィアコントロール対象のスフィアを返す */
    _getCtrlSphere() {
        // グラブ中は特別処理
        if (this.grabActive) return { isGrabbing: true };
        // 固定スフィアが配置済みなら固定スフィア制御
        if (this.core.fixedSphere && this.core.fixedSphere.visible) return this.core.fixedSphere;
        return null;
    }

    /** コントロール可能なスフィアが存在するか */
    _hasSphere() {
        return !!this._getCtrlSphere();
    }

    /** パネル中心座標を返す（右下固定） */
    _getCtrlCenter() {
        const margin = this.CTRL_PANEL_MARGIN + this.CTRL_PANEL_R;
        return {
            x: window.innerWidth  - margin,
            y: window.innerHeight - margin
        };
    }

    /** 座標がコントロールパネル円内か判定 */
    _isInCtrlPanel(px, py) {
        if (!this._ctrlPanelEl || this._ctrlPanelEl.style.display === 'none') return false;
        const c = this._getCtrlCenter();
        const dx = px - c.x, dy = py - c.y;
        return (dx * dx + dy * dy) <= (this.CTRL_PANEL_R * this.CTRL_PANEL_R);
    }

    /** パネルDOMを生成（setupEventListenersから1回呼ばれる） */
    _buildSphereCtrlPanel() {
        // 外側リング（常時表示のベース）
        const panel = document.createElement('div');
        panel.id = 'sphere-ctrl-panel';
        const r = this.CTRL_PANEL_R;
        const margin = this.CTRL_PANEL_MARGIN;
        panel.style.cssText = `
            position: fixed;
            right: ${margin}px;
            bottom: ${margin}px;
            width: ${r * 2}px;
            height: ${r * 2}px;
            border-radius: 50%;
            background: rgba(255,255,255,0.08);
            border: 2px solid rgba(255,255,255,0.35);
            display: none;
            pointer-events: none;
            z-index: 500;
            box-sizing: border-box;
        `;

        // 中心ドット（常時表示）
        const dot = document.createElement('div');
        dot.id = 'sphere-ctrl-dot';
        dot.style.cssText = `
            position: absolute;
            width: 14px; height: 14px;
            border-radius: 50%;
            background: rgba(255,255,255,0.5);
            top: 50%; left: 50%;
            transform: translate(-50%, -50%);
        `;
        panel.appendChild(dot);

        // スティック（ドラッグ中に動くノブ）
        const stick = document.createElement('div');
        stick.id = 'sphere-ctrl-stick';
        stick.style.cssText = `
            position: absolute;
            width: 44px; height: 44px;
            border-radius: 50%;
            background: rgba(0,220,255,0.55);
            border: 2px solid rgba(0,220,255,0.9);
            top: 50%; left: 50%;
            transform: translate(-50%, -50%);
            display: none;
            pointer-events: none;
        `;
        panel.appendChild(stick);

        // ラベル
        const label = document.createElement('div');
        label.style.cssText = `
            position: absolute;
            bottom: -22px;
            left: 50%;
            transform: translateX(-50%);
            color: rgba(255,255,255,0.6);
            font-size: 11px;
            white-space: nowrap;
            pointer-events: none;
        `;
        label.textContent = 'Sphere';
        panel.appendChild(label);

        document.body.appendChild(panel);
        this._ctrlPanelEl  = panel;
        this._ctrlStickEl  = stick;
    }

    /** パネルを表示する */
    _showCtrlPanel() {
        if (this._ctrlPanelEl) this._ctrlPanelEl.style.display = 'block';
    }

    /** パネルを非表示にする */
    _hideCtrlPanel() {
        if (this._ctrlPanelEl) this._ctrlPanelEl.style.display = 'none';
        if (this._ctrlStickEl) this._ctrlStickEl.style.display = 'none';
    }

    /** パネル内タッチ開始 */
    _startSphereCtrl(touch) {
        const ctrlTarget = this._getCtrlSphere();
        if (!ctrlTarget) return;

        this.sphereCtrlActive  = true;
        this.sphereCtrlOrigin  = {x: touch.clientX, y: touch.clientY};
        this.sphereCtrlTouchId = touch.identifier;
        
        if (this.grabActive) {
            // ★ グラブ中：現在のグラブ位置を基準として記録 + 一時停止解除
            this.grabSuspended = false;
            this.sphereCtrlBasePos = [...this.grabVertPos];
            console.log('[TouchInput] Panel control - grab reactivated');
        } else if (this.core.fixedSphere && this.core.fixedSphere.visible) {
            // ★ 固定スフィア制御：現在のスフィア位置を基準として記録
            this.sphereCtrlBasePos = [...this.fixedSpherePos];
        }

        // スティックをタッチ点に表示
        if (this._ctrlStickEl) {
            const c = this._getCtrlCenter();
            const dx = Math.max(-this.CTRL_PANEL_R, Math.min(this.CTRL_PANEL_R,
                touch.clientX - c.x));
            const dy = Math.max(-this.CTRL_PANEL_R, Math.min(this.CTRL_PANEL_R,
                touch.clientY - c.y));
            this._ctrlStickEl.style.display = 'block';
            this._ctrlStickEl.style.transform =
                `translate(calc(-50% + ${dx}px), calc(-50% + ${dy}px))`;
        }

        console.log('[TouchInput] Panel control started - grab continues');
    }

    /** パネル内ドラッグ移動 */
    _moveSphereCtrl(cx, cy) {
        const dx = cx - this.sphereCtrlOrigin.x;
        const dy = cy - this.sphereCtrlOrigin.y;

        // カメラのRight・Upベクトルで3D移動量を計算
        const right = this.core.camera.getRightVector();
        const up    = this.core.camera.getUpVector();
        const s     = this.core.panelSensitivity;

        if (this.grabActive && !this.grabSuspended && this.core.softBody) {
            // ★ グラブ中（一時停止でない）：基準位置からの絶対移動（変形リセット）
            const targetPos = [
                this.sphereCtrlBasePos[0] + right[0] * dx * s - up[0] * dy * s,
                this.sphereCtrlBasePos[1] + right[1] * dx * s - up[1] * dy * s,
                this.sphereCtrlBasePos[2] + right[2] * dx * s - up[2] * dy * s
            ];

            this.core.softBody.moveGrabbed(targetPos[0], targetPos[1], targetPos[2], 0, 0, 0);
            this.grabVertPos = [...targetPos];
            this.core.updateGrabSphere(targetPos, true);
        } else if (this.core.fixedSphere && this.core.fixedSphere.visible) {
            // ★ 固定スフィア制御：固定部全体の平行移動
            const targetPos = [
                this.sphereCtrlBasePos[0] + right[0] * dx * s - up[0] * dy * s,
                this.sphereCtrlBasePos[1] + right[1] * dx * s - up[1] * dy * s,
                this.sphereCtrlBasePos[2] + right[2] * dx * s - up[2] * dy * s
            ];

            // ★ 固定部全体の平行移動
            const delta = [
                targetPos[0] - this.fixedSpherePos[0],
                targetPos[1] - this.fixedSpherePos[1],
                targetPos[2] - this.fixedSpherePos[2]
            ];

            // 全固定粒子を移動
            const positions = this.core.softBody.getPositions();
            if (positions && this.core.fixedParticleIds && this.core.fixedParticleIds.length > 0) {
                for (const id of this.core.fixedParticleIds) {
                    positions[id * 3 + 0] += delta[0];
                    positions[id * 3 + 1] += delta[1];
                    positions[id * 3 + 2] += delta[2];
                }
            }

            // スフィア表示位置も更新
            this.core.fixedSphere.setCenterXYZ(targetPos[0], targetPos[1], targetPos[2]);
            this.fixedSpherePos = [...targetPos];
        }

        // スティックUIを更新
        if (this._ctrlStickEl) {
            const c   = this._getCtrlCenter();
            const sdx = Math.max(-this.CTRL_PANEL_R, Math.min(this.CTRL_PANEL_R,
                cx - c.x));
            const sdy = Math.max(-this.CTRL_PANEL_R, Math.min(this.CTRL_PANEL_R,
                cy - c.y));
            this._ctrlStickEl.style.transform =
                `translate(calc(-50% + ${sdx}px), calc(-50% + ${sdy}px))`;
        }
    }

    /** パネル内ドラッグ終了 → グラブ・スフィア維持 */
    _endSphereCtrl() {
        this.sphereCtrlActive  = false;
        this.sphereCtrlTouchId = -1;

        // ★ グラブ・スフィアは解除せず維持（連続操作可能）
        console.log('[TouchInput] Panel control ended - grab and spheres maintained');

        // スティック非表示、パネルも非表示
        if (this._ctrlStickEl) this._ctrlStickEl.style.display = 'none';
        this._hideCtrlPanel();
    }

    /** グラブ継続時：スフィア位置を更新 */
    _placeGrabSphereAtPosition() {
        if (!this.core.grabSphere || !this.grabVertPos) return;

        // グラブした位置にスフィア表示を更新（グラブ継続中）
        this.core.updateGrabSphere(this.grabVertPos, true);

        console.log('[TouchInput] Grab sphere updated at:', this.grabVertPos);
    }

    /** 全スフィア解除（ダブルタップ用） */
    _releaseAllSpheres() {
        if (this.grabActive && this.core.softBody) {
            this.core.softBody.endGrab(
                this.grabVertPos[0], this.grabVertPos[1], this.grabVertPos[2], 0, 0, 0
            );
            this.core.restoreFixedInvMasses();
            this.grabActive = false;
        }
        // ★ 状態完全クリア
        this.grabSuspended = false;
        this.core.updateGrabSphere([0,0,0], false);
        this.core.updateFixedSphere([0,0,0], false);
        this._hideCtrlPanel();
        console.log('[TouchInput] All spheres released');
    }

    /** スフィア自体のタッチ判定（3D→2D投影で近接確認） */
    _checkSphereHit(screenX, screenY) {
        const SPHERE_TOUCH_RADIUS = 60; // タッチ判定範囲（px）
        
        // グラブスフィア判定
        if (this.grabActive && this.core.grabSphere && this.core.grabSphere.visible) {
            const grabPos = [
                this.core.grabSphere.getCenterX(),
                this.core.grabSphere.getCenterY(), 
                this.core.grabSphere.getCenterZ()
            ];
            if (this._isNearSphere(screenX, screenY, grabPos, SPHERE_TOUCH_RADIUS)) {
                return { type: 'grab' };
            }
        }
        
        // 固定スフィア判定
        if (this.core.fixedSphere && this.core.fixedSphere.visible) {
            const fixedPos = [
                this.core.fixedSphere.getCenterX(),
                this.core.fixedSphere.getCenterY(),
                this.core.fixedSphere.getCenterZ()
            ];
            if (this._isNearSphere(screenX, screenY, fixedPos, SPHERE_TOUCH_RADIUS)) {
                return { type: 'fixed' };
            }
        }
        
        return null;
    }

    /** 3D球体位置がスクリーン座標に近いか判定 */
    _isNearSphere(screenX, screenY, worldPos, radius) {
        // 簡易判定：カメラからの視線方向と球体位置の近さで判定
        const ray = this.screenToWorldRay(screenX, screenY);
        if (!ray) return false;
        
        // レイと球体中心の距離で判定
        const toSphere = [
            worldPos[0] - ray.origin[0],
            worldPos[1] - ray.origin[1], 
            worldPos[2] - ray.origin[2]
        ];
        
        const dot = toSphere[0]*ray.direction[0] + toSphere[1]*ray.direction[1] + toSphere[2]*ray.direction[2];
        if (dot < 0) return false; // 後ろ
        
        const closest = [
            ray.origin[0] + ray.direction[0] * dot,
            ray.origin[1] + ray.direction[1] * dot,
            ray.origin[2] + ray.direction[2] * dot
        ];
        
        const dist = Math.sqrt(
            (worldPos[0] - closest[0])**2 +
            (worldPos[1] - closest[1])**2 +
            (worldPos[2] - closest[2])**2
        );
        
        return dist < (this.core.grabRadius * 2); // スフィア半径の2倍以内
    }
}

//=========================================================================
// Export
//=========================================================================
if (typeof module !== 'undefined') {
    module.exports = TouchInputHandler;
}