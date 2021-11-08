//https://itm4n.github.io/windows-dll-hijacking-clarified/
//https://github.com/mrpond/BlockTheSpot/blob/338134c77966397e0e5c757327234a3b6a7ce792/src/BlockTheSpot.cpp#L6
//asm code doesn't work on x64 because there's no way to force args to be passed on the stack
//(luckily Spotify doesn't release x64 versions)
static_assert(sizeof(void*) == 4);

//Forwarding can also be done with forward exports:
//#pragma comment(linker, "/export:DirectSoundCreate=dsound_orig.DirectSoundCreate,@1")
//But it needs a copy of the original dll renamed to "dsound_orig.dll"

//Moved to hid.dll because dsound is using COM fuckery and DllGetClassObject is already declared somewhere
#include <Windows.h>

void __stdcall LoadAPI(LPVOID* dest, LPCSTR apiName)
{
    if (*dest) return;

    LPCWSTR path = L"C:/Windows/SysWOW64/hid.dll";
    HMODULE module = GetModuleHandleW(path);
    if (!module) {
        module = LoadLibraryW(path);
    }
    *dest = GetProcAddress(module, apiName);

#if _DEBUG
    if (!*dest) DebugBreak();
#endif
}
#define FORWARD_API(name)                       \
    static char __str_##name[] = #name;         \
    static LPVOID __fna_##name = NULL;          \
    extern "C" __declspec(dllexport, naked) void name() { \
        __asm pushad                            \
        __asm push offset __str_##name          \
        __asm push offset __fna_##name          \
        __asm call LoadAPI                      \
        __asm popad                             \
        __asm jmp[__fna_##name]                 \
    }

FORWARD_API(HidD_FlushQueue)
FORWARD_API(HidD_FreePreparsedData)
FORWARD_API(HidD_GetAttributes)
FORWARD_API(HidD_GetConfiguration)
FORWARD_API(HidD_GetFeature)
FORWARD_API(HidD_GetHidGuid)
FORWARD_API(HidD_GetIndexedString)
FORWARD_API(HidD_GetInputReport)
FORWARD_API(HidD_GetManufacturerString)
FORWARD_API(HidD_GetMsGenreDescriptor)
FORWARD_API(HidD_GetNumInputBuffers)
FORWARD_API(HidD_GetPhysicalDescriptor)
FORWARD_API(HidD_GetPreparsedData)
FORWARD_API(HidD_GetProductString)
FORWARD_API(HidD_GetSerialNumberString)
FORWARD_API(HidD_Hello)
FORWARD_API(HidD_SetConfiguration)
FORWARD_API(HidD_SetFeature)
FORWARD_API(HidD_SetNumInputBuffers)
FORWARD_API(HidD_SetOutputReport)
FORWARD_API(HidP_GetButtonCaps)
FORWARD_API(HidP_GetCaps)
FORWARD_API(HidP_GetData)
FORWARD_API(HidP_GetExtendedAttributes)
FORWARD_API(HidP_GetLinkCollectionNodes)
FORWARD_API(HidP_GetScaledUsageValue)
FORWARD_API(HidP_GetSpecificButtonCaps)
FORWARD_API(HidP_GetSpecificValueCaps)
FORWARD_API(HidP_GetUsageValue)
FORWARD_API(HidP_GetUsageValueArray)
FORWARD_API(HidP_GetUsages)
FORWARD_API(HidP_GetUsagesEx)
FORWARD_API(HidP_GetValueCaps)
FORWARD_API(HidP_InitializeReportForID)
FORWARD_API(HidP_MaxDataListLength)
FORWARD_API(HidP_MaxUsageListLength)
FORWARD_API(HidP_SetData)
FORWARD_API(HidP_SetScaledUsageValue)
FORWARD_API(HidP_SetUsageValue)
FORWARD_API(HidP_SetUsageValueArray)
FORWARD_API(HidP_SetUsages)
FORWARD_API(HidP_TranslateUsagesToI8042ScanCodes)
FORWARD_API(HidP_UnsetUsages)
FORWARD_API(HidP_UsageListDifference)