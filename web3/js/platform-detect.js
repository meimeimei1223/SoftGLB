// platform-detect.js - Device detection and platform-specific setup
// Web3 SoftBody Physics - Modular architecture

//=========================================================================
// Platform Detection
//=========================================================================
async function detectPlatform() {
    // VR/AR最優先（Quest/Vision Pro等のブラウザ）
    if (navigator.xr) {
        try {
            const vrSupported = await navigator.xr.isSessionSupported('immersive-vr');
            const arSupported = await navigator.xr.isSessionSupported('immersive-ar');
            if (vrSupported || arSupported) {
                console.log('[detectPlatform] XR device detected');
                return 'xr';
            }
        } catch(e) {
            // XR not available
        }
    }
    
    // スマホ/タブレット判定
    if (window.matchMedia) {
        const isTouch = window.matchMedia('(pointer: coarse)').matches;
        const isMobile = window.matchMedia('(max-width: 768px)').matches;
        const hasTouch = 'ontouchstart' in window;
        
        if (isTouch || isMobile || hasTouch) {
            console.log('[detectPlatform] Mobile/Touch device detected');
            return 'touch';
        }
    }
    
    // デスクトップPC
    console.log('[detectPlatform] Desktop PC detected');
    return 'pc';
}

//=========================================================================
// Platform-specific initialization
//=========================================================================
async function initPlatform() {
    const platform = await detectPlatform();
    
    // プラットフォーム情報をUIに表示
    const infoElement = document.createElement('div');
    infoElement.id = 'platform-info';
    infoElement.style.cssText = `
        position: absolute; top: 10px; right: 200px;
        background: rgba(0,100,255,0.8); color: white;
        padding: 8px 12px; border-radius: 6px;
        font-size: 12px; font-weight: bold;
    `;
    
    switch(platform) {
        case 'xr':
            infoElement.textContent = '🥽 VR/AR Mode';
            break;
        case 'touch':
            infoElement.textContent = '📱 Mobile Mode';
            break;
        case 'pc':
        default:
            infoElement.textContent = '🖥️ Desktop Mode';
            break;
    }
    
    document.body.appendChild(infoElement);
    
    return platform;
}

//=========================================================================
// Platform-specific configurations
//=========================================================================
const PLATFORM_CONFIG = {
    pc: {
        cameraRadius: 5,
        cameraSpeed: 0.3,
        physicsSubsteps: 10,
        inputMethod: 'mouse-keyboard',
        defaultGridSize: 20
    },
    touch: {
        cameraRadius: 7,      // 少し引いた視点
        cameraSpeed: 0.5,     // タッチは少し速く
        physicsSubsteps: 6,   // パフォーマンス重視
        inputMethod: 'touch',
        defaultGridSize: 15   // 軽量化
    },
    xr: {
        cameraRadius: 1,      // VRは近接
        cameraSpeed: 1.0,     
        physicsSubsteps: 8,
        inputMethod: 'xr-controllers',
        defaultGridSize: 18
    }
};

function getPlatformConfig(platform) {
    return PLATFORM_CONFIG[platform] || PLATFORM_CONFIG.pc;
}

//=========================================================================
// Export for module system
//=========================================================================
if (typeof module !== 'undefined') {
    module.exports = { detectPlatform, initPlatform, getPlatformConfig };
}