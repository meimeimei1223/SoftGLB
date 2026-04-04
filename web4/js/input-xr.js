// input-xr.js - WebXR VR/AR input handling with hand tracking
// Web3 SoftBody Physics - XR input module

//=========================================================================
// XR Input Handler Class
//=========================================================================
class XRInputHandler {
    constructor(core) {
        this.core = core;
        
        // XR session references
        this.xrSession = null;
        this.xrRefSpace = null;
        this.xrFrame = null;
        
        // Hand tracking state
        this.leftHand = null;
        this.rightHand = null;
        this.leftHandSphere = null;   // 左手用スフィア（緑・タッチ）
        this.rightHandSphere = null;  // 右手用スフィア（青・ピンチ）
        
        // Gesture state
        this.leftTouchActive = false;
        this.rightPinchActive = false;
        this.pinchDistance = 0;
        this.grabActive = false;
        this.grabStartPos = [0, 0, 0];
        
        // Platform-specific settings
        const config = getPlatformConfig('xr');
        this.handSphereRadius = 0.03;  // 手のスフィア半径
        this.pinchThreshold = 0.05;    // ピンチ判定閾値
        
        console.log('[XRInputHandler] Initialized for VR/AR');
    }

    //=========================================================================
    // Event Listener Setup
    //=========================================================================
    setupEventListeners() {
        // XRセッション取得
        if (window.xrSession) {
            this.xrSession = window.xrSession;
            this.xrRefSpace = window.xrRefSpace;
            
            // XRセッションにWebGL層を設定
            const gl = this.core.gl;
            this.xrSession.updateRenderState({
                baseLayer: new XRWebGLLayer(this.xrSession, gl)
            });
        }
        
        // Hand sphere colliders初期化
        this.initHandSpheres();
        
        console.log('[XRInputHandler] XR event listeners setup complete');
    }

    //=========================================================================
    // Hand Tracking Initialization
    //=========================================================================
    async initHandSpheres() {
        if (typeof this.core.Module.SphereColliderPhysics === 'undefined') return;

        try {
            // 左手スフィア（緑・タッチ用）
            const res = await fetch('model/ioSphere.obj');
            if (!res.ok) return;
            const objText = await res.text();

            this.leftHandSphere = new this.core.Module.SphereColliderPhysics();
            if (!this.leftHandSphere.initFromOBJString(objText)) {
                this.leftHandSphere.delete();
                this.leftHandSphere = null;
                return;
            }
            
            this.leftHandSphere.setRadiusScale(this.handSphereRadius);
            this.leftHandSphere.setCenterXYZ(0, 0, 0);
            this.leftHandSphere.visible = true;
            
            // 右手スフィア（青・ピンチ用）
            this.rightHandSphere = new this.core.Module.SphereColliderPhysics();
            if (!this.rightHandSphere.initFromOBJString(objText)) {
                this.rightHandSphere.delete();
                this.rightHandSphere = null;
                return;
            }
            
            this.rightHandSphere.setRadiusScale(this.handSphereRadius);
            this.rightHandSphere.setCenterXYZ(0, 0, 0);
            this.rightHandSphere.visible = true;
            
            // 物理シミュレーションに追加
            if (this.core.softBody && this.core.Module) {
                this.core.Module.addSphereCollider(this.core.softBody, this.leftHandSphere);
                this.core.Module.addSphereCollider(this.core.softBody, this.rightHandSphere);
            }
            
            console.log('[XRInput] Hand spheres initialized');
        } catch(e) {
            console.warn('[XRInput] Hand sphere creation failed:', e.message);
        }
    }

    //=========================================================================
    // XR Frame Processing
    //=========================================================================
    onXRFrame(time, frame) {
        this.xrFrame = frame;
        
        if (!this.xrSession || !this.xrRefSpace) return;
        
        // Hand tracking update
        this.updateHandTracking(frame);
        
        // Gesture detection
        this.detectGestures();
        
        // Physics interaction
        this.updatePhysicsInteraction();
    }

    //=========================================================================
    // Hand Tracking Processing  
    //=========================================================================
    updateHandTracking(frame) {
        // Get hand input sources
        for (const inputSource of this.xrSession.inputSources) {
            if (inputSource.hand) {
                if (inputSource.handedness === 'left') {
                    this.leftHand = inputSource.hand;
                    this.updateHandSphere(this.leftHand, this.leftHandSphere, 'left');
                } else if (inputSource.handedness === 'right') {
                    this.rightHand = inputSource.hand;
                    this.updateHandSphere(this.rightHand, this.rightHandSphere, 'right');
                }
            }
        }
    }

    updateHandSphere(hand, sphere, side) {
        if (!hand || !sphere || !this.xrFrame) return;

        // 手のひら中心位置（wrist基準）
        const wristJoint = hand.get('wrist');
        if (!wristJoint) return;

        const pose = this.xrFrame.getJointPose(wristJoint, this.xrRefSpace);
        if (!pose) return;

        // スフィア位置更新
        const pos = pose.transform.position;
        sphere.setCenterXYZ(pos.x, pos.y, pos.z);
        
        console.log(`[XRInput] ${side} hand sphere at:`, pos.x.toFixed(3), pos.y.toFixed(3), pos.z.toFixed(3));
    }

    //=========================================================================
    // Gesture Detection
    //=========================================================================
    detectGestures() {
        this.detectLeftHandTouch();
        this.detectRightHandPinch();
    }

    detectLeftHandTouch() {
        if (!this.leftHand || !this.leftHandSphere) return;

        // 簡易タッチ検出：手のひらとメッシュの衝突
        const touching = this.leftHandSphere.isColliding && this.leftHandSphere.isColliding();
        
        if (touching && !this.leftTouchActive) {
            // タッチ開始
            this.leftTouchActive = true;
            this.startMeshGrab('left');
            console.log('[XRInput] Left hand touch started');
        } else if (!touching && this.leftTouchActive) {
            // タッチ終了
            this.leftTouchActive = false;
            this.endMeshGrab();
            console.log('[XRInput] Left hand touch ended');
        }
    }

    detectRightHandPinch() {
        if (!this.rightHand) return;

        // ピンチ検出：親指先端と人差指先端の距離
        const thumbTip = this.rightHand.get('thumb-tip');
        const indexTip = this.rightHand.get('index-finger-tip');
        
        if (!thumbTip || !indexTip || !this.xrFrame) return;

        const thumbPose = this.xrFrame.getJointPose(thumbTip, this.xrRefSpace);
        const indexPose = this.xrFrame.getJointPose(indexTip, this.xrRefSpace);
        
        if (!thumbPose || !indexPose) return;

        const distance = this.calculateDistance(thumbPose.transform.position, indexPose.transform.position);
        
        const isPinching = distance < this.pinchThreshold;
        
        if (isPinching && !this.rightPinchActive) {
            // ピンチ開始
            this.rightPinchActive = true;
            this.pinchDistance = distance;
            this.startMeshGrab('right');
            console.log('[XRInput] Right hand pinch started');
        } else if (!isPinching && this.rightPinchActive) {
            // ピンチ終了
            this.rightPinchActive = false;
            this.endMeshGrab();
            console.log('[XRInput] Right hand pinch ended');
        }
    }

    //=========================================================================
    // Physics Interaction
    //=========================================================================
    startMeshGrab(hand) {
        this.core.restoreFixedInvMasses();
        
        const handSphere = hand === 'left' ? this.leftHandSphere : this.rightHandSphere;
        if (!handSphere) return;

        const pos = [
            handSphere.getCenterX(),
            handSphere.getCenterY(), 
            handSphere.getCenterZ()
        ];

        // グラブ開始
        if (typeof this.core.softBody.startGrabWithRadius === 'function') {
            this.core.softBody.startGrabWithRadius(
                pos[0], pos[1], pos[2],
                this.core.grabRadius,
                pos[0], pos[1], pos[2]
            );
        } else {
            this.core.softBody.startGrab(pos[0], pos[1], pos[2]);
        }

        this.grabActive = true;
        this.grabStartPos = [...pos];
        
        // グラブスフィア表示
        this.core.updateGrabSphere(pos, true);
        
        console.log('[XRInput] XR grab started at:', pos);
    }

    endMeshGrab() {
        if (!this.grabActive || !this.core.softBody) return;

        this.core.softBody.endGrab(
            this.grabStartPos[0], this.grabStartPos[1], this.grabStartPos[2], 0, 0, 0
        );
        this.core.restoreFixedInvMasses();
        this.grabActive = false;
        
        // グラブスフィア非表示
        this.core.updateGrabSphere([0,0,0], false);
        
        console.log('[XRInput] XR grab ended');
    }

    updatePhysicsInteraction() {
        if (!this.grabActive || !this.core.softBody) return;

        let targetPos = null;
        
        if (this.leftTouchActive && this.leftHandSphere) {
            // 左手タッチモード
            targetPos = [
                this.leftHandSphere.getCenterX(),
                this.leftHandSphere.getCenterY(),
                this.leftHandSphere.getCenterZ()
            ];
        } else if (this.rightPinchActive && this.rightHandSphere) {
            // 右手ピンチモード  
            targetPos = [
                this.rightHandSphere.getCenterX(),
                this.rightHandSphere.getCenterY(),
                this.rightHandSphere.getCenterZ()
            ];
        }

        if (targetPos) {
            this.core.softBody.moveGrabbed(targetPos[0], targetPos[1], targetPos[2], 0, 0, 0);
            this.core.updateGrabSphere(targetPos, true);
        }
    }

    //=========================================================================
    // Utility Functions
    //=========================================================================
    calculateDistance(pos1, pos2) {
        const dx = pos1.x - pos2.x;
        const dy = pos1.y - pos2.y;
        const dz = pos1.z - pos2.z;
        return Math.sqrt(dx*dx + dy*dy + dz*dz);
    }

    // Resize handler (XR doesn't need this, but interface consistency)
    onResize() {
        // XR handles its own viewport
    }
}

//=========================================================================
// Export
//=========================================================================
if (typeof module !== 'undefined') {
    module.exports = XRInputHandler;
}