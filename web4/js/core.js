// core.js - Main rendering and physics engine
// Web3 SoftBody Physics - Core module (platform-independent)

//=========================================================================
// Core WebGL and Physics Management
//=========================================================================
class SoftBodyCore {
    constructor(canvas, platform = 'pc') {
        this.canvas = canvas;
        this.platform = platform;
        this.gl = null;
        this.softBody = null;
        this.sphereCollider = null;
        this.sphereCollider2 = null;
        this.shootSphere = null;
        this.grabSphere = null;  // ★ グラブ用スフィア
        
        // Shader programs
        this.program = null;
        this.wireProgram = null;
        
        // Buffers
        this.buffers = {};
        
        // Rendering state
        this.showWireframe = false;
        this.sphereVisible = true;
        this.isShootActive = false;
        this.glbTexture = null;
        this.numIndices = 0;
        this.numTetEdgeVerts = 0;
        this.numSphereLineVerts = 0;
        
        // Physics parameters  
        this.physicsSubsteps = 10;
        this.performanceMode = false;
        
        // Shot system
        this.shootSpeed = 10.0;
        this.shootRadius = 0.3;
        this.recoveryTime = 1.0;
        
        // Cached locations (performance)
        this.uloc = {};
        this.aloc = {};
        
        // Pre-allocated matrices
        this._viewMat = new Float32Array(16);
        this._projMat = new Float32Array(16);
        this._modelMat = new Float32Array([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]);
        this._normalMat = new Float32Array(9);
        this._sphereMat = new Float32Array(16);
        
        // ★ C++のFullSphereCameraと同じカメラシステム
        this._camPos = new Float32Array(3);
        const config = getPlatformConfig(platform);
        this.camera = {
            radius: config.cameraRadius,
            yaw: 0, 
            pitch: 15, 
            fov: 45,
            target: [0, 0, 0],
            getPosition: () => {
                const p = this.camera.pitch * Math.PI / 180;
                const y = this.camera.yaw * Math.PI / 180;
                this._camPos[0] = this.camera.radius * Math.cos(p) * Math.cos(y);
                this._camPos[1] = this.camera.radius * Math.sin(p);
                this._camPos[2] = this.camera.radius * Math.cos(p) * Math.sin(y);
                return this._camPos;
            },
            // ★ C++版と同じcameraRightベクトル計算
            getRightVector: () => {
                const forward = SoftBodyCore.normalize(SoftBodyCore.sub(this.camera.target, this.camera.getPosition()));
                const right = SoftBodyCore.normalize(SoftBodyCore.cross(forward, [0, 1, 0]));
                return right;
            },
            getUpVector: () => {
                const forward = SoftBodyCore.normalize(SoftBodyCore.sub(this.camera.target, this.camera.getPosition()));
                const right = SoftBodyCore.normalize(SoftBodyCore.cross(forward, [0, 1, 0]));
                const up = SoftBodyCore.cross(right, forward);
                return up;
            }
        };
    }

    //=========================================================================
    // Matrix and Vector Utilities (migrated from index.html)
    //=========================================================================
    static sub(a,b) { return [a[0]-b[0],a[1]-b[1],a[2]-b[2]]; }
    static cross(a,b) { return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]; }
    static dot(a,b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
    static normalize(v) { 
        const l = Math.sqrt(SoftBodyCore.dot(v,v)); 
        return l > 0 ? [v[0]/l, v[1]/l, v[2]/l] : [0,0,0]; 
    }

    // Matrix operations (zero-allocation versions)
    static perspectiveInto(out, fov, aspect, near, far) {
        const f = 1.0 / Math.tan(fov / 2), nf = 1 / (near - far);
        out[0]=f/aspect; out[1]=0;  out[2]=0;              out[3]=0;
        out[4]=0;        out[5]=f;  out[6]=0;              out[7]=0;
        out[8]=0;        out[9]=0;  out[10]=(far+near)*nf; out[11]=-1;
        out[12]=0;       out[13]=0; out[14]=2*far*near*nf; out[15]=0;
    }
    
    static lookAtInto(out, eye, center, up) {
        const z = SoftBodyCore.normalize(SoftBodyCore.sub(eye, center));
        const x = SoftBodyCore.normalize(SoftBodyCore.cross(up, z));
        const y = SoftBodyCore.cross(z, x);
        out[0]=x[0]; out[1]=y[0]; out[2]=z[0];  out[3]=0;
        out[4]=x[1]; out[5]=y[1]; out[6]=z[1];  out[7]=0;
        out[8]=x[2]; out[9]=y[2]; out[10]=z[2]; out[11]=0;
        out[12]=-SoftBodyCore.dot(x,eye); out[13]=-SoftBodyCore.dot(y,eye); out[14]=-SoftBodyCore.dot(z,eye); out[15]=1;
    }
    
    static multiplyInto(out, a, b) {
        for (let i = 0; i < 4; i++) {
            for (let j = 0; j < 4; j++) {
                out[i*4 + j] = a[j]*b[i*4] + a[4+j]*b[i*4+1] + a[8+j]*b[i*4+2] + a[12+j]*b[i*4+3];
            }
        }
    }

    //=========================================================================
    // Initialization
    //=========================================================================
    async initialize(Module) {
        this.Module = Module;
        
        // WebGL context with screenshot support
        this.gl = this.canvas.getContext('webgl', {preserveDrawingBuffer: true}) || 
                  this.canvas.getContext('experimental-webgl', {preserveDrawingBuffer: true});
        
        if (!this.gl) {
            throw new Error('WebGL not supported');
        }
        
        // Extensions
        this.gl.getExtension('OES_element_index_uint');
        
        // GL state
        this.gl.enable(this.gl.DEPTH_TEST);
        this.gl.enable(this.gl.BLEND);
        this.gl.blendFunc(this.gl.SRC_ALPHA, this.gl.ONE_MINUS_SRC_ALPHA);
        this.gl.clearColor(0.22, 0.22, 0.22, 1.0); // ★ C++版と同じグレー背景
        
        this.resize();
        this.setupShaders();
        this.setupBuffers();
        this.cacheShaderLocations();
        this.setupPhysicsPanel();  // ★ Physics Panel復活（パラメータ反映に必要）
        this.showPhysicsPanel();   // ★ デフォルト表示
        
        
        console.log('[SoftBodyCore] Initialized for platform:', this.platform);
    }
    
    setupShaders() {
        // Same shaders as main index.html (vertex with normal matrix, fragment with C++ lighting)
        const vsSource = `
            attribute vec3 aPosition;
            attribute vec3 aNormal;
            attribute vec2 aTexCoord;
            uniform mat4 uModel, uView, uProjection;
            uniform mat3 uNormalMatrix;
            varying vec3 vNormal;
            varying vec3 vFragPos;
            varying vec2 vTexCoord;
            void main() {
                vec4 wp = uModel * vec4(aPosition, 1.0);
                vFragPos  = wp.xyz;
                vNormal   = normalize(uNormalMatrix * aNormal);
                vTexCoord = aTexCoord;
                gl_Position = uProjection * uView * wp;
            }
        `;
        
        const fsSource = `
            precision mediump float;
            varying vec3 vNormal;
            varying vec3 vFragPos;
            varying vec2 vTexCoord;
            uniform vec3 uLightPos, uLightColor, uViewPos;
            uniform vec4 uColor;
            uniform sampler2D uSampler;
            uniform bool uHasTexture;
            void main() {
                vec3 N = normalize(vNormal);
                vec3 L = normalize(uLightPos - vFragPos);
                vec3 V = normalize(uViewPos  - vFragPos);
                vec3 H = normalize(L + V);

                // ★ C++版と同じ改良されたライティング係数
                float ambientFactor  = 0.80;  // 強いアンビエントで均一な明るさ
                float specularFactor = 0.03;  // テカリ防止（0.8から激減）
                float shininess      = 5.0;   // 広くソフトなハイライト

                vec3 ambient  = uLightColor * ambientFactor;
                vec3 diffuse  = uLightColor * max(dot(N, L), 0.0) * 0.25; // コントラスト低減
                vec3 specular = uLightColor * specularFactor * pow(max(dot(N, H), 0.0), shininess);

                vec3 lighting = ambient + diffuse + specular;
                lighting = min(lighting, vec3(1.0)); // オーバーブライト防止

                vec3 baseColor = uHasTexture
                    ? texture2D(uSampler, vTexCoord).rgb
                    : uColor.rgb;
                float alpha = uHasTexture
                    ? texture2D(uSampler, vTexCoord).a
                    : uColor.a;

                gl_FragColor = vec4(lighting, 1.0) * vec4(baseColor, alpha);
            }
        `;
        
        const wireVS = `
            attribute vec3 aPosition;
            uniform mat4 uModel, uView, uProjection;
            void main() { gl_Position = uProjection * uView * uModel * vec4(aPosition,1.0); }
        `;
        
        const wireFS = `
            precision mediump float;
            uniform vec4 uColor;
            void main() { gl_FragColor = uColor; }
        `;
        
        this.program = this.createShaderProgram(vsSource, fsSource);
        this.wireProgram = this.createShaderProgram(wireVS, wireFS);
    }

    createShaderProgram(vertexSource, fragmentSource) {
        const compile = (type, source) => {
            const shader = this.gl.createShader(type);
            this.gl.shaderSource(shader, source);
            this.gl.compileShader(shader);
            if (!this.gl.getShaderParameter(shader, this.gl.COMPILE_STATUS)) {
                console.error('Shader compile error:', this.gl.getShaderInfoLog(shader));
            }
            return shader;
        };
        
        const vs = compile(this.gl.VERTEX_SHADER, vertexSource);
        const fs = compile(this.gl.FRAGMENT_SHADER, fragmentSource);
        
        const program = this.gl.createProgram();
        this.gl.attachShader(program, vs);
        this.gl.attachShader(program, fs);
        this.gl.linkProgram(program);
        
        if (!this.gl.getProgramParameter(program, this.gl.LINK_STATUS)) {
            console.error('Shader link error:', this.gl.getProgramInfoLog(program));
        }
        
        return program;
    }
    
    cacheShaderLocations() {
        this.uloc = {
            model:        this.gl.getUniformLocation(this.program, 'uModel'),
            view:         this.gl.getUniformLocation(this.program, 'uView'), 
            projection:   this.gl.getUniformLocation(this.program, 'uProjection'),
            normalMatrix: this.gl.getUniformLocation(this.program, 'uNormalMatrix'),
            lightPos:     this.gl.getUniformLocation(this.program, 'uLightPos'),
            lightColor:   this.gl.getUniformLocation(this.program, 'uLightColor'),
            color:        this.gl.getUniformLocation(this.program, 'uColor'),
            viewPos:      this.gl.getUniformLocation(this.program, 'uViewPos'),
            hasTexture:   this.gl.getUniformLocation(this.program, 'uHasTexture'),
            sampler:      this.gl.getUniformLocation(this.program, 'uSampler'),
            wModel:       this.gl.getUniformLocation(this.wireProgram, 'uModel'),
            wView:        this.gl.getUniformLocation(this.wireProgram, 'uView'),
            wProjection:  this.gl.getUniformLocation(this.wireProgram, 'uProjection'),
            wColor:       this.gl.getUniformLocation(this.wireProgram, 'uColor'),
        };
        
        this.aloc = {
            position:  this.gl.getAttribLocation(this.program, 'aPosition'),
            normal:    this.gl.getAttribLocation(this.program, 'aNormal'),
            texCoord:  this.gl.getAttribLocation(this.program, 'aTexCoord'),
            wPosition: this.gl.getAttribLocation(this.wireProgram, 'aPosition'),
        };
        
        console.log('[SoftBodyCore] Shader locations cached for max performance');
    }

    setupBuffers() {
        const gl = this.gl;
        this.buffers = {
            visPos:     gl.createBuffer(),
            visNormal:  gl.createBuffer(), 
            visUV:      gl.createBuffer(),
            visIndex:   gl.createBuffer(),
            tetEdge:    gl.createBuffer(),
            sphereLine: gl.createBuffer()
        };
    }
    
    resize() {
        this.canvas.width = window.innerWidth;
        this.canvas.height = window.innerHeight; 
        this.gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    }

    //=========================================================================
    // Platform-agnostic core functions (will be called by input modules)
    //=========================================================================
    
    // Matrix calculations (migrated from index.html)
    updateViewMatrix() {
        SoftBodyCore.lookAtInto(this._viewMat, this.camera.getPosition(), [0,0,0], [0,1,0]);
        return this._viewMat;
    }
    
    updateProjMatrix() {
        SoftBodyCore.perspectiveInto(this._projMat, this.camera.fov*Math.PI/180, 
                                    this.canvas.width/this.canvas.height, 0.1, 100);
        return this._projMat;
    }
    
    computeNormalMatrix() {
        // Identity matrix for unit model matrix
        this._normalMat[0] = 1; this._normalMat[1] = 0; this._normalMat[2] = 0;
        this._normalMat[3] = 0; this._normalMat[4] = 1; this._normalMat[5] = 0;
        this._normalMat[6] = 0; this._normalMat[7] = 0; this._normalMat[8] = 1;
        return this._normalMat;
    }

    //=========================================================================
    // Buffer Management (migrated from index.html)
    //=========================================================================
    setupMeshBuffers() {
        if (!this.softBody) return;

        const pos = this.softBody.getVisPositions();
        const nrm = this.softBody.getVisNormals();
        const idx = this.softBody.getVisIndices();
        this.numIndices = idx.length;

        console.log('[Core] Setting up optimized buffers. Vertices:', pos.length/3);
        
        const gl = this.gl;
        
        gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers.visPos);
        gl.bufferData(gl.ARRAY_BUFFER, pos.byteLength, gl.STREAM_DRAW);
        gl.bufferSubData(gl.ARRAY_BUFFER, 0, pos);

        gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers.visNormal);
        gl.bufferData(gl.ARRAY_BUFFER, nrm.byteLength, gl.STREAM_DRAW);
        gl.bufferSubData(gl.ARRAY_BUFFER, 0, nrm);

        // UV buffer
        if (typeof this.softBody.getVisUVs === 'function') {
            const uvs = this.softBody.getVisUVs();
            if (uvs && uvs.length > 0) {
                gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers.visUV);
                gl.bufferData(gl.ARRAY_BUFFER, uvs, gl.STATIC_DRAW);
            }
        }

        // Index buffer
        const idxArr = new Uint32Array(idx.length);
        for (let i = 0; i < idx.length; i++) idxArr[i] = idx[i];
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.buffers.visIndex);
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idxArr, gl.STATIC_DRAW);

        // Tet edge buffer (for wireframe)
        gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers.tetEdge);
        gl.bufferData(gl.ARRAY_BUFFER, 1000000, gl.STREAM_DRAW); // Pre-allocate
        this.numTetEdgeVerts = 0;
        
        console.log('[Core] Optimized buffers ready for high-performance updates');
    }

    updateMeshBuffers() {
        if (!this.softBody) return;
        
        const gl = this.gl;
        
        // Position update (zero-copy)
        const pos = this.softBody.getVisPositions();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers.visPos);
        gl.bufferSubData(gl.ARRAY_BUFFER, 0, pos);

        // Normal update (zero-copy)
        const nrm = this.softBody.getVisNormals();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers.visNormal);
        gl.bufferSubData(gl.ARRAY_BUFFER, 0, nrm);

        // Tet edge update (wireframe only)
        if (this.showWireframe) {
            const te = this.softBody.getTetEdgeVertices();
            this.numTetEdgeVerts = te.length / 3;
            gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers.tetEdge);
            gl.bufferSubData(gl.ARRAY_BUFFER, 0, te);
        }
    }

    //=========================================================================
    // GLB Loading System (migrated from index.html)
    //=========================================================================
    async loadGLB(arrayBuffer) {
        try {
            if (!arrayBuffer) {
                throw new Error('ArrayBuffer is null');
            }
            
            console.log('[Core] Starting GLB load, size:', arrayBuffer.byteLength);

            if (this.softBody) { 
                this.softBody.delete(); 
                this.softBody = null; 
            }

            // Uint8Array conversion for C++
            const uint8 = new Uint8Array(arrayBuffer);
            
            console.log('[Core] Passing Uint8Array to C++. size=', uint8.length,
                'magic=', uint8[0].toString(16), uint8[1].toString(16),
                uint8[2].toString(16), uint8[3].toString(16));

            // Call C++ tetrahedralization engine (5 parameters required)
            this.softBody = this.Module.createSoftBodyFromGlbBytes(uint8, 2.0, 20, 1.0, 0.0);

            if (!this.softBody || this.softBody.getNumParticles() === 0) {
                throw new Error('C++ createSoftBodyFromGlbBytes failed');
            }

                    // Apply bottom particle fixing (anatomical accuracy)
            this.fixBottomParticles();
            
            // Update input handler with fixed thresholds (for raycast exclusion)
            if (window.inputHandler && typeof window.inputHandler.updateFixedThresholds === 'function') {
                window.inputHandler.updateFixedThresholds(
                    this.fixedThreshold,
                    this.ungrabbableThreshold,
                    this.fixedParticleIds,      // ★ 固定点IDリストも渡す
                    this.visFixedThreshold,     // ★ Visメッシュ用閾値
                    this.visUngrabbableThreshold // ★ Visメッシュ用閾値
                );
            }

            // Upload texture from WASM
            this.uploadTextureFromWasm();

            // Setup rendering buffers
            this.setupMeshBuffers();

            // Update UI
            this.updateUI();

            // Initialize sphere colliders
            await this.initSphereCollider();
            await this.createGrabSphere();  // ★ グラブスフィア初期化
            this.reattachExistingSpheres();

            console.log('[Core] GLB loaded successfully:', this.softBody.getNumParticles(), 'particles');

        } catch(error) {
            console.error('[Core] GLB loading error:', error);
            throw error;
        }
    }

    // Parameter-customizable GLB loading
    async loadGLBWithParams(arrayBuffer, targetSize, gridSize) {
        try {
            console.log('[Core] Loading GLB with custom params - Grid:', gridSize, 'Target:', targetSize);
            console.log('[Core] ArrayBuffer size:', arrayBuffer.byteLength, 'Module available:', !!this.Module);

            if (this.softBody) { 
                console.log('[Core] Cleaning up previous softBody...');
                this.softBody.delete(); 
                this.softBody = null; 
            }

            const uint8 = new Uint8Array(arrayBuffer);
            console.log('[Core] Uint8Array created, size:', uint8.length, 'first bytes:', 
                       uint8[0].toString(16), uint8[1].toString(16), uint8[2].toString(16), uint8[3].toString(16));
            
            console.log('[Core] Calling createSoftBodyFromGlbBytes with params:', targetSize, gridSize, 1.0, 0.0);
            
            // Call C++ with custom parameters (5 parameters required: targetSize, gridSize, edge, volume)
            this.softBody = this.Module.createSoftBodyFromGlbBytes(uint8, targetSize, gridSize, 1.0, 0.0);
            
            console.log('[Core] createSoftBodyFromGlbBytes returned:', !!this.softBody);

            if (!this.softBody || this.softBody.getNumParticles() === 0) {
                throw new Error('C++ createSoftBodyFromGlbBytes failed');
            }

            this.fixBottomParticles();
            
            if (window.inputHandler && typeof window.inputHandler.updateFixedThresholds === 'function') {
                window.inputHandler.updateFixedThresholds(
                    this.fixedThreshold,
                    this.ungrabbableThreshold,
                    this.fixedParticleIds,      // ★ 固定点IDリストも渡す
                    this.visFixedThreshold,     // ★ Visメッシュ用閾値
                    this.visUngrabbableThreshold // ★ Visメッシュ用閾値
                );
            }

            this.uploadTextureFromWasm();
            this.setupMeshBuffers();
            this.updateUI();

            await this.initSphereCollider();
            await this.createGrabSphere();  // ★ グラブスフィア初期化
            this.reattachExistingSpheres();

            console.log(`[Core] Custom GLB loaded: ${this.softBody.getNumParticles()} particles with Grid ${gridSize}`);

        } catch(error) {
            console.error('[Core] Custom GLB loading error:', error);
            throw error;
        }
    }

    // Anatomical bottom fixing (migrated from index.html)
    fixBottomParticles() {
        if (!this.softBody) return;
        
        const positions = this.softBody.getPositions();
        const numParticles = this.softBody.getNumParticles();
        
        // Collect Y coordinates and sort
        const ys = [];
        for (let i = 0; i < numParticles; i++) {
            ys.push(positions[i * 3 + 1]);
        }
        const sortedYs = [...ys].sort((a, b) => a - b);
        
        // Two-tier threshold system
        const fixedThresholdVal = sortedYs[Math.floor(numParticles / 4)];      // Bottom 1/4 → Fixed
        const ungrabbableThresholdVal = sortedYs[Math.floor(numParticles / 3)]; // Bottom 1/3 → Ungrabbable
        
        this.fixedThreshold = fixedThresholdVal;       // invMass 設定に使用
        this.ungrabbableThreshold = ungrabbableThresholdVal; // invMass 設定に使用

        // ★ ビジュアルメッシュ（28249頂点）用の閾値を別途計算
        //    raycastMesh() は getVisPositions() を使うのでこちらで判定する
        const visPositions  = this.softBody.getVisPositions();
        const numVisVerts   = this.softBody.getNumVisVerts();
        const visYs = [];
        for (let i = 0; i < numVisVerts; i++) {
            visYs.push(visPositions[i * 3 + 1]);
        }
        const sortedVisYs = [...visYs].sort((a, b) => a - b);

        // テトメッシュと同じ比率（下位1/4 固定、下位1/3 アングラブ）で計算
        this.visFixedThreshold       = sortedVisYs[Math.floor(numVisVerts / 4)];
        this.visUngrabbableThreshold = sortedVisYs[Math.floor(numVisVerts / 3)];

        console.log(`[Core] Tet  thresholds: fixed=${fixedThresholdVal.toFixed(3)} ungrab=${ungrabbableThresholdVal.toFixed(3)}`);
        console.log(`[Core] Vis  thresholds: fixed=${this.visFixedThreshold.toFixed(3)} ungrab=${this.visUngrabbableThreshold.toFixed(3)}`);

        // ★ 固定点IDを永続保存（startGrab/endGrabでinvMassが壊れても復元できる）
        this.fixedParticleIds = [];
        
        // Set invMass to 0 for bottom 1/4 particles (physical fixing)
        let fixedCount = 0;
        for (let i = 0; i < numParticles; i++) {
            if (ys[i] <= fixedThresholdVal) {
                this.softBody.setInvMass(i, 0.0);
                this.fixedParticleIds.push(i);   // ★ IDを記録
                fixedCount++;
            }
        }
        
        console.log(`[Core] Fixed ${fixedCount}/${numParticles} particles (bottom 1/4, Y <= ${fixedThresholdVal.toFixed(3)})`);
        console.log(`[Core] Ungrabbable region: bottom 1/3 (Y <= ${ungrabbableThresholdVal.toFixed(3)})`);
        console.log(`[Core] Fixed particle IDs saved:`, this.fixedParticleIds.length);
    }

    // ★ 固定点のinvMassを必ず0に再設定（startGrab/endGrabで壊れた後の復元用）
    restoreFixedInvMasses() {
        if (!this.softBody || !this.fixedParticleIds) return;
        for (const id of this.fixedParticleIds) {
            this.softBody.setInvMass(id, 0.0);
        }
        console.log('[Core] Fixed invMasses restored for', this.fixedParticleIds.length, 'particles');
    }

    // ★ Grab properties
    grabRadius = 0.15;
    grabSphereVisible = true;  // ★ グラブスフィア表示ON/OFF

    // UI update helper
    updateUI() {
        const particlesEl = document.getElementById('particles');
        const tetsEl = document.getElementById('tets');
        
        if (particlesEl) particlesEl.textContent = this.softBody.getNumParticles();
        if (tetsEl) tetsEl.textContent = this.softBody.getNumTets();
        
        console.log('[Core] UI updated with mesh statistics');
    }

    // Texture management (migrated from index.html)
    uploadTextureFromWasm() {
        if (!this.Module.getLastHasTexture || !this.Module.getLastHasTexture()) {
            this.glbTexture = null;
            return;
        }
        
        const w = this.Module.getLastTextureWidth();
        const h = this.Module.getLastTextureHeight(); 
        const ch = this.Module.getLastTextureChannels();
        const pixels = this.Module.getLastTexturePixels();

        if (!pixels || w <= 0 || h <= 0) { 
            this.glbTexture = null; 
            return; 
        }

        // Copy WASM memory view (required before GC)
        const pixelsCopy = new Uint8Array(pixels);

        if (this.glbTexture) { 
            this.gl.deleteTexture(this.glbTexture); 
        }
        
        this.glbTexture = this.gl.createTexture();
        this.gl.bindTexture(this.gl.TEXTURE_2D, this.glbTexture);

        const fmt = (ch === 4) ? this.gl.RGBA : this.gl.RGB;
        this.gl.texImage2D(this.gl.TEXTURE_2D, 0, fmt, w, h, 0, fmt, this.gl.UNSIGNED_BYTE, pixelsCopy);

        // High-quality filtering settings
        this.gl.generateMipmap(this.gl.TEXTURE_2D);
        this.gl.texParameteri(this.gl.TEXTURE_2D, this.gl.TEXTURE_MIN_FILTER, this.gl.LINEAR_MIPMAP_LINEAR);
        this.gl.texParameteri(this.gl.TEXTURE_2D, this.gl.TEXTURE_MAG_FILTER, this.gl.LINEAR);
        this.gl.texParameteri(this.gl.TEXTURE_2D, this.gl.TEXTURE_WRAP_S, this.gl.REPEAT);
        this.gl.texParameteri(this.gl.TEXTURE_2D, this.gl.TEXTURE_WRAP_T, this.gl.REPEAT);
        
        // Anisotropic filtering if available
        const ext = this.gl.getExtension('EXT_texture_filter_anisotropic');
        if (ext) {
            const maxAnisotropy = this.gl.getParameter(ext.MAX_TEXTURE_MAX_ANISOTROPY_EXT);
            this.gl.texParameterf(this.gl.TEXTURE_2D, ext.TEXTURE_MAX_ANISOTROPY_EXT, Math.min(16, maxAnisotropy));
            console.log('[Core] Anisotropic filtering enabled:', Math.min(16, maxAnisotropy));
        }
        
        this.gl.bindTexture(this.gl.TEXTURE_2D, null);

        console.log('[Core] High-quality texture uploaded:', w, 'x', h, 'ch=', ch);
    }
    
    //=========================================================================
    // Rendering Pipeline (migrated from index.html)
    //=========================================================================
    startRenderLoop() {
        const FIXED_DT = 1/60;  // Fixed timestep
        let fpsLast = performance.now();
        let fpsCount = 0;

        const renderFrame = () => {
            requestAnimationFrame(renderFrame);
            
            if (!this.softBody) return;

            // Sphere physics updates
            if (this.sphereCollider) this.sphereCollider.update(FIXED_DT);
            if (this.sphereCollider2) this.sphereCollider2.update(FIXED_DT);
            if (this.shootSphere && this.isShootActive) this.shootSphere.update(FIXED_DT);

            // ★ 毎フレーム固定点invMass=0を再保証（startGrab内部で書き換えられても正しく戻す）
            this.restoreFixedInvMasses();

            // Main physics step
            const currentSubsteps = this.performanceMode ? 5 : this.physicsSubsteps;
            this.softBody.step(FIXED_DT, currentSubsteps);

            // Buffer updates
            this.updateMeshBuffers();

            // Rendering
            this.gl.clear(this.gl.COLOR_BUFFER_BIT | this.gl.DEPTH_BUFFER_BIT);
            this.drawMesh();
            this.drawSpheres();

            // FPS calculation
            fpsCount++;
            const now = performance.now();
            if (now - fpsLast >= 1000) {
                const fpsElement = document.getElementById('fps');
                if (fpsElement) fpsElement.textContent = fpsCount;
                fpsCount = 0;
                fpsLast = now;
            }

        };

        renderFrame();
        console.log('[Core] Render loop started');
    }
    
    drawMesh() {
        const view = this.updateViewMatrix();
        const proj = this.updateProjMatrix();
        const camPos = this.camera.getPosition();
        const gl = this.gl;


        if (!this.showWireframe) {
            gl.useProgram(this.program);
            
            // Set uniforms with cached locations
            gl.uniformMatrix4fv(this.uloc.model, false, this._modelMat);
            gl.uniformMatrix4fv(this.uloc.view, false, view);
            gl.uniformMatrix4fv(this.uloc.projection, false, proj);
            gl.uniformMatrix3fv(this.uloc.normalMatrix, false, this.computeNormalMatrix());
            gl.uniform3fv(this.uloc.lightPos, camPos);
            gl.uniform3f(this.uloc.lightColor, 0.9, 0.88, 0.85); // ★ C++版と同じ暖色ライト
            gl.uniform4f(this.uloc.color, 0.8, 0.25, 0.25, 1);
            gl.uniform3fv(this.uloc.viewPos, camPos);

            // Texture handling
            const hasTexture = (this.glbTexture !== null);
            gl.uniform1i(this.uloc.hasTexture, hasTexture ? 1 : 0);
            if (hasTexture) {
                gl.activeTexture(gl.TEXTURE0);
                gl.bindTexture(gl.TEXTURE_2D, this.glbTexture);
                gl.uniform1i(this.uloc.sampler, 0);
            }

            // Vertex attributes
            this.bindAttribute(this.aloc.position, this.buffers.visPos, 3);
            this.bindAttribute(this.aloc.normal, this.buffers.visNormal, 3);
            if (hasTexture && this.aloc.texCoord >= 0) {
                this.bindAttribute(this.aloc.texCoord, this.buffers.visUV, 2);
            }

            gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.buffers.visIndex);
            gl.drawElements(gl.TRIANGLES, this.numIndices, gl.UNSIGNED_INT, 0);
        } else {
            // Wireframe rendering
            gl.useProgram(this.wireProgram);
            gl.uniformMatrix4fv(this.uloc.wModel, false, this._modelMat);
            gl.uniformMatrix4fv(this.uloc.wView, false, view);
            gl.uniformMatrix4fv(this.uloc.wProjection, false, proj);
            gl.uniform4f(this.uloc.wColor, 0, 1, 0.5, 1);

            this.bindAttribute(this.aloc.wPosition, this.buffers.tetEdge, 3);
            gl.drawArrays(this.gl.LINES, 0, this.numTetEdgeVerts);
        }
    }
    
    bindAttribute(location, buffer, size) {
        if (location < 0) return;
        const gl = this.gl;
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        gl.enableVertexAttribArray(location);
        gl.vertexAttribPointer(location, size, gl.FLOAT, false, 0, 0);
    }

    drawSpheres() {
        if (!this.sphereVisible || this.numSphereLineVerts === 0) return;

        const view = this._viewMat;  // Already updated in drawMesh
        const proj = this._projMat;  // Already updated in drawMesh
        const gl = this.gl;

        gl.useProgram(this.wireProgram);
        gl.uniformMatrix4fv(this.uloc.wView, false, view);
        gl.uniformMatrix4fv(this.uloc.wProjection, false, proj);

        this.bindAttribute(this.aloc.wPosition, this.buffers.sphereLine, 3);

        // Draw sphere 1 (blue)
        if (this.sphereCollider) {
            this.drawSingleSphere(this.sphereCollider, [0.3, 0.8, 1.0, 0.9]);
        }

        // Draw sphere 2 (orange)  
        if (this.sphereCollider2) {
            this.drawSingleSphere(this.sphereCollider2, [1.0, 0.5, 0.0, 0.9]);
        }
        
        // Draw shot sphere (purple)
        if (this.shootSphere && this.shootSphere.visible && this.isShootActive) {
            this.drawSingleSphere(this.shootSphere, [0.8, 0.2, 1.0, 1.0]);
        }
        
        // ★ Draw grab sphere (green)
        if (this.grabSphere && this.grabSphere.visible && this.grabSphereVisible) {
            this.drawSingleSphere(this.grabSphere, [0.2, 1.0, 0.3, 0.7]);
        }
    }

    drawSingleSphere(sphere, color) {
        const cx = sphere.getCenterX();
        const cy = sphere.getCenterY();
        const cz = sphere.getCenterZ();
        const r = sphere.getRadius();

        // Set sphere model matrix
        this._sphereMat[0]=r; this._sphereMat[1]=0; this._sphereMat[2]=0; this._sphereMat[3]=0;
        this._sphereMat[4]=0; this._sphereMat[5]=r; this._sphereMat[6]=0; this._sphereMat[7]=0;
        this._sphereMat[8]=0; this._sphereMat[9]=0; this._sphereMat[10]=r; this._sphereMat[11]=0;
        this._sphereMat[12]=cx; this._sphereMat[13]=cy; this._sphereMat[14]=cz; this._sphereMat[15]=1;

        this.gl.uniformMatrix4fv(this.uloc.wModel, false, this._sphereMat);
        this.gl.uniform4f(this.uloc.wColor, color[0], color[1], color[2], color[3]);
        this.gl.drawArrays(this.gl.LINES, 0, this.numSphereLineVerts);
    }

    //=========================================================================
    // Sphere Management System (migrated from index.html)
    //=========================================================================
    async initSphereCollider() {
        if (this.sphereCollider) { 
            this.sphereCollider.delete(); 
            this.sphereCollider = null; 
        }

        if (typeof this.Module.SphereColliderPhysics === 'undefined') return;

        try {
            const res = await fetch('model/ioSphere.obj');
            if (!res.ok) return;
            const objText = await res.text();

            this.sphereCollider = new this.Module.SphereColliderPhysics();
            if (!this.sphereCollider.initFromOBJString(objText)) {
                this.sphereCollider.delete(); 
                this.sphereCollider = null; 
                return;
            }
            
            this.sphereCollider.setRadiusScale(0.2);
            this.sphereCollider.setCenterXYZ(0, 2, 0);

            if (this.softBody) {
                this.Module.addSphereCollider(this.softBody, this.sphereCollider);
            }

            // Update UI
            const statusEl = document.getElementById('sphereStatus');
            if (statusEl) statusEl.textContent = 'visible';
            
            this.setupSphereLineBuffer();
            
            console.log('[Core] Primary sphere collider initialized');
        } catch(e) {
            console.warn('[Core] Sphere collider init failed:', e.message);
        }
    }

    async addSecondSphere() {
        if (!this.softBody) {
            console.warn('[Core] SoftBody not loaded yet');
            return;
        }
        
        if (this.sphereCollider2) {
            console.log('[Core] Second sphere already exists');
            return;
        }
        
        try {
            const res = await fetch('model/ioSphere.obj');
            if (!res.ok) {
                console.warn('[Core] ioSphere.obj not found');
                return;
            }
            const objText = await res.text();

            this.sphereCollider2 = new this.Module.SphereColliderPhysics();
            if (!this.sphereCollider2.initFromOBJString(objText)) {
                this.sphereCollider2.delete(); 
                this.sphereCollider2 = null; 
                console.warn('[Core] Second sphere init failed');
                return;
            }
            
            // Different position and size from first sphere
            this.sphereCollider2.setRadiusScale(0.15);  // Smaller
            this.sphereCollider2.setCenterXYZ(-2, 1.5, 0); // Left side
            
            // Add to physics
            this.Module.addSphereCollider(this.softBody, this.sphereCollider2);
            
            console.log('[Core] Second sphere added at (-2, 1.5, 0)');
        } catch(e) {
            console.warn('[Core] Second sphere creation failed:', e.message);
        }
    }
    
    clearAllSpheres() {
        if (!this.softBody) return;
        
        this.Module.clearSphereColliders(this.softBody);
        
        if (this.sphereCollider) { 
            this.sphereCollider.delete(); 
            this.sphereCollider = null; 
        }
        if (this.sphereCollider2) { 
            this.sphereCollider2.delete(); 
            this.sphereCollider2 = null; 
        }
        if (this.shootSphere) {
            this.shootSphere.delete();
            this.shootSphere = null;
        }
        
        // Update UI
        const statusEl = document.getElementById('sphereStatus');
        if (statusEl) statusEl.textContent = 'cleared';
        
        this.isShootActive = false;
        console.log('[Core] All spheres cleared');
    }

    reattachExistingSpheres() {
        if (!this.softBody) return;
        
        let reattachedCount = 0;
        
        // Clear all first, then re-register (clean state)
        this.Module.clearSphereColliders(this.softBody);
        
        if (this.sphereCollider) {
            this.Module.addSphereCollider(this.softBody, this.sphereCollider);
            reattachedCount++;
            console.log('[Core] Sphere 1 (blue) reattached with collision');
        }
        
        if (this.sphereCollider2) {
            this.Module.addSphereCollider(this.softBody, this.sphereCollider2);
            reattachedCount++;
            console.log('[Core] Sphere 2 (orange) reattached with collision');
        }
        
        if (this.shootSphere && this.isShootActive) {
            this.Module.addSphereCollider(this.softBody, this.shootSphere);
            reattachedCount++;
            console.log('[Core] Shoot sphere (purple) reattached');
        }
        
        if (reattachedCount > 0) {
            console.log(`[Core] ${reattachedCount} sphere(s) reattached to new softBody`);
        }
    }

    async createGrabSphere() {
        if (typeof this.Module.SphereColliderPhysics === 'undefined') return;

        try {
            const res = await fetch('model/ioSphere.obj');
            if (!res.ok) return;
            const objText = await res.text();

            this.grabSphere = new this.Module.SphereColliderPhysics();
            if (!this.grabSphere.initFromOBJString(objText)) {
                this.grabSphere.delete(); 
                this.grabSphere = null; 
                return;
            }
            
            // 初期設定：半透明の緑色、grabRadiusと同じサイズ
            this.grabSphere.setRadiusScale(this.grabRadius);
            this.grabSphere.setCenterXYZ(0, 0, 0);  // 初期位置
            this.grabSphere.visible = false;  // 最初は非表示
            
            console.log('[Core] Grab sphere initialized');
        } catch(e) {
            console.warn('[Core] Grab sphere creation failed:', e.message);
        }
    }

    updateGrabSphere(position, visible) {
        if (!this.grabSphere) return;
        
        // グラブスフィア表示設定を反映
        this.grabSphere.visible = visible && this.grabSphereVisible;
        if (visible && this.grabSphereVisible) {
            this.grabSphere.setCenterXYZ(position[0], position[1], position[2]);
            this.grabSphere.setRadiusScale(this.grabRadius);  // グラブ範囲と同じサイズ
        }
    }

    setupSphereLineBuffer() {
        // UV sphere wireframe generation (migrated from index.html)
        const verts = [];
        const stacks = 12, slices = 16;
        
        // Horizontal circles
        for (let i = 1; i < stacks; i++) {
            const phi = Math.PI * i / stacks;
            for (let j = 0; j < slices; j++) {
                const t0 = 2 * Math.PI * j / slices;
                const t1 = 2 * Math.PI * (j + 1) / slices;
                verts.push(
                    Math.sin(phi) * Math.cos(t0), Math.cos(phi), Math.sin(phi) * Math.sin(t0),
                    Math.sin(phi) * Math.cos(t1), Math.cos(phi), Math.sin(phi) * Math.sin(t1)
                );
            }
        }
        
        // Vertical circles  
        for (let j = 0; j < slices; j++) {
            const th = 2 * Math.PI * j / slices;
            for (let i = 0; i < stacks; i++) {
                const p0 = Math.PI * i / stacks;
                const p1 = Math.PI * (i + 1) / stacks;
                verts.push(
                    Math.sin(p0) * Math.cos(th), Math.cos(p0), Math.sin(p0) * Math.sin(th),
                    Math.sin(p1) * Math.cos(th), Math.cos(p1), Math.sin(p1) * Math.sin(th)
                );
            }
        }
        
        this.numSphereLineVerts = verts.length / 3;
        this.gl.bindBuffer(this.gl.ARRAY_BUFFER, this.buffers.sphereLine);
        this.gl.bufferData(this.gl.ARRAY_BUFFER, new Float32Array(verts), this.gl.STATIC_DRAW);
        
        console.log('[Core] Sphere wireframe buffer created:', this.numSphereLineVerts, 'vertices');
    }
    
    //=========================================================================
    // Physics Parameter Panel (migrated from index.html)
    //=========================================================================
    setupPhysicsPanel() {
        const edgeSlider = document.getElementById('edgeSlider');
        const volSlider = document.getElementById('volSlider');
        const dampSlider = document.getElementById('dampSlider');
        const substepsSlider = document.getElementById('substepsSlider');
        const shotSpeedSlider = document.getElementById('shotSpeedSlider');
        const shotRadiusSlider = document.getElementById('shotRadiusSlider');
        const recoveryTimeSlider = document.getElementById('recoveryTimeSlider');
        const grabSizeSlider = document.getElementById('grabSizeSlider');
        const grabSphereToggle = document.getElementById('grabSphereToggle');
        
        // Real-time physics parameter updates
        if (edgeSlider) {
            edgeSlider.oninput = () => {
                const val = parseFloat(edgeSlider.value);
                document.getElementById('edgeValue').textContent = val.toFixed(2);
                if (this.softBody) {
                    this.softBody.setEdgeCompliance(val);
                    console.log('[Core] Edge compliance changed to:', val);
                }
            };
        }
        
        if (volSlider) {
            volSlider.oninput = () => {
                const val = parseFloat(volSlider.value);
                document.getElementById('volValue').textContent = val.toFixed(2);
                if (this.softBody) {
                    this.softBody.setVolCompliance(val);
                    console.log('[Core] Volume compliance changed to:', val);
                }
            };
        }
        
        if (dampSlider) {
            dampSlider.oninput = () => {
                const val = parseFloat(dampSlider.value);
                document.getElementById('dampValue').textContent = val.toFixed(2);
                if (this.softBody) {
                    this.softBody.setDamping(val);
                    console.log('[Core] Damping changed to:', val);
                }
            };
        }
        
        if (substepsSlider) {
            substepsSlider.oninput = () => {
                const val = parseInt(substepsSlider.value);
                document.getElementById('substepsValue').textContent = val;
                this.physicsSubsteps = val;
                this.performanceMode = false; // Manual adjustment disables auto mode
                console.log('[Core] Substeps changed to:', val);
            };
        }

        // Shot parameter controls
        if (shotSpeedSlider) {
            shotSpeedSlider.oninput = () => {
                const val = parseFloat(shotSpeedSlider.value);
                this.shootSpeed = val;
                document.getElementById('shotSpeedValue').textContent = val.toFixed(1);
            };
        }
        
        if (shotRadiusSlider) {
            shotRadiusSlider.oninput = () => {
                const val = parseFloat(shotRadiusSlider.value);
                this.shootRadius = val;
                document.getElementById('shotRadiusValue').textContent = val.toFixed(2);
                if (this.shootSphere && this.isShootActive) {
                    this.shootSphere.setRadiusScale(val);
                }
            };
        }
        
        if (recoveryTimeSlider) {
            recoveryTimeSlider.oninput = () => {
                const val = parseFloat(recoveryTimeSlider.value);
                this.recoveryTime = val;
                document.getElementById('recoveryTimeValue').textContent = val.toFixed(1);
            };
        }

        // ★ Grab Mode controls (unified slider)
        if (grabSizeSlider) {
            grabSizeSlider.oninput = () => {
                const val = parseFloat(grabSizeSlider.value);
                this.grabRadius = val;  // グラブ範囲とスフィアサイズ両方を設定
                document.getElementById('grabSizeValue').textContent = val.toFixed(2);
                // ★ リアルタイムでスフィアサイズを更新
                if (this.grabSphere) {
                    this.grabSphere.setRadiusScale(val);
                }
                console.log('[Core] Grab size changed to:', val);
            };
        }

        if (grabSphereToggle) {
            grabSphereToggle.onchange = () => {
                this.grabSphereVisible = grabSphereToggle.checked;
                console.log('[Core] Grab sphere visibility:', this.grabSphereVisible);
            };
        }
        
        // ★ スライダーの現在値 → コア変数へ同期（ブラウザのフォーム記憶対策）
        const speedSliderEl = document.getElementById('shotSpeedSlider');
        const radiusSliderEl = document.getElementById('shotRadiusSlider');
        const recoverySliderEl = document.getElementById('recoveryTimeSlider');
        const edgeSliderEl = document.getElementById('edgeSlider');
        const volSliderEl = document.getElementById('volSlider');
        const dampSliderEl = document.getElementById('dampSlider');
        const substepsSliderEl = document.getElementById('substepsSlider');
        const grabSizeSliderEl = document.getElementById('grabSizeSlider');
        const grabSphereToggleEl = document.getElementById('grabSphereToggle');

        if (speedSliderEl) {
            this.shootSpeed = parseFloat(speedSliderEl.value);
        }
        if (radiusSliderEl) {
            this.shootRadius = parseFloat(radiusSliderEl.value);
        }
        if (recoverySliderEl) {
            this.recoveryTime = parseFloat(recoverySliderEl.value);
        }
        if (substepsSliderEl) {
            this.physicsSubsteps = parseInt(substepsSliderEl.value);
        }
        if (grabSizeSliderEl) {
            this.grabRadius = parseFloat(grabSizeSliderEl.value);
        }
        if (grabSphereToggleEl) {
            this.grabSphereVisible = grabSphereToggleEl.checked;
        }

        // ★ コア変数 → UI表示へ同期（表示を確実に一致させる）
        if (document.getElementById('shotSpeedValue')) {
            document.getElementById('shotSpeedValue').textContent = this.shootSpeed.toFixed(1);
        }
        if (document.getElementById('shotRadiusValue')) {
            document.getElementById('shotRadiusValue').textContent = this.shootRadius.toFixed(2);
        }
        if (document.getElementById('recoveryTimeValue')) {
            document.getElementById('recoveryTimeValue').textContent = this.recoveryTime.toFixed(1);
        }
        if (document.getElementById('substepsValue')) {
            document.getElementById('substepsValue').textContent = this.physicsSubsteps;
        }
        if (edgeSliderEl && document.getElementById('edgeValue')) {
            document.getElementById('edgeValue').textContent = parseFloat(edgeSliderEl.value).toFixed(2);
        }
        if (volSliderEl && document.getElementById('volValue')) {
            document.getElementById('volValue').textContent = parseFloat(volSliderEl.value).toFixed(2);
        }
        if (dampSliderEl && document.getElementById('dampValue')) {
            document.getElementById('dampValue').textContent = parseFloat(dampSliderEl.value).toFixed(2);
        }
        if (document.getElementById('grabSizeValue')) {
            document.getElementById('grabSizeValue').textContent = this.grabRadius.toFixed(2);
        }

        console.log('[Core] Physics panel initialized. Core vars synced from sliders:',
            'speed=', this.shootSpeed, 'radius=', this.shootRadius, 'recovery=', this.recoveryTime);
    }

    showPhysicsPanel() {
        const panel = document.getElementById('compliance-panel');
        if (panel) {
            panel.style.display = 'block';
            console.log('[Core] Physics panel shown');
        } else {
            console.error('[Core] Physics panel element not found!');
        }
    }

    hidePhysicsPanel() {
        const panel = document.getElementById('compliance-panel');
        if (panel) {
            panel.style.display = 'none';
            console.log('[Core] Physics panel hidden');
        }
    }

    togglePhysicsPanel() {
        const panel = document.getElementById('compliance-panel');
        if (panel) {
            const isVisible = panel.style.display !== 'none' && panel.style.display !== '';
            if (isVisible) {
                this.hidePhysicsPanel();
            } else {
                this.showPhysicsPanel();
            }
        }
    }

    // Screenshot functionality (migrated from index.html)
    takeScreenshot() {
        try {
            // Next frame capture for drawing completion
            requestAnimationFrame(() => {
                const dataURL = this.canvas.toDataURL('image/png', 1.0);
                
                if (dataURL === 'data:,') {
                    alert('スクリーンショットが空です。描画内容を確認してください。');
                    return;
                }
                
                // Create download link
                const link = document.createElement('a');
                
                // Generate timestamped filename
                const now = new Date();
                const timestamp = now.getFullYear() + 
                    String(now.getMonth() + 1).padStart(2, '0') + 
                    String(now.getDate()).padStart(2, '0') + '_' +
                    String(now.getHours()).padStart(2, '0') + 
                    String(now.getMinutes()).padStart(2, '0') + 
                    String(now.getSeconds()).padStart(2, '0');
                
                const filename = `SoftGLB_${timestamp}.png`;
                
                // Execute download
                link.href = dataURL;
                link.download = filename;
                link.style.display = 'none';
                document.body.appendChild(link);
                link.click();
                document.body.removeChild(link);
                
                console.log('[Core] Screenshot saved as:', filename);
                
                // UI feedback
                const btn = document.querySelector('button[onclick*="takeScreenshot"]');
                if (btn) {
                    const originalText = btn.textContent;
                    btn.textContent = '✅ Saved!';
                    btn.style.background = 'linear-gradient(135deg, #00ff88, #00cc66)';
                    
                    setTimeout(() => {
                        btn.textContent = originalText;
                        btn.style.background = '';
                    }, 1500);
                }
            });
            
        } catch(error) {
            console.error('[Core] Screenshot error:', error);
            alert('スクリーンショット取得に失敗しました: ' + error.message);
        }
    }

    //=========================================================================
    // Shot System (migrated from index.html) - Phase 3
    //=========================================================================
    async initShootSphere() {
        if (this.shootSphere) return; // Already created
        
        try {
            const res = await fetch('model/ioSphere.obj');
            if (!res.ok) return;
            const objText = await res.text();

            this.shootSphere = new this.Module.SphereColliderPhysics();
            if (!this.shootSphere.initFromOBJString(objText)) {
                this.shootSphere.delete(); 
                this.shootSphere = null;
                return;
            }
            
            this.resetShootSphere();
            console.log('[Core] Shoot sphere initialized for reuse');
        } catch(e) {
            console.warn('[Core] Shoot sphere creation failed:', e.message);
        }
    }

    resetShootSphere() {
        if (!this.shootSphere) return;
        
        // Move to far standby position (prevent visual noise)
        this.shootSphere.setCenterXYZ(-1000, -1000, -1000);
        this.shootSphere.visible = false;
        this.isShootActive = false;
        
        console.log('[Core] Shoot sphere moved to standby position');
    }

    fireShootSphere(targetX, targetY, targetZ) {
        if (!this.shootSphere || !this.softBody) return;
        
        const camPos = this.camera.getPosition();
        
        // Set position, size and visibility (eliminate visual noise)
        this.shootSphere.setCenterXYZ(camPos[0], camPos[1], camPos[2]);
        this.shootSphere.setRadiusScale(this.shootRadius);
        
        this.shootSphere.visible = false; // Hide initially
        
        // Show after 30ms delay (smooth appearance)
        setTimeout(() => {
            if (this.shootSphere && this.isShootActive) {
                this.shootSphere.visible = true;
                console.log('[Core] Shot sphere visible after delay');
            }
        }, 30);
        
        // Calculate trajectory vector
        const dx = targetX - camPos[0];
        const dy = targetY - camPos[1]; 
        const dz = targetZ - camPos[2];
        const len = Math.sqrt(dx*dx + dy*dy + dz*dz);
        
        // Normalized direction × speed
        const vx = (dx / len) * this.shootSpeed;
        const vy = (dy / len) * this.shootSpeed;
        const vz = (dz / len) * this.shootSpeed;
        
        // Start physics interaction
        if (!this.isShootActive) {
            this.Module.addSphereCollider(this.softBody, this.shootSphere);
            this.isShootActive = true;
            console.log('[Core] Shoot sphere added to physics');
        }
        
        // Initialize drag state for CCD
        this.shootSphere.startDragAt(camPos[0], camPos[1], camPos[2], 1.0);
        
        console.log(`[Core] Shot fired! Speed: ${this.shootSpeed}, Direction: (${vx.toFixed(2)}, ${vy.toFixed(2)}, ${vz.toFixed(2)})`);
        
        // Start animation
        this.animateShootSphere(targetX, targetY, targetZ, vx, vy, vz);
    }

    animateShootSphere(targetX, targetY, targetZ, vx, vy, vz) {
        if (!this.shootSphere || !this.isShootActive) return;
        
        const startTime = performance.now();
        
        const updateShot = () => {
            if (!this.isShootActive) return;
            
            const elapsed = (performance.now() - startTime) / 1000;
            
            if (elapsed >= this.recoveryTime) {
                // Auto-recycle: preserve normal spheres
                if (this.shootSphere) {
                    this.Module.clearSphereColliders(this.softBody);
                    
                    // Re-register normal spheres (preserve collision)
                    if (this.sphereCollider) {
                        this.Module.addSphereCollider(this.softBody, this.sphereCollider);
                        console.log('[Core] Sphere 1 restored after shot');
                    }
                    if (this.sphereCollider2) {
                        this.Module.addSphereCollider(this.softBody, this.sphereCollider2);
                        console.log('[Core] Sphere 2 restored after shot');
                    }
                }
                
                this.resetShootSphere();
                console.log(`[Core] Shot recycled after ${this.recoveryTime}s, normal spheres restored`);
                return;
            }
            
            // Trajectory calculation
            const currentX = this.camera.getPosition()[0] + vx * elapsed;
            const currentY = this.camera.getPosition()[1] + vy * elapsed;  
            const currentZ = this.camera.getPosition()[2] + vz * elapsed;
            
            // CCD-enabled movement (60FPS stable)
            this.shootSphere.moveDragTo(currentX, currentY, currentZ, 1/60);
            this.shootSphere.update(1/60);  // Enable CCD
            
            // Continue animation
            requestAnimationFrame(updateShot);
        };
        
        updateShot();
    }

}

//=========================================================================
// Export
//=========================================================================
if (typeof module !== 'undefined') {
    module.exports = SoftBodyCore;
}