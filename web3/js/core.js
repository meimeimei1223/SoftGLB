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
        
        // Shader programs
        this.program = null;
        this.wireProgram = null;
        
        // Buffers
        this.buffers = {};
        
        // Rendering state
        this.showWireframe = false;
        this.sphereVisible = true;
        this.isShootActive = false;
        
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
        
        // Platform-specific camera
        this._camPos = new Float32Array(3);
        const config = getPlatformConfig(platform);
        this.camera = {
            radius: config.cameraRadius,
            yaw: 0, 
            pitch: 15, 
            fov: 45,
            getPosition: () => {
                const p = this.camera.pitch * Math.PI / 180;
                const y = this.camera.yaw * Math.PI / 180;
                this._camPos[0] = this.camera.radius * Math.cos(p) * Math.cos(y);
                this._camPos[1] = this.camera.radius * Math.sin(p);
                this._camPos[2] = this.camera.radius * Math.cos(p) * Math.sin(y);
                return this._camPos;
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
        this.gl.clearColor(0.1, 0.1, 0.18, 1.0);
        
        this.resize();
        this.setupShaders();
        this.setupBuffers();
        this.cacheShaderLocations();
        
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

                float ambientFactor  = 0.3;
                float specularFactor = 0.8;
                float shininess      = 32.0;

                vec3 ambient  = uLightColor * ambientFactor;
                vec3 diffuse  = uLightColor * max(dot(N, L), 0.0);
                vec3 specular = uLightColor * specularFactor * pow(max(dot(N, H), 0.0), shininess);

                vec3 lighting = ambient + diffuse + specular;

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

    // GLB loading
    async loadGLB(arrayBuffer) {
        // Implementation from main index.html
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
            gl.uniform3f(this.uloc.lightColor, 1, 1, 1);
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
            gl.drawArrays(gl.LINES, 0, this.numTetEdgeVerts);
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

    // Sphere management
    addSecondSphere() {
        // Implementation from main index.html
    }
    
    clearAllSpheres() {
        // Implementation from main index.html
    }
    
    // Screenshot
    takeScreenshot() {
        // Implementation from main index.html
    }
}

//=========================================================================
// Export
//=========================================================================
if (typeof module !== 'undefined') {
    module.exports = SoftBodyCore;
}