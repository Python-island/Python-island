#pragma once

#ifdef _WIN32
#ifdef __MINGW32__
// ============================================================
//  MinGW compatibility layer
// ============================================================
// MinGW's taskschd.h is missing the ILogonTrigger interface declaration
// required for the Task Scheduler COM API.  This block fills the gap.
// When building with MSVC the standard Windows SDK provides the
// interface, so the guard below skips this block.

#include <windows.h>
#include <taskschd.h>

#ifndef __ILogonTrigger_INTERFACE_DEFINED__
#define __ILogonTrigger_INTERFACE_DEFINED__

EXTERN_C const GUID DECLSPEC_SELECTANY IID_ILogonTrigger = {0xda506fd1, 0x1a43, 0x4f66, {0x9e, 0x4b, 0xde, 0x31, 0xb3, 0xae, 0xca, 0xb6}};

MIDL_INTERFACE("da506fd1-1a43-4f66-9e4b-de31b3aecab6")
ILogonTrigger : public ITrigger
{
    virtual HRESULT STDMETHODCALLTYPE put_Delay(
        BSTR delay) = 0;

    virtual HRESULT STDMETHODCALLTYPE get_Delay(
        BSTR *delay) = 0;

    virtual HRESULT STDMETHODCALLTYPE put_UserId(
        BSTR user) = 0;

    virtual HRESULT STDMETHODCALLTYPE get_UserId(
        BSTR *user) = 0;
};

#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(ILogonTrigger, 0xda506fd1, 0x1a43, 0x4f66, 0x9e, 0x4b, 0xde, 0x31, 0xb3, 0xae, 0xca, 0xb6)
#endif

#endif /* __ILogonTrigger_INTERFACE_DEFINED__ */

#endif /* __MINGW32__ */
#else
// Linux: nothing required — mingw_compat.h is a no-op on non-Windows
#endif /* _WIN32 */
