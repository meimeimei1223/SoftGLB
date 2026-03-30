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
    
    // Matrix calculations
    updateViewMatrix() {
        // Implementation from main index.html
    }
    
    updateProjMatrix() {
        // Implementation from main index.html  
    }
    
    computeNormalMatrix() {
        // Identity matrix for unit model matrix
        this._normalMat[0] = 1; this._normalMat[1] = 0; this._normalMat[2] = 0;
        this._normalMat[3] = 0; this._normalMat[4] = 1; this._normalMat[5] = 0;
        this._normalMat[6] = 0; this._normalMat[7] = 0; this._normalMat[8] = 1;
        return this._normalMat;
    }

    // GLB loading
    async loadGLB(arrayBuffer) {
        // Implementation from main index.html
    }
    
    // Rendering
    render() {
        // Implementation from main index.html
    }
    
    drawMesh() {
        // Implementation from main index.html
    }
    
    drawSpheres() {
        // Implementation from main index.html  
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