#pragma once

//#include <xmmintrin.h>
//#include <comdef.h>
//#include <exception>
//#include <string>
//#include <memory>
//
//#include <wrl\client.h>
//using Microsoft::WRL::ComPtr;
//
//#include <Engine.h>
//#include <UnRender.h>
//
//#define PREAMBLE PROJECTNAME L": "
//
////Don't use GError because that popup window calls Exit(), meaning the log won't be written
//#define LOGMESSAGE(str) GLog->Log(PREAMBLE str);
//#define LOGMESSAGEF(str, ...) GLog->Logf(PREAMBLE str, __VA_ARGS__);
//#define LOGWARNING(str) GWarn->Log(PREAMBLE str);
//#define LOGWARNINGF(str, ...) GWarn->Logf(PREAMBLE str, __VA_ARGS__);
//
//
//namespace Decor
//{
//    // Helper class for COM exceptions
//    class ComException : public std::exception
//    {
//    public:
//        ComException(HRESULT hr, const char* errorMsg) : result(hr), message(errorMsg) {}
//
//        const char* what() const override
//        {
//            static char s_str[512] = {};
//            sprintf_s(s_str, "Failure with HRESULT of %08X. %s",
//                static_cast<unsigned int>(result),
//                message.c_str());
//            return s_str;
//        }
//
//    private:
//        HRESULT result;
//        std::string message;
//    };
//
//    // Helper utility converts D3D API failures into exceptions.
//    inline void ThrowIfFailed(HRESULT hr)
//    {
//        if (FAILED(hr))
//        {
//            throw ComException(hr, "");
//        }
//    }
//
//    // Helper utility converts D3D API failures into exceptions with formated error message
//    inline void ThrowIfFailed(const HRESULT hr, const char* const pszMsg, ...)
//    {
//        if (FAILED(hr))
//        {
//            //Expand variadic params
//            va_list ap;
//            va_start(ap, pszMsg);
//            char szBuf[512];
//            vsprintf_s(szBuf, pszMsg, ap);
//
//            throw ComException(hr, szBuf);
//        }
//    }
//
//#ifdef _DEBUG
//    template<class C> void SetResourceName(const ComPtr<C>& pResource, const char* const pszName)
//    {
//        assert(pResource);
//        pResource->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(pszName), pszName);
//    }
//
//    template<class C> void SetResourceNameW(const ComPtr<C>& pResource, const wchar_t* const pszName)
//    {
//        assert(pResource);
//        char szBuf[1024];
//        sprintf_s(szBuf, _countof(szBuf), "%S", pszName);
//        pResource->SetPrivateData(WKPDID_D3DDebugObjectName, 1024, szBuf);
//    }
//#else
//    template<class C> void SetResourceName(const ComPtr<C>&, const char* const) {}
//    template<class C> void SetResourceNameW(const ComPtr<C>&, const wchar_t* const) {}
//#endif
//}
