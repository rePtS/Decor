#include "ShaderCompiler.h"
#include "Helpers.h"
#include <cassert>

ShaderCompiler::ShaderCompiler(ID3D11Device& Device, const wchar_t* const pszFileName)
:m_Device(Device)
,m_pszFileName(pszFileName)
{

}

void ShaderCompiler::CompileShader(const char* const pszEntryPoint, const char* const pszTarget)
{
    LOGMESSAGEF(L"Compiling \"%s\" for target %S.", m_pszFileName, pszTarget);

    UINT iFlags = 0;
#ifdef _DEBUG
    iFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> pMessages;

    const HRESULT hResult = D3DCompileFromFile(m_pszFileName, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, pszEntryPoint, pszTarget, iFlags, 0, &m_pBlob, &pMessages);

    if (pMessages)
    {
        LOGWARNINGF(L"Shader compilation:\r\n%S", static_cast<char*>(pMessages->GetBufferPointer()));
        OutputDebugStringA(static_cast<char*>(pMessages->GetBufferPointer()));
        DebugBreak();
    }

    Decor::ThrowIfFailed(hResult, "Failed to compile shader '%s'.", m_pszFileName);
}

template<class T, class Func>
ComPtr<T> ShaderCompiler::CompileXShader(const char* const pszEntryPoint, const char* const pszTarget, const Func& CreationFunc)
{
    CompileShader(pszEntryPoint, pszTarget);

    ComPtr<T> pShader;
    Decor::ThrowIfFailed(
        (m_Device.*CreationFunc)(m_pBlob->GetBufferPointer(), m_pBlob->GetBufferSize(), nullptr, &pShader), 
        "Failed to create shader '%s'.", m_pszFileName
    );
    Decor::SetResourceNameW(pShader, m_pszFileName);

    return pShader;
}

ComPtr<ID3D11VertexShader> ShaderCompiler::CompileVertexShader()
{
    return CompileXShader<ID3D11VertexShader>("VSMain", "vs_4_0",  &ID3D11Device::CreateVertexShader);
}

ComPtr<ID3D11GeometryShader> ShaderCompiler::CompileGeometryShader()
{
    return CompileXShader<ID3D11GeometryShader>("GSMain", "gs_4_0", &ID3D11Device::CreateGeometryShader);
}

ComPtr<ID3D11PixelShader> ShaderCompiler::CompilePixelShader()
{
    return CompileXShader<ID3D11PixelShader>("PSMain", "ps_4_0", &ID3D11Device::CreatePixelShader);
}

int ShaderCompiler::GetResourceSlot(const char* const pszName)
{
    assert(m_pBlob);

    if (!m_pReflection) //Create reflection on first use
    {
        Decor::ThrowIfFailed(
            D3DReflect(m_pBlob->GetBufferPointer(), m_pBlob->GetBufferSize(), __uuidof(m_pReflection), &m_pReflection), 
            "Failed to create shader reflection instance."
        );
    }

    D3D11_SHADER_INPUT_BIND_DESC Desc;
    if (FAILED(m_pReflection->GetResourceBindingDescByName(pszName, &Desc)))
    {
        return -1;
    }

    return Desc.BindPoint;
}

ComPtr<ID3D11InputLayout> ShaderCompiler::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* InputElementDescs, const UINT NumElements)
{
    assert(m_pBlob);

    ComPtr<ID3D11InputLayout> pInputLayout;

    Decor::ThrowIfFailed(
        m_Device.CreateInputLayout(InputElementDescs, NumElements, m_pBlob->GetBufferPointer(), m_pBlob->GetBufferSize(), &pInputLayout),
        "Failed to create input layout from '%s'.", m_pszFileName
    );
    Decor::SetResourceNameW(pInputLayout, m_pszFileName);

    return pInputLayout;
}
