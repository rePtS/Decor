module;

#include <fstream>
#include <sstream>

export module Scene.Log;

namespace SceneLog
{
    export enum class ELoggingLevel
    {
        eError = 0,
        eWarning = 1,
        eDebug = 2,
        eInfo = 3
    };

    static std::wstringstream wstream;

    export ELoggingLevel sLoggingLevel = ELoggingLevel::eInfo;

    const wchar_t* LogLevelToString(ELoggingLevel level)
    {
        switch (level)
        {
        case ELoggingLevel::eDebug:
            return L"Debug";
        case ELoggingLevel::eInfo:
            return L"Info";
        case ELoggingLevel::eWarning:
            return L"Warning";
        case ELoggingLevel::eError:
            return L"Error";
        default:
            return L"Uknown";
        }
    }

    export void Flush()
    {
        std::wofstream outFile("DecorDrv.log");
        outFile << wstream.str();
        outFile.close();
        wstream.clear();
    }

    export template <typename... Args>
    void Write(ELoggingLevel msgLevel, const wchar_t* msg, Args... args)
    {
        if (sLoggingLevel < msgLevel)
            return;

        wchar_t tmpBuff1[1024] = {};
        swprintf_s(tmpBuff1, msg, args...);

        wchar_t tmpBuff2[1024] = {};
        swprintf_s(tmpBuff2, L"[% 7s] %s\n", LogLevelToString(msgLevel), tmpBuff1);

        wstream << tmpBuff2;
    }

    export template <typename... Args>
    void Write(bool isRequiredData, const wchar_t* msg, Args... args)
    {
        Write(isRequiredData ? ELoggingLevel::eError : ELoggingLevel::eDebug, msg, args...);
    }

    export template <typename... Args>
    void Debug(const wchar_t* msg, Args... args)
    {
        Write(ELoggingLevel::eDebug, msg, args...);
    }

    export template <typename... Args>
    void Info(const wchar_t* msg, Args... args)
    {
        Write(ELoggingLevel::eInfo, msg, args...);
    }

    export template <typename... Args>
    void Warning(const wchar_t* msg, Args... args)
    {
        Write(ELoggingLevel::eWarning, msg, args...);
    }

    export template <typename... Args>
    void Error(const wchar_t* msg, Args... args)
    {
        Write(ELoggingLevel::eError, msg, args...);
    }
};