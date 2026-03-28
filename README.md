# 🫀 SoftGLB - Web3 SoftBody Physics

Real-time soft-body physics simulation with GLB direct loading and tetrahedralization.

[![Deploy with Vercel](https://vercel.com/button)](https://vercel.com/new/clone?repository-url=https://github.com/meimeimei1223/SoftGLB)

## 🌟 Features

- **🔧 GLB Direct Loading** - Upload any GLB file for instant tetrahedralization
- **⚡ Real-time Physics** - 30-35fps C++ performance via WebAssembly  
- **🔵 Dual Sphere Colliders** - Interactive physics collision objects
- **🎛️ Parameter Tuning** - Real-time physics adjustment panel
- **📱 Performance Modes** - Substeps adjustment for speed vs accuracy

## 🎮 Controls

| Input | Action |
|-------|--------|
| **Left Click** | Grab mesh (excluding fixed bottom 1/3) |
| **Left Drag (empty)** | Rotate camera |
| **Middle Click** | Move sphere colliders |
| **Scroll** | Zoom (up=closer) |

### Keyboard Controls

| Key | Function |
|-----|----------|
| **S** | Toggle wireframe |
| **R** | Reset shape |
| **V** | Toggle sphere visibility |
| **N/M** | Adjust sphere size |
| **W** | Add 2nd sphere (orange) |
| **C** | Clear all spheres |
| **P** | Open Physics Panel |
| **F** | Performance mode toggle |

## 🛠️ Physics Parameters

**Real-time adjustable in Physics Panel (P key):**
- **Edge Compliance** (0.0-3.0): Softness control
- **Volume Compliance** (0.0-1.0): Volume change allowance  
- **Damping** (0.90-0.99): Energy dissipation
- **Physics Substeps** (2-20): Speed vs accuracy balance

## 🚀 Live Demo

**Vercel Deployment:** [https://soft-glb.vercel.app](https://soft-glb.vercel.app)

## 💻 Local Development

```bash
# Clone repository
git clone https://github.com/meimeimei1223/SoftGLB.git
cd SoftGLB

# Start local server
cd web3
python3 -m http.server 8085

# Open browser
open http://localhost:8085
```

## 🔧 Build from Source (Optional)

```bash
# Install Emscripten
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh

# Build WASM
cd wasm/build
emcmake cmake -G Ninja ..
emmake ninja

# Copy to web3
cp softbody.js ../../web3/
cp softbody.wasm ../../web3/
```

## 📁 Project Structure

```
├── web3/                   ← 🚀 Vercel deployment target
│   ├── index.html          ← Main application
│   ├── softbody.js         ← WASM JavaScript glue
│   ├── softbody.wasm       ← WebAssembly binary  
│   └── model/
│       ├── liver.glb       ← Default model
│       └── ioSphere.obj    ← Sphere collider mesh
├── src/                    ← C++ physics engine source
├── wasm/                   ← Emscripten build configuration
├── vercel.json             ← Vercel deployment config
└── DEPLOYMENT_PLAN.md      ← Detailed deployment guide
```

## 🎯 Technical Highlights

### **C++ Physics Engine**
- Position Based Dynamics (PBD) solver
- Real-time tetrahedralization via `CentVoxTetrahedralizerHybrid`
- Sphere-mesh collision detection
- Bottom 1/3 particle fixing (anatomically correct)

### **WebAssembly Optimization**
- Zero-copy buffer updates (`bufferSubData`)
- Cached uniform/attribute locations  
- Pre-allocated matrices (no `new Float32Array` per frame)
- Conditional tetEdge updates (wireframe only)

### **Rendering Performance**
- **Target**: 30-35fps (matching C++ native)
- **Optimizations**: 
  - Eliminated 12+ GPU queries per frame
  - Eliminated 8+ memory allocations per frame
  - Stream-optimized buffer usage

## 🌐 Web Technologies

- **WebAssembly (WASM)** - C++ physics engine
- **WebGL** - Hardware-accelerated rendering
- **Emscripten** - C++ to WASM compilation
- **GLB/GLTF** - 3D model format support

## 📜 License

MIT License - Feel free to use for research and development

## 🤖 Generated with Claude Code

This project was implemented with assistance from Claude Code, Anthropic's AI coding assistant.

---

**🎉 Experience real-time soft-body physics in your browser!**