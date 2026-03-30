// platform-detect.js - Device detection and platform-specific setup
// Web3 SoftBody Physics - Modular architecture

//=========================================================================
// Platform Detection
//=========================================================================
async function detectPlatform() {
    // まずスマホ/タブレット判定を優先（XRより先に）
    const userAgent = navigator.userAgent.toLowerCase();
    const isMobileUA = /android|iphone|ipad|ipod|blackberry|iemobile|opera mini|mobile/i.test(userAgent);
    
    if (window.matchMedia) {
        const isTouch = window.matchMedia('(pointer: coarse)').matches;
        const isMobile = window.matchMedia('(max-width: 768px)').matches;
        const hasTouch = 'ontouchstart' in window;
        
        if (isTouch || isMobile || hasTouch || isMobileUA) {
            console.log('[detectPlatform] Mobile/Touch device detected');
            console.log('[detectPlatform] Details - Touch:', isTouch, 'Mobile:', isMobile, 'HasTouch:', hasTouch, 'UA:', isMobileUA);
            return 'touch';
        }
    }
    
    // VR/AR判定（真のVR/ARヘッドセットのみ）
    if (navigator.xr && !isMobileUA) {
        try {
            const vrSupported = await navigator.xr.isSessionSupported('immersive-vr');
            const arSupported = await navigator.xr.isSessionSupported('immersive-ar');
            
            if (vrSupported || arSupported) {
                console.log('[detectPlatform] True XR headset detected (VR:', vrSupported, 'AR:', arSupported, ')');
                return 'xr';
            }
        } catch(e) {
            console.log('[detectPlatform] XR check failed:', e.message);
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
            infoElement.style.background = 'rgba(255,0,255,0.8)';
            break;
        case 'touch':
            infoElement.textContent = '📱 Mobile Mode';
            infoElement.style.background = 'rgba(0,255,100,0.8)';
            break;
        case 'pc':
        default:
            infoElement.textContent = '🖥️ Desktop Mode';
            infoElement.style.background = 'rgba(0,100,255,0.8)';
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