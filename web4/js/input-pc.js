// input-pc.js - PC Mouse/Keyboard input handling
// Web3 SoftBody Physics - PC input module

//=========================================================================
// PC Input Handler Class
//=========================================================================
class PCInputHandler {
    constructor(core) {
        this.core = core;
        this.canvas = core.canvas;
        
        // Input state
        this.isDragging = false;
        this.lastMouse = {x: 0, y: 0};
        
        // Grab state  
        this.grabActive = false;
        this.grabDist = 0;
        this.grabPrevPos = [0,0,0];
        this.grabPrevTime = 0;
        
        // Sphere drag state
        this.sphereDragging = false;
        this.sphere2Dragging = false;
        this.sphereGrabDist = 0;
        this.sphere2GrabDist = 0;
        
        // Fixed point thresholds
        this.fixedThreshold = -999;      // テトメッシュ用（invMass設定）
        this.ungrabbableThreshold = -999; // テトメッシュ用（invMass設定）
        this.visFixedThreshold = -999;   // ★ ビジュアルメッシュ用（raycast判定）
        this.visUngrabbableThreshold = -999; // ★ ビジュアルメッシュ用（raycast判定）
        
        // ★ FIXED_DRAG state
        this.fixedDragActive = false;
        this.fixedDragDist   = 0;
        this.fixedDragPrevPos = [0, 0, 0];
        this.fixedParticleIds = [];   // core.jsから受け取る
        
        // ★ ビクつき修正用（デルタ加算方式）
        this.grabVertPos = [0, 0, 0];  // grabId頂点の追跡位置（デルタ加算の基点）
        this.grabSurfaceOffset = [0, 0, 0];
        this.grabPrevRay = [0, 0, 0];
        
        console.log('[PCInputHandler] Initialized for desktop PC');
    }

    //=========================================================================
    // Event Listener Setup
    //=========================================================================
    setupEventListeners() {
        // Mouse events
        this.canvas.addEventListener('contextmenu', e => e.preventDefault());
        this.canvas.addEventListener('mousedown', this.onMouseDown.bind(this));
        this.canvas.addEventListener('mousemove', this.onMouseMove.bind(this));
        this.canvas.addEventListener('mouseup', this.onMouseUp.bind(this));
        this.canvas.addEventListener('wheel', this.onWheel.bind(this), {passive: false});
        
        // Keyboard events
        document.addEventListener('keydown', this.onKeyDown.bind(this));
        
        // Window events
        window.addEventListener('resize', this.onResize.bind(this));
        
        console.log('[PCInputHandler] Event listeners attached');
    }

    //=========================================================================
    // Mouse Event Handlers
    //=========================================================================
    onMouseDown(e) {
        this.lastMouse = {x: e.clientX, y: e.clientY};

        // Right click → Shooting
        if (e.button === 2 && this.core.softBody) {
            this.handleRightClick(e.clientX, e.clientY);
            e.preventDefault();
            return;
        }

        // Middle click → Sphere selection and drag
        if (e.button === 1 && (this.core.sphereCollider || this.core.sphereCollider2)) {
            this.handleMiddleClick(e.clientX, e.clientY);
            e.preventDefault();
            return;
        }

        // Left click → Mesh grab or camera rotation
        if (e.button === 0 && this.core.softBody) {
            const hit = this.raycastMesh(e.clientX, e.clientY);
            if (hit) {
                this.startMeshGrab(hit);
                e.preventDefault();
                return;
            }
            // No mesh hit → camera rotation
        }

        if (e.button === 0) this.isDragging = true;
    }

    onMouseMove(e) {
        const dx = e.clientX - this.lastMouse.x;
        const dy = e.clientY - this.lastMouse.y;

        // Sphere dragging (first sphere)
        if (this.sphereDragging && this.core.sphereCollider) {
            this.moveSphere(this.core.sphereCollider, this.sphereGrabDist, e.clientX, e.clientY);
            this.lastMouse = {x: e.clientX, y: e.clientY};
            return;
        }
        
        // Sphere dragging (second sphere)
        if (this.sphere2Dragging && this.core.sphereCollider2) {
            this.moveSphere(this.core.sphereCollider2, this.sphere2GrabDist, e.clientX, e.clientY);
            this.lastMouse = {x: e.clientX, y: e.clientY};
            return;
        }

        // ★ FIXED_DRAG
        if (this.fixedDragActive && this.core.softBody) {
            this.moveFixedDrag(e.clientX, e.clientY);
            return;
        }

        // 通常グラブ
        if (this.grabActive && this.core.softBody) {
            this.moveMeshGrab(e.clientX, e.clientY);
            return;
        }

        // Show 3D grab sphere when hovering over mesh
        if (!this.grabActive && !this.fixedDragActive && this.core.softBody) {
            const hit = this.raycastMesh(e.clientX, e.clientY);
            if (hit && !hit.isFixed) {  // ★ 固定域では表示しない
                this.core.updateGrabSphere(hit.point, true);
            } else {
                this.core.updateGrabSphere([0,0,0], false);
            }
        }

        // Camera rotation
        if (this.isDragging) {
            this.core.camera.yaw += dx * 0.3;
            this.core.camera.pitch = Math.max(-89, Math.min(89, this.core.camera.pitch + dy * 0.3));
        }

        this.lastMouse = {x: e.clientX, y: e.clientY};
    }

    onMouseUp(e) {
        if (e.button === 1) {
            // Middle click release
            if (this.sphereDragging && this.core.sphereCollider) {
                this.core.sphereCollider.endDrag();
                this.sphereDragging = false;
                console.log('[PCInput] Sphere 1 drag ended');
            }
            if (this.sphere2Dragging && this.core.sphereCollider2) {
                this.core.sphereCollider2.endDrag();
                this.sphere2Dragging = false;
                console.log('[PCInput] Sphere 2 drag ended');
            }
            return;
        }
        
        if (e.button === 0) {
            if (this.fixedDragActive) {
                // ★ FIXED_DRAG終了：固定点invMassを必ず再設定
                this.fixedDragActive = false;
                this.core.restoreFixedInvMasses();
                console.log('[PCInput] FIXED_DRAG ended, invMasses restored');
            }
            if (this.grabActive && this.core.softBody) {
                // ★ endGrab には grabVertPos（頂点追跡位置）を渡す
                this.core.softBody.endGrab(this.grabVertPos[0], this.grabVertPos[1], this.grabVertPos[2], 0, 0, 0);
                this.grabActive = false;
                // ★ endGrab後も必ず固定点を再設定（内部でinvMassが書き換えられるため）
                this.core.restoreFixedInvMasses();
                // ★ グラブスフィア非表示
                this.core.updateGrabSphere([0,0,0], false);
            }
            this.isDragging = false;
            this.canvas.style.cursor = 'default';
        }
    }

    onWheel(e) {
        this.core.camera.radius = Math.max(1, Math.min(30, this.core.camera.radius + e.deltaY * 0.01));
        e.preventDefault();
    }

    onResize() {
        this.core.resize();
    }

    //=========================================================================
    // Keyboard Event Handler
    //=========================================================================
    onKeyDown(e) {
        if (e.key === 's' || e.key === 'S') {
            this.core.showWireframe = !this.core.showWireframe;
            console.log('[PCInput] Wireframe:', this.core.showWireframe);
            e.preventDefault();
        }
        else if (e.key === 'r' || e.key === 'R') {
            if (this.core.softBody) this.core.softBody.applyShapeRestoration(0.05);
            e.preventDefault();
        }
        else if ((e.key === 'v' || e.key === 'V') && this.core.sphereCollider) {
            this.core.sphereVisible = !this.core.sphereVisible;
            this.core.sphereCollider.visible = this.core.sphereVisible;
            console.log('[PCInput] Spheres visible:', this.core.sphereVisible);
        }
        else if ((e.key === 'n' || e.key === 'N') && this.core.sphereCollider) {
            this.core.sphereCollider.changeRadiusScale(0.01);
        }
        else if ((e.key === 'm' || e.key === 'M') && this.core.sphereCollider) {
            this.core.sphereCollider.changeRadiusScale(-0.01);
        }
        else if (e.key === 'w' || e.key === 'W') {
            this.core.addSecondSphere();
        }
        else if (e.key === 'c' || e.key === 'C') {
            this.core.clearAllSpheres();
        }
        else if (e.key === 'p' || e.key === 'P') {
            this.togglePhysicsPanel();
        }
        else if (e.key === 'f' || e.key === 'F') {
            this.core.performanceMode = !this.core.performanceMode;
            this.core.physicsSubsteps = this.core.performanceMode ? 5 : 10;
            console.log('[PCInput] Performance mode:', this.core.performanceMode);
        }
        else if (e.key === 'q' || e.key === 'Q') {
            this.core.takeScreenshot();
            e.preventDefault();
        }
        else if (e.key === ',' || e.key === '<') {
            this.core.grabRadius = Math.max(0.03, this.core.grabRadius - 0.02);
            // ★ UIスライダーも同期
            const grabSizeSlider = document.getElementById('grabSizeSlider');
            const grabSizeValue = document.getElementById('grabSizeValue');
            if (grabSizeSlider) grabSizeSlider.value = this.core.grabRadius;
            if (grabSizeValue) grabSizeValue.textContent = this.core.grabRadius.toFixed(2);
            console.log('[PCInput] Grab size decreased:', this.core.grabRadius.toFixed(3));
        }
        else if (e.key === '.' || e.key === '>') {
            this.core.grabRadius = Math.min(0.8, this.core.grabRadius + 0.02);
            // ★ UIスライダーも同期
            const grabSizeSlider = document.getElementById('grabSizeSlider');
            const grabSizeValue = document.getElementById('grabSizeValue');
            if (grabSizeSlider) grabSizeSlider.value = this.core.grabRadius;
            if (grabSizeValue) grabSizeValue.textContent = this.core.grabRadius.toFixed(2);
            console.log('[PCInput] Grab size increased:', this.core.grabRadius.toFixed(3));
        }
    }

    //=========================================================================
    // Raycast and Collision (PC-specific precise implementation)
    //=========================================================================
    screenToWorldRay(sx, sy) {
        const rect = this.canvas.getBoundingClientRect();
        const x = ((sx - rect.left) / rect.width) * 2 - 1;
        const y = 1 - ((sy - rect.top) / rect.height) * 2;
        
        // Use core's matrix functions
        const viewMat = this.core.updateViewMatrix();
        const projMat = this.core.updateProjMatrix();
        
        // Create view-projection matrix and invert
        const vpMat = new Float32Array(16);
        this.multiplyMatrices(vpMat, projMat, viewMat);
        const invVP = this.invertMatrix(vpMat);
        
        if (!invVP) return null;
        
        // Transform points
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

    raycastMesh(screenX, screenY) {
        if (!this.core.softBody) return null;
        
        const ray = this.screenToWorldRay(screenX, screenY);
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
            const fullyFixed    = (maxY <= this.visFixedThreshold);       // 完全固定域
            const touchesBoundary = (minY <= this.visUngrabbableThreshold && maxY > this.visFixedThreshold); // 境界のみ

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

    // Sphere-ray intersection for precise sphere selection
    raySphereIntersect(ray, center, radius) {
        const oc = SoftBodyCore.sub(ray.origin, center);
        const a = SoftBodyCore.dot(ray.direction, ray.direction);
        const b = 2.0 * SoftBodyCore.dot(oc, ray.direction);
        const c = SoftBodyCore.dot(oc, oc) - radius * radius;
        
        const discriminant = b * b - 4 * a * c;
        if (discriminant < 0) return null;
        
        const t1 = (-b - Math.sqrt(discriminant)) / (2 * a);
        const t2 = (-b + Math.sqrt(discriminant)) / (2 * a);
        
        const t = (t1 > 0) ? t1 : t2;
        if (t <= 0) return null;
        
        return {
            distance: t,
            point: [
                ray.origin[0] + ray.direction[0] * t,
                ray.origin[1] + ray.direction[1] * t,
                ray.origin[2] + ray.direction[2] * t
            ]
        };
    }

    // Matrix helper functions
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
        
        out[0]=(m11*b11-m12*b10+m13*b09)*det; out[1]=(m02*b10-m01*b11-m03*b09)*det;
        out[2]=(m31*b05-m32*b04+m33*b03)*det; out[3]=(m22*b04-m21*b05-m23*b03)*det;
        out[4]=(m12*b08-m10*b11-m13*b07)*det; out[5]=(m00*b11-m02*b08+m03*b07)*det;
        out[6]=(m32*b02-m30*b05-m33*b01)*det; out[7]=(m20*b05-m22*b02+m23*b01)*det;
        out[8]=(m10*b10-m11*b08+m13*b06)*det; out[9]=(m01*b08-m00*b10-m03*b06)*det;
        out[10]=(m30*b04-m31*b02+m33*b00)*det; out[11]=(m21*b02-m20*b04-m23*b00)*det;
        out[12]=(m11*b07-m10*b09-m12*b06)*det; out[13]=(m00*b09-m01*b07+m02*b06)*det;
        out[14]=(m31*b01-m30*b03-m32*b00)*det; out[15]=(m20*b03-m21*b01+m22*b00)*det;
        
        return out;
    }
    
    //=========================================================================
    // Helper Functions
    //=========================================================================
    togglePhysicsPanel() {
        const panel = document.getElementById('compliance-panel');
        if (panel) {
            const isVisible = panel.style.display !== 'none' && panel.style.display !== '';
            panel.style.display = isVisible ? 'none' : 'block';
            console.log('[PCInput] Physics panel:', isVisible ? 'hidden' : 'shown');
        }
    }

    //=========================================================================
    // Physics Operations (detailed implementation from index.html)
    //=========================================================================
    handleRightClick(x, y) {
        // Shooting system
        const ray = this.screenToWorldRay(x, y);
        if (!ray) return;
        
        const target = this.rayAtDist(ray, this.core.camera.radius * 1.5);
        
        if (!this.core.isShootActive && this.core.shootSphere) {
            this.core.fireShootSphere(target[0], target[1], target[2]);
        } else if (!this.core.shootSphere) {
            console.warn('[PCInput] Shoot sphere not ready');
        }
    }

    handleMiddleClick(x, y) {
        // Precise sphere selection with raycast
        const ray = this.screenToWorldRay(x, y);
        if (!ray) return;
        
        let bestSphere = null;
        let bestDist = Infinity;
        let bestType = null;
        
        // Test sphere 1
        if (this.core.sphereCollider) {
            const cx = this.core.sphereCollider.getCenterX();
            const cy = this.core.sphereCollider.getCenterY();
            const cz = this.core.sphereCollider.getCenterZ();
            const r = this.core.sphereCollider.getRadius();
            
            const hit = this.raySphereIntersect(ray, [cx, cy, cz], r);
            if (hit && hit.distance < bestDist) {
                bestSphere = this.core.sphereCollider;
                bestDist = hit.distance;
                bestType = 1;
                this.sphereGrabDist = Math.sqrt((cx-ray.origin[0])**2 + (cy-ray.origin[1])**2 + (cz-ray.origin[2])**2);
            }
        }
        
        // Test sphere 2  
        if (this.core.sphereCollider2) {
            const cx = this.core.sphereCollider2.getCenterX();
            const cy = this.core.sphereCollider2.getCenterY();
            const cz = this.core.sphereCollider2.getCenterZ();
            const r = this.core.sphereCollider2.getRadius();
            
            const hit = this.raySphereIntersect(ray, [cx, cy, cz], r);
            if (hit && hit.distance < bestDist) {
                bestSphere = this.core.sphereCollider2;
                bestDist = hit.distance;
                bestType = 2;
                this.sphere2GrabDist = Math.sqrt((cx-ray.origin[0])**2 + (cy-ray.origin[1])**2 + (cz-ray.origin[2])**2);
            }
        }
        
        // Start dragging closest sphere
        if (bestSphere) {
            const center = [bestSphere.getCenterX(), bestSphere.getCenterY(), bestSphere.getCenterZ()];
            bestSphere.startDragAt(center[0], center[1], center[2], 
                                 bestType === 1 ? this.sphereGrabDist : this.sphere2GrabDist);
            
            if (bestType === 1) {
                this.sphereDragging = true;
                console.log('[PCInput] Dragging sphere 1 (blue) at distance:', bestDist.toFixed(2));
            } else {
                this.sphere2Dragging = true;
                console.log('[PCInput] Dragging sphere 2 (orange) at distance:', bestDist.toFixed(2));
            }
        }
    }

    startMeshGrab(hit) {
        this.core.restoreFixedInvMasses();

        if (hit.isFixed) {
            // ★ FIXED_DRAG開始前：既存グラブを完全終了（頂点固定防止）
            if (this.grabActive && this.core.softBody) {
                this.core.softBody.endGrab(
                    this.grabVertPos[0], this.grabVertPos[1], this.grabVertPos[2], 0, 0, 0
                );
                this.grabActive = false;
                console.log('[PCInput] Previous grab ended before FIXED_DRAG');
            }
            
            // ★ 完全固定域 → FIXED_DRAG（平行移動、startGrabを呼ばない）
            this.fixedDragActive  = true;
            this.fixedDragDist    = hit.distance;
            this.fixedDragPrevPos = [...hit.point];
            this.grabActive       = false;
            this.canvas.style.cursor = 'grabbing';
            // ★ grabSphere完全非表示・リセット
            this.core.updateGrabSphere([0,0,0], false);
            console.log('[PCInput] FIXED_DRAG started (parallel translation)');
        } else {
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
            this.grabDist     = hit.distance;
            this.grabPrevTime = performance.now();
            this.canvas.style.cursor = 'grabbing';
            console.log('[PCInput] Normal grab started');
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
        
        // ★ 固定点のみ移動（直接positions操作、WASMバイパス）
        const positions = this.core.softBody.getPositions();  // typed_memory_view
        if (positions && this.core.fixedParticleIds && this.core.fixedParticleIds.length > 0) {
            for (const id of this.core.fixedParticleIds) {
                positions[id * 3 + 0] += delta[0];
                positions[id * 3 + 1] += delta[1];
                positions[id * 3 + 2] += delta[2];
            }
            console.log('[PCInput] Fixed particles moved by delta:', delta);
        }
        
        this.fixedDragPrevPos = [...newPos];
    }

    moveMeshGrab(x, y) {
        if (!this.grabActive) return;
        const now = performance.now();
        const dt  = Math.max((now - this.grabPrevTime) / 1000, 1e-4);
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
        this.grabPrevTime = now;
        
        // ★ グラブ中スフィア位置更新
        this.core.updateGrabSphere(targetPos, true);
    }

    moveSphere(sphere, grabDist, x, y) {
        const ray = this.screenToWorldRay(x, y);
        if (!ray) return;
        
        const wp = this.rayAtDist(ray, grabDist);
        sphere.moveDragTo(wp[0], wp[1], wp[2], 1/60);
    }

    // ★ レイ交点に最も近いテトメッシュ（物理）頂点の現在位置を返す
    _findNearestVertexPos(hitPoint) {
        const positions  = this.core.softBody.getPositions();
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
    // Fixed Point Management (anatomical accuracy)
    //=========================================================================
    updateFixedThresholds(fixedThreshold, ungrabbableThreshold, fixedParticleIds = [],
                          visFixedThreshold = -999, visUngrabbableThreshold = -999) {
        this.fixedThreshold           = fixedThreshold;
        this.ungrabbableThreshold     = ungrabbableThreshold;
        this.fixedParticleIds         = fixedParticleIds;
        this.visFixedThreshold        = visFixedThreshold;        // ★ Visメッシュ用
        this.visUngrabbableThreshold  = visUngrabbableThreshold;  // ★ Visメッシュ用
        
        console.log('[PCInput] Thresholds updated:',
            'tet fixed=', fixedThreshold.toFixed(3),
            'vis fixed=', visFixedThreshold.toFixed(3),
            'vis ungrab=', visUngrabbableThreshold.toFixed(3),
            'fixedIds=', fixedParticleIds.length);
    }
}

//=========================================================================
// Export
//=========================================================================
if (typeof module !== 'undefined') {
    module.exports = PCInputHandler;
}