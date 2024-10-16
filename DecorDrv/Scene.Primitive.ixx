﻿module;

#include <D3D11.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <map>

#define STRIP_BREAK static_cast<uint32_t>(-1)

export module Scene.Primitive;

import Scene.IRenderingContext;
import Scene.TangentCalculator;
import Scene.GltfUtils;
import Scene.Utils;
import Scene.Log;
import TinyGltf;

using DirectX::XMFLOAT2;
using DirectX::XMFLOAT3;
using DirectX::XMFLOAT4;

export class ScenePrimitive : public ITangentCalculable
{
    struct SceneVertex
    {
        XMFLOAT3 Pos;
        XMFLOAT3 Normal;
        XMFLOAT4 Tangent; // w represents handedness of the tangent basis and is either 1 or -1
        XMFLOAT2 Tex;
    };

public:

    ScenePrimitive()
    {}

    ScenePrimitive(const ScenePrimitive& src) :
        mVertices(src.mVertices),
        mIndices(src.mIndices),
        mTopology(src.mTopology),
        mIsTangentPresent(src.mIsTangentPresent),
        mVertexBuffer(src.mVertexBuffer),
        mIndexBuffer(src.mIndexBuffer),
        mMaterialIdx(src.mMaterialIdx)
    {
        // We are creating new references of device resources
        SceneUtils::SafeAddRef(mVertexBuffer);
        SceneUtils::SafeAddRef(mIndexBuffer);
    }

    ScenePrimitive(ScenePrimitive&& src) :
        mVertices(std::move(src.mVertices)),
        mIndices(std::move(src.mIndices)),
        mIsTangentPresent(SceneUtils::Exchange(src.mIsTangentPresent, false)),
        mTopology(SceneUtils::Exchange(src.mTopology, D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED)),
        mVertexBuffer(SceneUtils::Exchange(src.mVertexBuffer, nullptr)),
        mIndexBuffer(SceneUtils::Exchange(src.mIndexBuffer, nullptr)),
        mMaterialIdx(SceneUtils::Exchange(src.mMaterialIdx, -1))
    {}

    ~ScenePrimitive()
    {
        Destroy();
    }

    ScenePrimitive& operator = (const ScenePrimitive& src)
    {
        mVertices = src.mVertices;
        mIndices = src.mIndices;
        mIsTangentPresent = src.mIsTangentPresent;
        mTopology = src.mTopology;
        mVertexBuffer = src.mVertexBuffer;
        mIndexBuffer = src.mIndexBuffer;

        // We are creating new references of device resources
        SceneUtils::SafeAddRef(mVertexBuffer);
        SceneUtils::SafeAddRef(mIndexBuffer);

        mMaterialIdx = src.mMaterialIdx;

        return *this;
    }

    ScenePrimitive& operator = (ScenePrimitive&& src)
    {
        mVertices = std::move(src.mVertices);
        mIndices = std::move(src.mIndices);
        mIsTangentPresent = SceneUtils::Exchange(src.mIsTangentPresent, false);
        mTopology = SceneUtils::Exchange(src.mTopology, D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
        mVertexBuffer = SceneUtils::Exchange(src.mVertexBuffer, nullptr);
        mIndexBuffer = SceneUtils::Exchange(src.mIndexBuffer, nullptr);

        mMaterialIdx = SceneUtils::Exchange(src.mMaterialIdx, -1);

        return *this;
    }

    bool LoadFromGLTF(IRenderingContext& ctx,
        const tinygltf::Model& model,
        const tinygltf::Mesh& mesh,
        const int primitiveIdx,
        const std::wstring& logPrefix)
    {
        if (!LoadDataFromGLTF(model, mesh, primitiveIdx, logPrefix))
            return false;
        //throw new std::exception("Failed to load data from GLTF!");
        if (!CreateDeviceBuffers(ctx))
            return false;
        //throw new std::exception("Failed to create device buffers!");

        return true;
    }

    // Uses mikktspace tangent space calculator by Morten S. Mikkelsen.
    // Requires position, normal, and texture coordinates to be already loaded.
    bool CalculateTangentsIfNeeded(const std::wstring& logPrefix = std::wstring())
    {
        // TODO: if (material needs tangents && are not present) ... GetMaterial()
        // TODO: Requires position, normal, and texcoords
        // TODO: Only for triangles?
        if (!IsTangentPresent())
        {
            SceneLog::Debug(L"%sComputing tangents...", logPrefix.c_str());

            if (!TangentCalculator::Calculate(*this))
            {
                SceneLog::Error(L"%sTangents computation failed!", logPrefix.c_str());
                return false;
            }
            mIsTangentPresent = true;
        }

        return true;
    }

    size_t GetVerticesPerFace() const override
    {
        switch (mTopology)
        {
        case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
            return 1;
        case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
            return 2;
        case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
            return 2;
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
            return 3;
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
            return 3;
        default:
            return 0;
        }
    }

    size_t GetFacesCount() const override
    {
        FillFaceStripsCacheIfNeeded();

        switch (mTopology)
        {
        case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
        case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
            return mIndices.size() / GetVerticesPerFace();

        case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
            return mFaceStripsTotalCount;

        default:
            return 0; // Unsupported
        }
    }

    const size_t GetVertexIndex(const int face, const int vertex) const
    {
        if (vertex >= GetVerticesPerFace())
            return 0;

        switch (mTopology)
        {
        case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
        case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
        case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
            return vertex;

        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
        {
            const bool isOdd = (face % 2 == 1);

            if (isOdd && (vertex == 1))
                return 2;
            else if (isOdd && (vertex == 2))
                return 1;
            else
                return vertex;
        }

        default:
            return 0; // Unsupported
        }
    }    

    void GetPosition(float outpos[], const int face, const int vertex) const override
    {
        const auto& pos = GetConstVertex(face, vertex).Pos;
        outpos[0] = pos.x;
        outpos[1] = pos.y;
        outpos[2] = pos.z;
    }

    void GetNormal(float outnormal[], const int face, const int vertex) const override
    {
        const auto& normal = GetConstVertex(face, vertex).Normal;
        outnormal[0] = normal.x;
        outnormal[1] = normal.y;
        outnormal[2] = normal.z;
    }

    void GetTextCoord(float outuv[], const int face, const int vertex) const override
    {
        const auto& tex = GetConstVertex(face, vertex).Tex;
        outuv[0] = tex.x;
        outuv[1] = tex.y;
    }

    void SetTangent(const float intangent[], const float sign, const int face, const int vertex) override
    {
        auto& tangent = GetVertex(face, vertex).Tangent;
        tangent.x = intangent[0];
        tangent.y = intangent[1];
        tangent.z = intangent[2];
        tangent.w = sign;
    }

    bool IsTangentPresent() const { return mIsTangentPresent; }

    void DrawGeometry(IRenderingContext& ctx, ID3D11InputLayout* vertexLayout) const
    {
        auto& deviceContext = ctx.GetDeviceContext();

        deviceContext.IASetInputLayout(vertexLayout);
        UINT stride = sizeof(SceneVertex);
        UINT offset = 0;
        deviceContext.IASetVertexBuffers(0, 1, &mVertexBuffer, &stride, &offset);
        deviceContext.IASetIndexBuffer(mIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
        deviceContext.IASetPrimitiveTopology(mTopology);

        deviceContext.DrawIndexed((UINT)mIndices.size(), 0, 0);
    }

    void SetMaterialIdx(int idx) { mMaterialIdx = idx; };

    int GetMaterialIdx() const { return mMaterialIdx; };

    void Destroy()
    {
        DestroyGeomData();
        DestroyDeviceBuffers();
    }

private:

    const SceneVertex& GetConstVertex(const int face, const int vertex) const
    {
        static const SceneVertex invalidVert{};

        FillFaceStripsCacheIfNeeded();

        if (vertex >= GetVerticesPerFace())
            return invalidVert;

        switch (mTopology)
        {
        case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
        case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
        {
            const auto idx = face * GetVerticesPerFace() + vertex;
            if ((idx < 0) || (idx >= mIndices.size()))
                return invalidVert;
            return mVertices[mIndices[idx]];
        }

        case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
        {
            if (!mAreFaceStripsCached)
                return invalidVert;
            if (face >= mFaceStripsTotalCount)
                return invalidVert;

            // Strip
            // (naive impl for now, could be done in log time using cumulative counts and binary search)
            size_t strip = 0;
            size_t skippedFaces = 0;
            for (; strip < mFaceStrips.size(); strip++)
            {
                const auto currentFaceCount = mFaceStrips[strip].faceCount;
                if (face < (skippedFaces + currentFaceCount))
                    break; // found
                skippedFaces += currentFaceCount;
            }
            if (strip >= mFaceStrips.size())
                return invalidVert;

            // Face & vertex
            const auto faceIdx = face - skippedFaces;
            const auto vertexIdx = GetVertexIndex((int)faceIdx, vertex);
            const auto idx = mFaceStrips[strip].startIdx + faceIdx + vertexIdx;
            if ((idx < 0) || (idx >= mIndices.size()))
                return invalidVert;
            return mVertices[mIndices[idx]];
        }

        default:
            return invalidVert; // Unsupported
        }
    }

    SceneVertex& GetVertex(const int face, const int vertex)
    {
        return
            const_cast<SceneVertex&>(
                static_cast<const ScenePrimitive&>(*this).
                GetConstVertex(face, vertex));
    }

    const tinygltf::Accessor& GetPrimitiveAttrAccessor(bool& accessorLoaded,
        const tinygltf::Model& model,
        const std::map<std::string, int>& attributes,
        const int primitiveIdx,
        bool requiredData,
        const std::string& attrName,
        const std::wstring& logPrefix)
    {
        static tinygltf::Accessor dummyAccessor;

        const auto attrIt = attributes.find(attrName);
        if (attrIt == attributes.end())
        {
            SceneLog::Write(requiredData,
                L"%sNo %s attribute present in primitive %d!",
                logPrefix.c_str(),
                SceneUtils::StringToWstring(attrName).c_str(),
                primitiveIdx);
            accessorLoaded = false;
            return dummyAccessor;
        }

        const auto accessorIdx = attrIt->second;
        if ((accessorIdx < 0) || (accessorIdx >= model.accessors.size()))
        {
            SceneLog::Error(L"%sInvalid %s accessor index (%d/%d)!",
                logPrefix.c_str(),
                SceneUtils::StringToWstring(attrName).c_str(),
                accessorIdx,
                model.accessors.size());
            accessorLoaded = false;
            return dummyAccessor;
        }

        accessorLoaded = true;
        return model.accessors[accessorIdx];
    }

    template <typename ComponentType,
        size_t ComponentCount,
        typename TDataConsumer>
    bool IterateGltfAccesorData(const tinygltf::Model& model,
        const tinygltf::Accessor& accessor,
        TDataConsumer DataConsumer,
        const wchar_t* logPrefix,
        const wchar_t* logDataName)
    {
        SceneLog::Debug(L"%s%s accesor \"%s\": view %d, offset %d, type %s<%s>, count %d",
            logPrefix,
            logDataName,
            SceneUtils::StringToWstring(accessor.name).c_str(),
            accessor.bufferView,
            accessor.byteOffset,
            GltfUtils::TypeToWstring(accessor.type).c_str(),
            GltfUtils::ComponentTypeToWstring(accessor.componentType).c_str(),
            accessor.count);

        // Buffer view
        const auto bufferViewIdx = accessor.bufferView;

        if ((bufferViewIdx < 0) || (bufferViewIdx >= model.bufferViews.size()))
        {
            SceneLog::Error(L"%sInvalid %s view buffer index (%d/%d)!",
                logPrefix, logDataName, bufferViewIdx, model.bufferViews.size());
            return false;
        }

        const auto& bufferView = model.bufferViews[bufferViewIdx];

        //Log::Debug(L"%s%s buffer view %d \"%s\": buffer %d, offset %d, length %d, stride %d, target %s",
        //           logPrefix,
        //           logDataName,
        //           bufferViewIdx,
        //           Utils::StringToWstring(bufferView.name).c_str(),
        //           bufferView.buffer,
        //           bufferView.byteOffset,
        //           bufferView.byteLength,
        //           bufferView.byteStride,
        //           GltfUtils::TargetToWstring(bufferView.target).c_str());

        // Buffer

        const auto bufferIdx = bufferView.buffer;

        if ((bufferIdx < 0) || (bufferIdx >= model.buffers.size()))
        {
            SceneLog::Error(L"%sInvalid %s buffer index (%d/%d)!",
                logPrefix, logDataName, bufferIdx, model.buffers.size());
            return false;
        }

        const auto& buffer = model.buffers[bufferIdx];

        const auto byteEnd = bufferView.byteOffset + bufferView.byteLength;
        if (byteEnd > buffer.data.size())
        {
            SceneLog::Error(L"%sAccessing data chunk outside %s buffer %d!",
                logPrefix, logDataName, bufferIdx);
            return false;
        }

        //Log::Debug(L"%s%s buffer %d \"%s\": data %x, size %d, uri \"%s\"",
        //           logPrefix,
        //           logDataName,
        //           bufferIdx,
        //           Utils::StringToWstring(buffer.name).c_str(),
        //           buffer.data.data(),
        //           buffer.data.size(),
        //           Utils::StringToWstring(buffer.uri).c_str());

        // TODO: Check that buffer view is large enough to contain all data from accessor?

        // Data

        const auto componentSize = sizeof(ComponentType);
        const auto typeSize = ComponentCount * componentSize;
        const auto stride = bufferView.byteStride;
        const auto typeOffset = (stride == 0) ? typeSize : stride;

        auto ptr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
        int idx = 0;
        for (; idx < accessor.count; ++idx, ptr += typeOffset)
            DataConsumer(idx, ptr);

        return true;
    }

    bool LoadDataFromGLTF(const tinygltf::Model& model,
                          const tinygltf::Mesh& mesh,
                          const int primitiveIdx,
                          const std::wstring& logPrefix)
    {        
        bool success = false;
        const auto& primitive = mesh.primitives[primitiveIdx];
        const auto& attrs = primitive.attributes;
        const auto subItemsLogPrefix = logPrefix + L"   ";
        const auto dataConsumerLogPrefix = subItemsLogPrefix + L"   ";

        SceneLog::Debug(L"%sPrimitive %d/%d: mode %s, attributes [%s], indices %d, material %d",
            logPrefix.c_str(),
            primitiveIdx,
            mesh.primitives.size(),
            GltfUtils::ModeToWstring(primitive.mode).c_str(),
            GltfUtils::StringIntMapToWstring(primitive.attributes).c_str(),
            primitive.indices,
            primitive.material);

        // Positions
        auto& posAccessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
            true, "POSITION", subItemsLogPrefix.c_str());
        if (!success)
            return false;

        if ((posAccessor.componentType != tinygltf::COMPONENT_TYPE_FLOAT) ||
            (posAccessor.type != tinygltf::TYPE_VEC3))
        {
            SceneLog::Error(L"%sUnsupported POSITION data type!", subItemsLogPrefix.c_str());
            return false;
        }

        mVertices.clear();
        mVertices.reserve(posAccessor.count);
        if (mVertices.capacity() < posAccessor.count)
        {
            SceneLog::Error(L"%sUnable to allocate %d vertices!", subItemsLogPrefix.c_str(), posAccessor.count);
            mVertices.clear();
            return false;
        }

        auto PositionDataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char* ptr)
            {
                auto pos = *reinterpret_cast<const XMFLOAT3*>(ptr);

                itemIdx; // unused param
                //Log::Debug(L"%s%d: pos [%.4f, %.4f, %.4f]",
                //           dataConsumerLogPrefix.c_str(),
                //           itemIdx,
                //           pos.x, pos.y, pos.z);

                mVertices.push_back(SceneVertex{ XMFLOAT3(pos.x, pos.y, pos.z),
                                                 XMFLOAT3(0.0f, 0.0f, 1.0f), // TODO: Leave invalid?
                                                 XMFLOAT4(1.0f, 0.5f, 0.0f, 1.0f),  // debug; TODO: Leave invalid?
                                                 XMFLOAT2(0.0f, 0.0f) });
            };

        if (!IterateGltfAccesorData<float, 3>(model,
            posAccessor,
            PositionDataConsumer,
            subItemsLogPrefix.c_str(),
            L"Position"))
            return false;

        // Normals
        auto& normalAccessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
            false, "NORMAL", subItemsLogPrefix.c_str());
        if (success)
        {
            if ((normalAccessor.componentType != tinygltf::COMPONENT_TYPE_FLOAT) ||
                (normalAccessor.type != tinygltf::TYPE_VEC3))
            {
                SceneLog::Error(L"%sUnsupported NORMAL data type!", subItemsLogPrefix.c_str());
                return false;
            }

            if (normalAccessor.count != posAccessor.count)
            {
                SceneLog::Error(L"%sNormals count (%d) is different from position count (%d)!",
                    subItemsLogPrefix.c_str(), normalAccessor.count, posAccessor.count);
                return false;
            }

            auto NormalDataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char* ptr)
                {
                    auto normal = *reinterpret_cast<const XMFLOAT3*>(ptr);

                    //Log::Debug(L"%s%d: normal [%.4f, %.4f, %.4f]",
                    //           dataConsumerLogPrefix.c_str(),
                    //           itemIdx, normal.x, normal.y, normal.z);

                    mVertices[itemIdx].Normal = normal;
                };

            if (!IterateGltfAccesorData<float, 3>(model,
                normalAccessor,
                NormalDataConsumer,
                subItemsLogPrefix.c_str(),
                L"Normal"))
                return false;
        }
        //else
        //{
        //    // No normals provided
        //    // TODO: Generate?
        //}

        // Tangents
        auto& tangentAccessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
            false, "TANGENT", subItemsLogPrefix.c_str());
        if (success)
        {
            if ((tangentAccessor.componentType != tinygltf::COMPONENT_TYPE_FLOAT) ||
                (tangentAccessor.type != tinygltf::TYPE_VEC4))
            {
                SceneLog::Error(L"%sUnsupported TANGENT data type!", subItemsLogPrefix.c_str());
                return false;
            }

            if (tangentAccessor.count != posAccessor.count)
            {
                SceneLog::Error(L"%sTangents count (%d) is different from position count (%d)!",
                    subItemsLogPrefix.c_str(), tangentAccessor.count, posAccessor.count);
                return false;
            }

            auto TangentDataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char* ptr)
                {
                    auto tangent = *reinterpret_cast<const XMFLOAT4*>(ptr);

                    //Log::Debug(L"%s%d: tangent [%7.4f, %7.4f, %7.4f] * %.1f",
                    //           dataConsumerLogPrefix.c_str(), itemIdx,
                    //           tangent.x, tangent.y, tangent.z, tangent.w);

                    if ((tangent.w != 1.f) && (tangent.w != -1.f))
                        SceneLog::Warning(L"%s%d: tangent w component (handedness) is not equal to 1 or -1 but to %7.4f",
                            dataConsumerLogPrefix.c_str(), itemIdx, tangent.w);

                    mVertices[itemIdx].Tangent = tangent;
                };

            if (!IterateGltfAccesorData<float, 4>(model,
                tangentAccessor,
                TangentDataConsumer,
                subItemsLogPrefix.c_str(),
                L"Tangent"))
                return false;

            mIsTangentPresent = true;
        }
        else
        {
            SceneLog::Debug(L"%sTangents are not present", subItemsLogPrefix.c_str());
        }

        // Texture coordinates
        auto& texCoord0Accessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
            false, "TEXCOORD_0", subItemsLogPrefix.c_str());
        if (success)
        {
            if ((texCoord0Accessor.componentType != tinygltf::COMPONENT_TYPE_FLOAT) ||
                (texCoord0Accessor.type != tinygltf::TYPE_VEC2))
            {
                SceneLog::Error(L"%sUnsupported TEXCOORD_0 data type!", subItemsLogPrefix.c_str());
                return false;
            }

            if (texCoord0Accessor.count != posAccessor.count)
            {
                SceneLog::Error(L"%sTexture coords count (%d) is different from position count (%d)!",
                    subItemsLogPrefix.c_str(), texCoord0Accessor.count, posAccessor.count);
                return false;
            }

            auto TexCoord0DataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char* ptr)
                {
                    auto texCoord0 = *reinterpret_cast<const XMFLOAT2*>(ptr);

                    //Log::Debug(L"%s%d: texCoord0 [%.1f, %.1f]",
                    //           dataConsumerLogPrefix.c_str(), itemIdx, texCoord0.x, texCoord0.y);

                    mVertices[itemIdx].Tex = texCoord0;
                };

            if (!IterateGltfAccesorData<float, 2>(model,
                texCoord0Accessor,
                TexCoord0DataConsumer,
                subItemsLogPrefix.c_str(),
                L"Texture coordinates"))
                return false;
        }

        // Indices
        const auto indicesAccessorIdx = primitive.indices;
        if (indicesAccessorIdx >= model.accessors.size())
        {
            SceneLog::Error(L"%sInvalid indices accessor index (%d/%d)!",
                subItemsLogPrefix.c_str(), indicesAccessorIdx, model.accessors.size());
            return false;
        }
        if (indicesAccessorIdx < 0)
        {
            SceneLog::Error(L"%sNon-indexed geometry is not supported!", subItemsLogPrefix.c_str());
            return false;
        }

        const auto& indicesAccessor = model.accessors[indicesAccessorIdx];

        if (indicesAccessor.type != tinygltf::TYPE_SCALAR)
        {
            SceneLog::Error(L"%sUnsupported indices data type (must be scalar)!", subItemsLogPrefix.c_str());
            return false;
        }
        if ((indicesAccessor.componentType < tinygltf::COMPONENT_TYPE_BYTE) ||
            (indicesAccessor.componentType > tinygltf::COMPONENT_TYPE_UNSIGNED_INT))
        {
            SceneLog::Error(L"%sUnsupported indices data component type (%d)!",
                subItemsLogPrefix.c_str(), indicesAccessor.componentType);
            return false;
        }

        mIndices.clear();
        mIndices.reserve(indicesAccessor.count);
        if (mIndices.capacity() < indicesAccessor.count)
        {
            SceneLog::Error(L"%sUnable to allocate %d indices!", subItemsLogPrefix.c_str(), indicesAccessor.count);
            return false;
        }

        const auto indicesComponentType = indicesAccessor.componentType;
        auto IndexDataConsumer =
            [this, &dataConsumerLogPrefix, indicesComponentType]
            (int itemIdx, const unsigned char* ptr)
            {
                switch (indicesComponentType)
                {
                case tinygltf::COMPONENT_TYPE_BYTE:              mIndices.push_back(*reinterpret_cast<const int8_t*>(ptr)); break;
                case tinygltf::COMPONENT_TYPE_UNSIGNED_BYTE:     mIndices.push_back(*reinterpret_cast<const uint8_t*>(ptr)); break;
                case tinygltf::COMPONENT_TYPE_SHORT:             mIndices.push_back(*reinterpret_cast<const int16_t*>(ptr)); break;
                case tinygltf::COMPONENT_TYPE_UNSIGNED_SHORT:    mIndices.push_back(*reinterpret_cast<const uint16_t*>(ptr)); break;
                case tinygltf::COMPONENT_TYPE_INT:               mIndices.push_back(*reinterpret_cast<const int32_t*>(ptr)); break;
                case tinygltf::COMPONENT_TYPE_UNSIGNED_INT:      mIndices.push_back(*reinterpret_cast<const uint32_t*>(ptr)); break;
                }

                // debug
                itemIdx; // unused param
                //Log::Debug(L"%s%d: %d",
                //           dataConsumerLogPrefix.c_str(),
                //           itemIdx,
                //           mIndices.back());
            };

        // TODO: Wrap into IterateGltfAccesorData(componentType, ...)? std::forward()?
        switch (indicesComponentType)
        {
        case tinygltf::COMPONENT_TYPE_BYTE:
            IterateGltfAccesorData<const int8_t, 1>(model,
                indicesAccessor,
                IndexDataConsumer,
                subItemsLogPrefix.c_str(),
                L"Indices");
            break;
        case tinygltf::COMPONENT_TYPE_UNSIGNED_BYTE:
            IterateGltfAccesorData<uint8_t, 1>(model,
                indicesAccessor,
                IndexDataConsumer,
                subItemsLogPrefix.c_str(),
                L"Indices");
            break;
        case tinygltf::COMPONENT_TYPE_SHORT:
            IterateGltfAccesorData<int16_t, 1>(model,
                indicesAccessor,
                IndexDataConsumer,
                subItemsLogPrefix.c_str(),
                L"Indices");
            break;
        case tinygltf::COMPONENT_TYPE_UNSIGNED_SHORT:
            IterateGltfAccesorData<uint16_t, 1>(model,
                indicesAccessor,
                IndexDataConsumer,
                subItemsLogPrefix.c_str(),
                L"Indices");
            break;
        case tinygltf::COMPONENT_TYPE_INT:
            IterateGltfAccesorData<int32_t, 1>(model,
                indicesAccessor,
                IndexDataConsumer,
                subItemsLogPrefix.c_str(),
                L"Indices");
            break;
        case tinygltf::COMPONENT_TYPE_UNSIGNED_INT:
            IterateGltfAccesorData<uint32_t, 1>(model,
                indicesAccessor,
                IndexDataConsumer,
                subItemsLogPrefix.c_str(),
                L"Indices");
            break;
        }
        if (mIndices.size() != indicesAccessor.count)
        {
            SceneLog::Error(L"%sFailed to load indices (loaded %d instead of %d))!",
                subItemsLogPrefix.c_str(), mIndices.size(), indicesAccessor.count);
            return false;
        }

        // DX primitive topology
        mTopology = GltfUtils::ModeToTopology(primitive.mode);
        if (mTopology == D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED)
        {
            SceneLog::Error(L"%sUnsupported primitive topology!", subItemsLogPrefix.c_str());
            return false;
        }

        // Material
        const auto matIdx = primitive.material;
        if (matIdx >= 0)
        {
            if (matIdx >= model.materials.size())
            {
                SceneLog::Error(L"%sInvalid material index (%d/%d)!",
                    subItemsLogPrefix.c_str(), matIdx, model.materials.size());
                return false;
            }

            mMaterialIdx = matIdx;
        }

        // reverse indices
        std::reverse(mIndices.begin(), mIndices.end());

#ifndef SIMPLE_RENDER_MODE
        CalculateTangentsIfNeeded(subItemsLogPrefix);
#endif // !1

        return true;
    };

    void FillFaceStripsCacheIfNeeded() const
    {
        if (mAreFaceStripsCached)
            return;

        switch (mTopology)
        {
        case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
        {
            mFaceStrips.clear();

            const auto count = mIndices.size();
            for (size_t i = 0; i < count; )
            {
                // Start
                while ((i < count) && (mIndices[i] == STRIP_BREAK))
                {
                    ++i;
                }
                const size_t start = i;

                // Length
                size_t length = 0;
                while ((i < count) && (mIndices[i] != STRIP_BREAK))
                {
                    ++length;
                    ++i;
                }

                // Strip
                if (length >= GetVerticesPerFace())
                {
                    const auto faceCount = length - (GetVerticesPerFace() - 1);
                    mFaceStrips.push_back({ start, faceCount });
                }
            }

            mFaceStripsTotalCount = 0;
            for (const auto& strip : mFaceStrips)
                mFaceStripsTotalCount += strip.faceCount;

            mAreFaceStripsCached = true;
            return;
        }

        case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
        case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
        default:
            return;
        }
    }

    bool CreateDeviceBuffers(IRenderingContext& ctx)
    {
        DestroyDeviceBuffers();

        auto& device = ctx.GetDevice();
        assert(&device);

        HRESULT hr = S_OK;

        D3D11_BUFFER_DESC bd;
        ZeroMemory(&bd, sizeof(bd));

        D3D11_SUBRESOURCE_DATA initData;
        ZeroMemory(&initData, sizeof(initData));

        // Vertex buffer
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = (UINT)(sizeof(SceneVertex) * mVertices.size());
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = 0;
        initData.pSysMem = mVertices.data();
        hr = device.CreateBuffer(&bd, &initData, &mVertexBuffer);
        if (FAILED(hr))
        {
            DestroyDeviceBuffers();
            return false;
        }

        // Index buffer
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(uint32_t) * (UINT)mIndices.size();
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.CPUAccessFlags = 0;
        initData.pSysMem = mIndices.data();
        hr = device.CreateBuffer(&bd, &initData, &mIndexBuffer);
        if (FAILED(hr))
        {
            DestroyDeviceBuffers();
            return false;
        }

        return true;
    }

    void DestroyGeomData()
    {
        mVertices.clear();
        mIndices.clear();
        mTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    }

    void DestroyDeviceBuffers()
    {
        SceneUtils::ReleaseAndMakeNull(mVertexBuffer);
        SceneUtils::ReleaseAndMakeNull(mIndexBuffer);
    }

private:

    // Geometry data
    std::vector<SceneVertex>    mVertices;
    std::vector<uint32_t>       mIndices;
    D3D11_PRIMITIVE_TOPOLOGY    mTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    bool                        mIsTangentPresent = false;

    // Cached geometry data
    struct FaceStrip
    {
        size_t startIdx;
        size_t faceCount;
    };
    mutable bool                    mAreFaceStripsCached = false;
    mutable std::vector<FaceStrip>  mFaceStrips;
    mutable size_t                  mFaceStripsTotalCount = 0;

    // Device geometry data
    ID3D11Buffer* mVertexBuffer = nullptr;
    ID3D11Buffer* mIndexBuffer = nullptr;

    // Material
    int           mMaterialIdx = -1;
};