# Web3 SoftBody Physics - GitHub → Vercel デプロイメント計画書

## 🎯 プロジェクト概要

**現状:** C++物理エンジンをEmscripten/WASMでブラウザ化、ローカル開発完了
**目標:** GitHub → Vercel連携により、世界中からアクセス可能なWebアプリケーションとして公開

---

## 📁 現在のファイル構成確認

```
AAA_SoftBodyLiver-mainWasm/
├── src/                    ← C++物理エンジン本体
│   ├── SoftBodyPhysics.cpp
│   ├── SphereColliderPhysics.cpp
│   ├── GlbLoader.cpp
│   ├── WasmBindings.cpp    ← 完全実装済み
│   └── ...
├── wasm/                   ← Emscripten ビルド設定
│   ├── CMakeLists.txt      ← 完全設定済み
│   ├── build/
│   │   ├── softbody.js     ← 最新ビルド
│   │   └── softbody.wasm   ← 最新ビルド
│   └── third_party/        ← 依存ライブラリ
└── web3/                   ← ✅ Vercel デプロイ対象
    ├── index.html          ← メインアプリケーション
    ├── softbody.js         ← WASM JavaScript Glue
    ├── softbody.wasm       ← WebAssembly バイナリ
    └── model/
        ├── liver.glb       ← デフォルトモデル
        ├── ioSphere.obj    ← 球コライダー
        └── model1/         ← 従来形式フォールバック
```

---

## 🚀 実装ステップ（推奨順序）

### **Step 1: Git リポジトリ整備** ⭐ 最重要
```bash
# 1.1 .gitignore 作成
cd AAA_SoftBodyLiver-mainWasm
echo "
# ビルド成果物
wasm/build/CMakeCache.txt
wasm/build/CMakeFiles/
wasm/build/.ninja_*
wasm/build/build.ninja
wasm/build/cmake_install.cmake

# エディタ・OS
.DS_Store
.vscode/
*.tmp
*.log

# 一時ファイル  
temp/
*.bak
" > .gitignore

# 1.2 Git初期化
git init
git add .
git commit -m "🎉 Initial commit: Web3 SoftBody Physics Complete

✅ Features:
- GLB direct loading with real-time tetrahedralization
- Dual sphere colliders with physics collision
- Real-time parameter adjustment
- C++ main.cpp compatible implementation
- Optimized rendering (30-35fps)

🔧 Generated with Claude Code
Co-Authored-By: Claude <noreply@anthropic.com>"

# 1.3 GitHub連携（GitHubでリポジトリ作成後）
git remote add origin https://github.com/USERNAME/REPOSITORY.git
git branch -M main
git push -u origin main
```

### **Step 2: Vercel 最適化設定**

**2.1 vercel.json 作成**
```json
{
  "version": 2,
  "name": "web3-softbody-physics",
  "builds": [
    {
      "src": "web3/**",
      "use": "@vercel/static"
    }
  ],
  "routes": [
    {
      "src": "/(.*)",
      "dest": "/web3/$1"
    }
  ],
  "headers": [
    {
      "source": "/(.*\\.wasm)",
      "headers": [
        {
          "key": "Content-Type",
          "value": "application/wasm"
        },
        {
          "key": "Cross-Origin-Opener-Policy",
          "value": "same-origin"
        },
        {
          "key": "Cross-Origin-Embedder-Policy", 
          "value": "require-corp"
        }
      ]
    },
    {
      "source": "/(.*\\.glb)",
      "headers": [
        {
          "key": "Content-Type",
          "value": "model/gltf-binary"
        }
      ]
    }
  ]
}
```

**2.2 web3/README.md 作成**
```markdown
# Web3 SoftBody Physics

Real-time soft-body physics simulation with GLB loading.

## Features
- 🫀 GLB → Tetrahedralization → Physics
- 🔵 Dual sphere colliders
- 🎛️ Real-time parameter adjustment
- ⚡ 30-35fps C++ performance

## Controls
- Left Click: Grab mesh
- Middle Click: Move spheres  
- P: Physics Panel
- W: Add 2nd sphere
```

### **Step 3: Vercel デプロイ実行**

**3.1 Vercel CLI デプロイ**
```bash
# Vercel CLI インストール
npm i -g vercel

# 初回デプロイ
cd AAA_SoftBodyLiver-mainWasm
vercel

# 本番デプロイ
vercel --prod
```

**3.2 GitHub連携**
- Vercel Dashboard → New Project
- Import Git Repository → 作成したGitHubリポジトリ選択
- Framework: Other
- Root Directory: `./`
- Build Command: (空欄)
- Output Directory: `web3`

### **Step 4: 動作確認・最適化**

**4.1 デプロイ確認項目**
- [ ] liver.glb自動ロード
- [ ] GLBアップロード機能  
- [ ] 球コライダー動作
- [ ] Physics Panel機能
- [ ] WASM読み込み速度

**4.2 パフォーマンス最適化**
- WASM圧縮（gzip）
- GLB CDN キャッシュ
- 初期ロード最適化

---

## 🎛️ オプション機能（Phase 5以降）

### **CI/CD自動化**
- GitHub Actions でEmscripten自動ビルド
- C++変更時のWASM自動更新

### **機能拡張**
- URLパラメータでモデル指定
- モバイル対応
- パフォーマンス統計ダッシュボード

---

## 📊 期待される成果物

**✅ 完成イメージ:**
- **URL**: `https://web3-softbody.vercel.app`
- **機能**: GLBアップロード→リアルタイム四面体化→物理シミュレーション
- **パフォーマンス**: C++ネイティブ並みの30-35fps
- **アクセス**: 世界中どこからでも利用可能

---

## 🎯 最初の着手推奨

**今すぐ実行すべきタスク:**
1. ✅ Git リポジトリ初期化
2. ✅ .gitignore作成
3. ✅ 初回コミット・GitHub連携
4. ✅ vercel.json設定
5. ✅ Vercelデプロイテスト

**これらのステップから始めますか？**