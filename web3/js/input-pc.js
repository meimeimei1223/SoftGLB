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
        this.fixedThreshold = -999;
        this.ungrabbableThreshold = -999;
        
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

        // Mesh grabbing
        if (this.grabActive && this.core.softBody) {
            this.moveMeshGrab(e.clientX, e.clientY);
            return;
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
            if (this.grabActive && this.core.softBody) {
                this.core.softBody.endGrab(this.grabPrevPos[0], this.grabPrevPos[1], this.grabPrevPos[2], 0, 0, 0);
                this.grabActive = false;
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
    }

    //=========================================================================
    // Raycast and Collision (PC-specific precise implementation)
    //=========================================================================
    raycastMesh(screenX, screenY) {
        // Detailed implementation will be copied from main index.html
        if (!this.core.softBody) return null;
        
        // Placeholder for now - will implement full raycast
        console.log('[PCInput] Raycast at:', screenX, screenY);
        return null;
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

    // Sphere management functions (to be implemented)
    handleRightClick(x, y) { console.log('[PCInput] Right click shooting:', x, y); }
    handleMiddleClick(x, y) { console.log('[PCInput] Middle click sphere:', x, y); }
    startMeshGrab(hit) { console.log('[PCInput] Start mesh grab'); }
    moveMeshGrab(x, y) { console.log('[PCInput] Move mesh grab'); }
    moveSphere(sphere, grabDist, x, y) { console.log('[PCInput] Move sphere'); }
}

//=========================================================================
// Export
//=========================================================================
if (typeof module !== 'undefined') {
    module.exports = PCInputHandler;
}