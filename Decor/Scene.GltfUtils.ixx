module;

#include <d3d11.h>
#include <DirectXMath.h>
#include <sstream>
#include <string>
#include <vector>
#include <map>

export module Scene.GltfUtils;

import Scene.Log;
import Scene.Utils;
import TinyGltf;

export namespace GltfUtils
{
    bool LoadModel(tinygltf::Model& model, const std::wstring& filePath)
    {
        using namespace std;

        // Convert to plain string for tinygltf
        string filePathA = Utils::WstringToString(filePath);

        tinygltf::TinyGLTF tinyGltf;
        string errA, warnA;
        wstring ext = Utils::GetFilePathExt(filePath);

        bool ret = false;
        if (ext.compare(L"glb") == 0)
        {
            Log::Debug(L"Gltf::LoadModel: Reading binary glTF from \"%s\"", filePath.c_str());
            ret = tinyGltf.LoadBinaryFromFile(&model, &errA, &warnA, filePathA);
        }
        else
        {
            Log::Debug(L"Gltf::LoadModel: Reading ASCII glTF from \"%s\"", filePath.c_str());
            ret = tinyGltf.LoadASCIIFromFile(&model, &errA, &warnA, filePathA);
        }

        if (!errA.empty())
            Log::Debug(L"Gltf::LoadModel: Error: %s", Utils::StringToWstring(errA).c_str());

        if (!warnA.empty())
            Log::Debug(L"Gltf::LoadModel: Warning: %s", Utils::StringToWstring(warnA).c_str());

        if (ret)
            Log::Debug(L"Gltf::LoadModel: Succesfully loaded model");
        else
            Log::Error(L"Gltf::LoadModel: Failed to parse glTF file \"%s\"", filePath.c_str());

        return ret;
    }

    D3D11_PRIMITIVE_TOPOLOGY ModeToTopology(int mode)
    {
        switch (mode)
        {
        case tinygltf::MODE_POINTS:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case tinygltf::MODE_LINE:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case tinygltf::MODE_LINE_STRIP:
            return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case tinygltf::MODE_TRIANGLES:
            return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case tinygltf::MODE_TRIANGLE_STRIP:
            return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            //case TINYGLTF_MODE_LINE_LOOP:
            //case TINYGLTF_MODE_TRIANGLE_FAN:
        default:
            return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }
    }

    std::wstring ModeToWstring(int mode)
    {
        if (mode == tinygltf::MODE_POINTS)
            return L"POINTS";
        else if (mode == tinygltf::MODE_LINE)
            return L"LINE";
        else if (mode == tinygltf::MODE_LINE_LOOP)
            return L"LINE_LOOP";
        else if (mode == tinygltf::MODE_TRIANGLES)
            return L"TRIANGLES";
        else if (mode == tinygltf::MODE_TRIANGLE_FAN)
            return L"TRIANGLE_FAN";
        else if (mode == tinygltf::MODE_TRIANGLE_STRIP)
            return L"TRIANGLE_STRIP";
        else
            return L"**UNKNOWN**";
    }

    std::wstring StringIntMapToWstring(const std::map<std::string, int>& m)
    {
        std::stringstream ss;
        bool first = true;
        for (auto item : m)
        {
            if (!first)
                ss << ", ";
            else
                first = false;
            ss << item.first << ": " << item.second;
        }
        return Utils::StringToWstring(ss.str());
    }

    std::wstring TypeToWstring(int ty)
    {
        if (ty == tinygltf::TYPE_SCALAR)
            return L"SCALAR";
        else if (ty == tinygltf::TYPE_VECTOR)
            return L"VECTOR";
        else if (ty == tinygltf::TYPE_VEC2)
            return L"VEC2";
        else if (ty == tinygltf::TYPE_VEC3)
            return L"VEC3";
        else if (ty == tinygltf::TYPE_VEC4)
            return L"VEC4";
        else if (ty == tinygltf::TYPE_MATRIX)
            return L"MATRIX";
        else if (ty == tinygltf::TYPE_MAT2)
            return L"MAT2";
        else if (ty == tinygltf::TYPE_MAT3)
            return L"MAT3";
        else if (ty == tinygltf::TYPE_MAT4)
            return L"MAT4";
        return L"**UNKNOWN**";
    }


    std::wstring ComponentTypeToWstring(int ty)
    {
        if (ty == tinygltf::COMPONENT_TYPE_BYTE)
            return L"BYTE";
        else if (ty == tinygltf::COMPONENT_TYPE_UNSIGNED_BYTE)
            return L"UNSIGNED_BYTE";
        else if (ty == tinygltf::COMPONENT_TYPE_SHORT)
            return L"SHORT";
        else if (ty == tinygltf::COMPONENT_TYPE_UNSIGNED_SHORT)
            return L"UNSIGNED_SHORT";
        else if (ty == tinygltf::COMPONENT_TYPE_INT)
            return L"INT";
        else if (ty == tinygltf::COMPONENT_TYPE_UNSIGNED_INT)
            return L"UNSIGNED_INT";
        else if (ty == tinygltf::COMPONENT_TYPE_FLOAT)
            return L"FLOAT";
        else if (ty == tinygltf::COMPONENT_TYPE_DOUBLE)
            return L"DOUBLE";

        return L"**UNKNOWN**";
    }

    std::wstring ColorToWstring(const DirectX::XMFLOAT4& color)
    {
        std::wstringstream ss;
        ss << L"[ ";
        ss << color.x << L", ";
        ss << color.y << L", ";
        ss << color.z << L", ";
        ss << color.w;
        ss << L" ]";

        return ss.str();
    }

    std::wstring FloatArrayToWstring(const std::vector<double>& arr)
    {
        if (arr.size() == 0)
            return L"";

        std::stringstream ss;
        ss << "[ ";
        for (size_t i = 0; i < arr.size(); i++)
            ss << arr[i] << ((i != arr.size() - 1) ? ", " : "");
        ss << " ]";

        return Utils::StringToWstring(ss.str());
    }

    std::wstring StringDoubleMapToWstring(const std::map<std::string, double>& mp)
    {
        if (mp.size() == 0)
            return L"";

        std::stringstream ss;
        ss << "[";
        bool first = true;
        for (auto& item : mp)
        {
            ss << (first ? " " : ", ");
            ss << item.first << ": " << item.second;
            first = false;
        }
        ss << " ]";

        return Utils::StringToWstring(ss.str());
    }

    std::wstring ParameterValueToWstring(const tinygltf::Parameter& param)
    {
        if (!param.number_array.empty())
            return FloatArrayToWstring(param.number_array);
        else if (!param.json_double_value.empty())
            return StringDoubleMapToWstring(param.json_double_value);
        else if (param.has_number_value)
        {
            std::wstringstream ss;
            ss << param.number_value;
            return ss.str();
        }
        else
            return Utils::StringToWstring("\"" + param.string_value + "\"");
    }

    bool FloatArrayToColor(DirectX::XMFLOAT4& color, const std::vector<double>& vector)
    {
        switch (vector.size())
        {
        case 4:
            color = DirectX::XMFLOAT4((float)vector[0],
                (float)vector[1],
                (float)vector[2],
                (float)vector[3]);
            return true;
        case 3:
            color = DirectX::XMFLOAT4((float)vector[0],
                (float)vector[1],
                (float)vector[2],
                1.0);
            return true;
        default:
            return false;
        }
    }

    template <int component>
    void FloatToColorComponent(DirectX::XMFLOAT4& color, double value)
    {
        static_assert(component < 4, "Incorrect color component index!");

        switch (component)
        {
        case 0: color.x = (float)value; break;
        case 1: color.y = (float)value; break;
        case 2: color.z = (float)value; break;
        case 3: color.w = (float)value; break;
        }
    }
}