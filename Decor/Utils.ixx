module;

#include <d3dcommon.h>
#include <wrl\client.h>
#include <Core.h>

export module Utils;

import <exception>;
import <string>;
import <cassert>;

using Microsoft::WRL::ComPtr;

export namespace Utils
{
    /// <summary>
    /// Helper class for COM exceptions
    /// </summary>
    class ComException : public std::exception
    {
    public:
        ComException(HRESULT hr, const char* errorMsg) : result(hr), message(errorMsg) {}

        const char* what() const override
        {
            static char s_str[512] = {};
            sprintf_s(s_str, "Failure with HRESULT of %08X. %s",
                static_cast<unsigned int>(result),
                message.c_str());
            return s_str;
        }

    private:
        HRESULT result;
        std::string message;
    };

    /// <summary>
    /// Helper utility converts D3D API failures into exceptions.
    /// </summary>
    /// <param name="hr">Handle returned by a D3D API function</param>
    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw ComException(hr, "");
        }
    }

    /// <summary>
    /// Helper utility converts D3D API failures into exceptions with formated error message
    /// </summary>
    /// <param name="hr">Handle returned by a D3D API function</param>
    /// <param name="pszMsg">Message template</param>
    inline void ThrowIfFailed(const HRESULT hr, const char* const pszMsg, ...)
    {
        if (FAILED(hr))
        {
            //Expand variadic params
            va_list ap;
            va_start(ap, pszMsg);
            char szBuf[512];
            vsprintf_s(szBuf, pszMsg, ap);

            throw ComException(hr, szBuf);
        }
    }
    
    template<typename... Args>
    void LogMessagef(const TCHAR* str, Args... args)
    {
#ifdef _DEBUG
        GLog->Logf(str, args...);
#endif
    }
    
    template<typename... Args>
    void LogWarningf(const TCHAR* str, Args... args)
    {
        // Don't use GError because that popup window calls Exit(), meaning the log won't be written
        GWarn->Logf(str, args...);
    }
    
    template<class C>
    void SetResourceName(const ComPtr<C>& pResource, const char* const pszName)
    {
#ifdef _DEBUG
        assert(pResource);
        pResource->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(pszName), pszName);
#endif
    }
    
    template<class C>
    void SetResourceNameW(const ComPtr<C>& pResource, const wchar_t* const pszName)
    {
#ifdef _DEBUG
        assert(pResource);
        char szBuf[1024];
        sprintf_s(szBuf, _countof(szBuf), "%S", pszName);
        pResource->SetPrivateData(WKPDID_D3DDebugObjectName, 1024, szBuf);
#endif
    }
};