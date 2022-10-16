#include "NativeHooks.h"

NativeHooks* NativeHooks::sm_pSingleton;

NativeHooks::NativeHooks(IRenderingContext* pRenderBackend)
{
    assert(!sm_pSingleton);
    sm_pSingleton = this;

    new FlagBaseSetBoolHook(pRenderBackend);
    new FlagBaseGetBoolHook(pRenderBackend);
}

void FlagBaseSetBoolHook::ReplacementFunc(XFlagBase& UObjectThis, FlagBaseSetBoolHook& HookObjectThis, FFrame& Stack, RESULT_DECL)
{
    P_GET_NAME(flagName)
    P_GET_UBOOL_OPTX(flagVal, 1)
    P_GET_UBOOL_OPTX(bAdd, 1)
    P_GET_INT_OPTX(expiration, -1)
    P_FINISH;

    if (flagName.GetIndex() == 1847 || flagName.GetIndex() == 1836)
    {
        //MessageBox(NULL, flagName.operator*(), L"Test", MB_OK | MB_ICONQUESTION);
        HookObjectThis.m_pRenderBackend->PreLoadLevel();
    }

    *static_cast<UBOOL*>(Result) = UObjectThis.SetBool(flagName, flagVal, bAdd, expiration);
}

void FlagBaseGetBoolHook::ReplacementFunc(XFlagBase& UObjectThis, FlagBaseGetBoolHook& HookObjectThis, FFrame& Stack, RESULT_DECL)
{
    P_GET_NAME(flagName)
    P_FINISH;

    if (flagName.GetIndex() == 1847 || flagName.GetIndex() == 1836)
    {
        //MessageBox(NULL, flagName.operator*(), L"Test", MB_OK | MB_ICONQUESTION);
        auto levelName = UObjectThis.GetOuter()->GetOuter()->GetName();
        HookObjectThis.m_pRenderBackend->LoadLevel(levelName);
    }

    UBOOL val;
    UBOOL getBoolResult = UObjectThis.GetBool(flagName, val);
    *reinterpret_cast<UBOOL*>(Result) = getBoolResult && val;
}