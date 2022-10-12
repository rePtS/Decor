#pragma once

#include <windows.h>
#include <Engine.h>
#include <Extension.h>
#include <cassert>
#include <unordered_map>
#include <memory>
#include "IRenderingContext.h"

/// <summary>
/// 
/// </summary>
class NativeHooks
{
public:
    explicit NativeHooks(IRenderingContext* pRenderBackend);

    class HookBase //So instances can share container
    {
    public:
        virtual ~HookBase() {}
    };

    template <class HookClass, class UnrealClass, size_t iNativeId> //We use CRTP as the replacement function is called with another object's 'this' pointer, i.e. virtual functions plain won't work.
    class HookTemplate : public HookBase
    {
    public:
        ~HookTemplate()
        {
            //Restore original behavior
            GNatives[iNativeId] = m_OrigFunc;
        }

    protected:
        explicit HookTemplate(const wchar_t* const pszName)
        :m_OrigFunc(GNatives[iNativeId])
        {
            GNatives[iNativeId] = reinterpret_cast<Native>(&HookTemplate<HookClass, UnrealClass, iNativeId>::ReplacementFuncInternal);
            GLog->Logf(L"Installing hook '%s'.", pszName);

            //We add ourselves to the parent, that way the native function id doesn't have to be exposed
            GetSingleton().m_ActiveHooks.emplace(iNativeId, std::unique_ptr<HookBase>(this));
        }

    private:
        //The actual native function that's called
        void ReplacementFuncInternal(FFrame& Stack, RESULT_DECL)
        {
            //At this point our 'this' pointer points to an Unreal object, not the fix object, so get its pointer.
            assert(NativeHooks::GetSingleton().m_ActiveHooks.find(iNativeId) != std::cend(GetSingleton().sm_pSingleton->m_ActiveHooks));
            HookBase* const pContext = NativeHooks::GetSingleton().m_ActiveHooks.at(iNativeId).get();
            assert(pContext);

            static_cast<HookClass&>(*this).ReplacementFunc(reinterpret_cast<UnrealClass&>(*this), static_cast<HookClass&>(*pContext), Stack, Result);

            // Call the original function
            //(reinterpret_cast<UnrealClass&>(*this).*m_OrigFunc)(Stack, Result);
        }

        const Native m_OrigFunc;
    };

private:
    static NativeHooks& GetSingleton()
    {
        assert(sm_pSingleton);
        return *sm_pSingleton;
    }

    static NativeHooks* sm_pSingleton; //Need a singleton as classes with a wrong 'this' pointer must be able to find context.
    std::unordered_map<size_t, std::unique_ptr<HookBase>> m_ActiveHooks;
};

/// <summary>
/// 
/// </summary>
class FlagBaseSetBoolHook : public NativeHooks::HookTemplate<FlagBaseSetBoolHook, XFlagBase, EXTENSION_FlagBaseSetBool>
{
public:
    explicit FlagBaseSetBoolHook(IRenderingContext* pRenderBackend) :
        HookTemplate(L"FlagBaseSetBool Hook"),
        m_pRenderBackend(pRenderBackend) {};

    void ReplacementFunc(XFlagBase& UObjectThis, FlagBaseSetBoolHook& /*HookObjectThis*/, FFrame& Stack, RESULT_DECL);

private:    
    explicit FlagBaseSetBoolHook();
    IRenderingContext* m_pRenderBackend;
};

/// <summary>
/// 
/// </summary>
class FlagBaseGetBoolHook : public NativeHooks::HookTemplate<FlagBaseGetBoolHook, XFlagBase, EXTENSION_FlagBaseGetBool>
{
public:
    explicit FlagBaseGetBoolHook(IRenderingContext* pRenderBackend) :
        HookTemplate(L"FlagBaseGetBool Hook"),
        m_pRenderBackend(pRenderBackend) {};

    void ReplacementFunc(XFlagBase& UObjectThis, FlagBaseGetBoolHook& /*HookObjectThis*/, FFrame& Stack, RESULT_DECL);

private:
    explicit FlagBaseGetBoolHook();
    IRenderingContext* m_pRenderBackend;
};