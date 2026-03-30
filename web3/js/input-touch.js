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
        
        // Touch gesture detection
        this.tapTimeout = null;
        this.longPressTimeout = null;
        this.gestureThreshold = 10; // pixels
        
        console.log('[TouchInputHandler] Initialized for mobile/tablet');
    }

    //=========================================================================
    // Event Listener Setup
    //=========================================================================
    setupEventListeners() {
        // Touch events
        this.canvas.addEventListener('touchstart', this.onTouchStart.bind(this), {passive: false});
        this.canvas.addEventListener('touchmove', this.onTouchMove.bind(this), {passive: false});
        this.canvas.addEventListener('touchend', this.onTouchEnd.bind(this), {passive: false});
        this.canvas.addEventListener('touchcancel', this.onTouchEnd.bind(this));
        
        // Prevent context menu on mobile
        this.canvas.addEventListener('contextmenu', e => e.preventDefault());
        
        // Window events
        window.addEventListener('resize', this.onResize.bind(this));
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
        
        // Set up long press detection (for sphere operations)
        this.longPressTimeout = setTimeout(() => {
            this.handleLongPress(touch.clientX, touch.clientY);
        }, 500); // 0.5 second long press
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
            // Mesh grabbing
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
        
        // Check for tap gesture
        if (this.grabTimer && performance.now() - this.grabTimer < 200) {
            // Quick tap - check for mesh grab
            this.handleTap(this.grabStartPos.x, this.grabStartPos.y);
        }
        
        // Reset states
        this.isPinching = false;
        this.isRotating = false;
        
        if (this.grabActive) {
            this.endMeshGrab();
        }
    }

    //=========================================================================
    // Gesture Recognition
    //=========================================================================
    handleTap(x, y) {
        console.log('[TouchInput] Tap detected at:', x, y);
        
        // Try mesh grab
        const hit = this.raycastMesh(x, y);
        if (hit) {
            this.startMeshGrab(hit);
            
            // Auto-release after short grab
            setTimeout(() => {
                if (this.grabActive) this.endMeshGrab();
            }, 100);
        }
    }

    handleLongPress(x, y) {
        console.log('[TouchInput] Long press detected - sphere operation');
        
        // Long press can be used for sphere shooting or special operations
        // For now, just provide feedback
        this.showTouchFeedback(x, y, 'Long Press!');
    }

    handleCameraRotation(dx, dy) {
        // Slower rotation for touch (more controllable)
        const sensitivity = 0.2;  // Reduced from PC's 0.3
        this.core.camera.yaw += dx * sensitivity;
        this.core.camera.pitch = Math.max(-89, Math.min(89, this.core.camera.pitch + dy * sensitivity));
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
    // Physics Operations (Touch-adapted)
    //=========================================================================
    raycastMesh(x, y) {
        // Will implement detailed raycast - same logic as PC but adapted for touch
        return null; // Placeholder
    }

    startMeshGrab(hit) {
        this.grabActive = true;
        console.log('[TouchInput] Mesh grab started');
    }

    moveMeshGrab(x, y) {
        // Implement mesh grabbing movement
    }

    endMeshGrab() {
        this.grabActive = false;
        console.log('[TouchInput] Mesh grab ended');
    }

    moveSphere(sphere, grabDist, x, y) {
        // Implement sphere movement
    }
}

//=========================================================================
// Export
//=========================================================================
if (typeof module !== 'undefined') {
    module.exports = TouchInputHandler;
}