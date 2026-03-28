#include "GlbLoader.h"
#include <iostream>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static bool CustomLoadImageData(tinygltf::Image* image,
                                const int,
                                std::string* err,
                                std::string*,
                                int, int,
                                const unsigned char* bytes,
                                int size,
                                void*)
{
    int w, h, comp;
    unsigned char* data = stbi_load_from_memory(bytes, size, &w, &h, &comp, 0);
    if (!data) {
        if (err) *err = "stbi_load_from_memory failed";
        return false;
    }
    image->width     = w;
    image->height    = h;
    image->component = comp;
    image->image.assign(data, data + w * h * comp);
    stbi_image_free(data);
    return true;
}

static bool CustomFreeImageData(tinygltf::Image*, void*) { return true; }

GlbResult GlbLoader::load(const std::string& filePath) {
    GlbResult result;

    tinygltf::Model    model;
    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(CustomLoadImageData, nullptr);

    std::string err, warn;
    bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, filePath);
    if (!warn.empty()) std::cerr << "[GlbLoader] Warning: " << warn << std::endl;
    if (!err.empty())  std::cerr << "[GlbLoader] Error: "   << err  << std::endl;
    if (!ok) {
        std::cerr << "[GlbLoader] Failed to load: " << filePath << std::endl;
        return result;
    }

    SoftBodyPhysics::MeshData& md = result.meshData;

    for (const auto& gltfMesh : model.meshes) {
        for (const auto& prim : gltfMesh.primitives) {

            size_t vertexOffset = md.verts.size() / 3;

            if (prim.attributes.count("POSITION")) {
                const auto& acc = model.accessors[prim.attributes.at("POSITION")];
                const auto& bv  = model.bufferViews[acc.bufferView];
                const auto& buf = model.buffers[bv.buffer];
                const float* p  = reinterpret_cast<const float*>(
                    &buf.data[bv.byteOffset + acc.byteOffset]);
                for (size_t i = 0; i < acc.count; ++i) {
                    md.verts.push_back(p[i*3]);
                    md.verts.push_back(p[i*3+1]);
                    md.verts.push_back(p[i*3+2]);
                }
            }

            if (prim.attributes.count("TEXCOORD_0")) {
                const auto& acc = model.accessors[prim.attributes.at("TEXCOORD_0")];
                const auto& bv  = model.bufferViews[acc.bufferView];
                const auto& buf = model.buffers[bv.buffer];
                const float* uv = reinterpret_cast<const float*>(
                    &buf.data[bv.byteOffset + acc.byteOffset]);
                for (size_t i = 0; i < acc.count; ++i) {
                    md.uvs.push_back(uv[i*2]);
                    md.uvs.push_back(uv[i*2+1]);
                }
            } else {
                size_t newVerts = md.verts.size() / 3 - vertexOffset;
                for (size_t i = 0; i < newVerts; ++i) {
                    md.uvs.push_back(0.0f);
                    md.uvs.push_back(0.0f);
                }
            }

            if (prim.indices >= 0) {
                const auto& acc = model.accessors[prim.indices];
                const auto& bv  = model.bufferViews[acc.bufferView];
                const auto& buf = model.buffers[bv.buffer];
                if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    const uint16_t* idx = reinterpret_cast<const uint16_t*>(
                        &buf.data[bv.byteOffset + acc.byteOffset]);
                    for (size_t i = 0; i < acc.count; ++i)
                        md.tetSurfaceTriIds.push_back((int)(idx[i] + vertexOffset));
                } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                    const uint32_t* idx = reinterpret_cast<const uint32_t*>(
                        &buf.data[bv.byteOffset + acc.byteOffset]);
                    for (size_t i = 0; i < acc.count; ++i)
                        md.tetSurfaceTriIds.push_back((int)(idx[i] + vertexOffset));
                }
            }

            if (!result.hasTexture && prim.material >= 0 &&
                prim.material < (int)model.materials.size())
            {
                const auto& mat = model.materials[prim.material];
                int texIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
                if (texIdx >= 0 && texIdx < (int)model.textures.size()) {
                    const auto& img = model.images[model.textures[texIdx].source];
                    if (!img.image.empty()) {
                        if (img.component == 4) {
                            result.pixels.resize(img.width * img.height * 3);
                            for (int px = 0; px < img.width * img.height; ++px) {
                                result.pixels[px*3]   = img.image[px*4];
                                result.pixels[px*3+1] = img.image[px*4+1];
                                result.pixels[px*3+2] = img.image[px*4+2];
                            }
                            result.textureChannels = 3;
                        } else {
                            result.pixels = img.image;
                            result.textureChannels = img.component;
                        }
                        result.textureWidth  = img.width;
                        result.textureHeight = img.height;
                        result.hasTexture    = true;
                    }
                }
            }
        }
    }

    std::cout << "[GlbLoader] Loaded: " << filePath << std::endl;
    std::cout << "  verts="  << md.verts.size() / 3
              << " tris="    << md.tetSurfaceTriIds.size() / 3
              << " uvs="     << md.uvs.size() / 2
              << " texture=" << (result.hasTexture ? "yes" : "no") << std::endl;

    return result;
}
