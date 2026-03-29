# 🚀 Web3 SoftBody Physics - 今後の開発計画

## 📊 現在の状況

**✅ Phase 0 完了：基本実装**
- GitHub: `https://github.com/meimeimei1223/SoftGLB`
- Vercel: `https://soft-glb.vercel.app/`
- 機能：GLB → リアルタイム四面体化 → C++物理エンジン
- パフォーマンス：30-35fps（C++ネイティブ並み）

---

## 🎯 マルチプラットフォーム対応計画

### **アーキテクチャ設計原則**
```
📱 Device Detection → 🎮 Input Layer → 🧠 Core Engine → 🖥️ Rendering

・Core Engine (WASM + WebGL): 全プラットフォーム完全共通
・Input Layer: プラットフォーム別実装
・Rendering Loop: XR は分岐、他は共通
```

---

## 🗓️ 実装フェーズ

### **Phase 1: JSリファクタリング（基盤投資）** ⭐ 最重要
**目標:** 現在の1400行 index.html をモジュール分割し、将来の拡張性を確保

#### **ファイル構成（推奨）**
```
web3/
├── index.html              ← エントリーポイント（200行程度に圧縮）
├── js/
│   ├── core.js             ← WebGL・物理・レンダリング（800行）
│   ├── input-pc.js         ← マウス・キーボード（300行）
│   ├── input-touch.js      ← タッチ操作（Phase 2で追加）
│   ├── input-xr.js         ← VR/ARコントローラー（Phase 4で追加）
│   ├── platform-detect.js  ← デバイス判定（50行）
│   └── ui-panels.js        ← Physics Panel等（200行）
├── softbody.js / .wasm     ← WASM（変更なし）
└── model/                  ← GLB/OBJ ファイル
```

#### **リファクタリング作業内容**
1. **core.js 切り出し**
   - WebGL初期化（`init()`）
   - 描画関数（`drawMesh()`, `drawSphere()`）
   - バッファ管理（`setupMeshBuffers()`, `updateMeshBuffers()`）
   - 物理ループ（`render()`）

2. **input-pc.js 切り出し**
   - イベントリスナー（`mousedown`, `mousemove`, `keydown`）
   - レイキャスト関数（`raycastMesh()`, `raySphereIntersect()`）
   - グラブ管理（`grabActive`, `sphereDragging`）

3. **platform-detect.js 作成**
   ```javascript
   function detectPlatform() {
       // VR/AR最優先
       if (navigator.xr && await navigator.xr.isSessionSupported('immersive-vr'))
           return 'xr';
       
       // スマホ/タブレット判定
       if (window.matchMedia && window.matchMedia('(pointer: coarse)').matches)
           return 'touch';
           
       // デスクトップ
       return 'pc';
   }
   ```

**Phase 1 成果物:** 現在の機能を100%維持しながら、拡張可能なアーキテクチャに転換

---

### **Phase 2: PCブラウザ完全対応（現状の改良）**

#### **PC最適化項目**
1. **操作性改善**
   - ファイルドロップ対応（ドラッグ&ドロップでGLB読み込み）
   - 右クリックコンテキストメニュー
   - ホットキー一覧表示

2. **UI/UX向上**
   - パラメータプリセット機能
   - パフォーマンス統計ダッシュボード
   - フルスクリーンモード

3. **デバッグ機能**
   - コンソールパネル（WebGL統計表示）
   - パフォーマンスプロファイラ

**Phase 2 成果物:** デスクトップでのプロフェッショナル級操作性

---

### **Phase 3: スマホブラウザ対応**

#### **タッチ操作設計**
```
🤏 1本指: カメラ回転（pan）
🤏 2本指: ピンチズーム
👆 タップ: メッシュグラブ
✋ 長押し: 球コライダー移動
```

#### **技術実装内容**
1. **入力システム**
   ```javascript
   // input-touch.js
   canvas.addEventListener('touchstart', handleTouchStart);
   canvas.addEventListener('touchmove', handleTouchMove);
   
   function handleTouchStart(e) {
       const touches = e.touches;
       if (touches.length === 1) startCameraRotate();
       else if (touches.length === 2) startPinchZoom();
   }
   ```

2. **UI適応**
   - Physics Panelをモバイル向けレイアウト
   - タッチ操作ガイド表示
   - バーチャルボタン（球追加等）

3. **パフォーマンス調整**
   - デフォルトサブステップ数: 10 → 6
   - テクスチャサイズ制限
   - メモリ使用量監視

**Phase 3 成果物:** スマホでの直感的物理シミュレーション操作

---

### **Phase 4: VR/ARブラウザ対応**

#### **WebXR統合**
- **既存アドバンテージ**: `FullSphereCamera.h`のAR対応済み
- **対応ヘッドセット**: Meta Quest, Vision Pro, Hololens

#### **技術実装内容**
1. **WebXRセッション管理**
   ```javascript
   // input-xr.js  
   const session = await navigator.xr.requestSession('immersive-vr');
   session.requestAnimationFrame(renderXR);
   
   function renderXR(time, frame) {
       const pose = frame.getViewerPose(referenceSpace);
       for (const view of pose.views) {
           // 左右眼それぞれでcore.jsの描画関数を呼ぶ
           core.drawMesh(view.projectionMatrix, view.transform);
       }
   }
   ```

2. **コントローラー操作**
   - トリガー：メッシュグラブ
   - スティック：球コライダー移動  
   - ボタン：Physics Panel（3D UI）

3. **C++カメラ統合**
   ```javascript
   // FullSphereCamera.h のsetIntrinsics使用
   const fx = view.projectionMatrix[0] * canvas.width / 2;
   const fy = view.projectionMatrix[5] * canvas.height / 2;
   core.setCameraIntrinsics(fx, fy, canvas.width/2, canvas.height/2);
   ```

**Phase 4 成果物:** VR/AR空間での3D物理シミュレーション

---

## ⚖️ Phase間の互換性

### **完全共通（100%流用）**
- ✅ **WASM物理エンジン** - プラットフォーム非依存
- ✅ **WebGL描画** - シェーダー・バッファ完全共通
- ✅ **GLB処理** - 四面体化・テクスチャ
- ✅ **Core.js** - レンダリング関数群

### **プラットフォーム別実装**
- ❌ **入力システム** - mouse/touch/XRInputSource
- ❌ **レンダリングループ** - requestAnimationFrame vs XRSession
- ❌ **UI表示** - DOM vs WebXR Layers

### **段階的移行戦略**
```
Phase 1 → 現状維持 + モジュール分割
Phase 2 → input-pc.js の改良のみ
Phase 3 → input-touch.js 追加
Phase 4 → input-xr.js 追加
```

---

## 🎯 優先度付きロードマップ

### **最高優先（今すぐ）**
- **Phase 1**: JSリファクタリング
  - 現状の1400行スクリプトをモジュール分割
  - 拡張性確保・メンテナンス性向上

### **高優先（短期）**
- **Phase 2**: PC操作性向上
  - ドラッグ&ドロップGLB読み込み
  - より詳細なパフォーマンス調整

### **中優先（中期）**
- **Phase 3**: スマホ対応
  - タッチ操作実装
  - モバイル最適化UI

### **将来検討（長期）**
- **Phase 4**: VR/AR対応
  - WebXR実装
  - 3D空間UI

---

## 💡 推奨着手順序

**1. Phase 1リファクタリング完了**
   → 現状機能100%維持しながら将来対応

**2. デプロイ・テスト**
   → リファクタリング後の安定動作確認

**3. Phase 2/3選択**
   → 需要に応じてPC強化 or スマホ対応

**4. 継続改善**
   → ユーザーフィードバック反映

---

## 📈 期待される成果

**Phase 1完了後:**
- **保守性**: モジュール分割によるメンテナンス効率化
- **拡張性**: 新プラットフォーム対応の基盤完成

**Phase 2完了後:**  
- **PC専用機能**: プロフェッショナル級操作性

**Phase 3完了後:**
- **ユーザーベース拡大**: スマホからのアクセス対応

**Phase 4完了後:**
- **次世代体験**: VR/AR空間での3D物理シミュレーション

---

## ✨ 結論

**現在のVercel成功版を基準として、Phase 1のリファクタリングに着手するのが最適解です。**

**既存機能を一切損なうことなく、将来のマルチプラットフォーム対応基盤を整備できます。**

**Phase 1から始めますか？**