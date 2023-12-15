module;

#include <codecvt>
#include <string>
#include <sstream>
#include <vector>

export module Scene.Utils;

export namespace Utils
{
    template<class T, class U = T>
    T Exchange(T& obj, U&& new_value)
    {
        T old_value = std::move(obj);
        obj = std::forward<U>(new_value);
        return old_value;
    }

    template <typename T>
    void ReleaseAndMakeNull(T& value)
    {
        if (value)
        {
            value->Release();
            value = nullptr;
        }
    }

    template <typename T>
    void SafeAddRef(T& value)
    {
        if (value)
            value->AddRef();
    }

    template <typename T>
    inline T ToggleBits(const T& aVal, const T& aMask)
    {
        return static_cast<T>(aVal ^ aMask);
    }

    template <typename T>
    inline T Lerp(const T& min, const T& max, const float c)
    {
        return (1.f - c) * min + c * max;
    }

    std::wstring GetFilePathExt(const std::wstring& path)
    {
        const auto lastDot = path.find_last_of(L".");
        if (lastDot != std::wstring::npos)
            return path.substr(lastDot + 1);
        else
            return L"";
    };

    std::string GetFilePathExt(const std::string& path)
    {
        const auto lastDot = path.find_last_of(".");
        if (lastDot != std::string::npos)
            return path.substr(lastDot + 1);
        else
            return "";
    };

    std::string WstringToString(const std::wstring& wideString)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

        return converter.to_bytes(wideString);
    }


    std::wstring StringToWstring(const std::string& string)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

        return converter.from_bytes(string);
    }

    inline const wchar_t* ConfigName()
    {
#ifdef DEBUG
        return L"debug";
#else
        return L"release";
#endif // DEBUG
    }

    float ModX(float x, float y)
    {
        float result = fmod(x, y);
        if (result < 0.0f)
            result += y;

        return result;
    };

    // Splits a given string into the parts by a char delimeter
    std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> result;
        std::stringstream ss(s);
        std::string item;

        while (getline(ss, item, delim)) {
            result.push_back(item);
        }

        return result;
    }
};