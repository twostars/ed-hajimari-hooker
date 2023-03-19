#include "stdafx.h"
#include "process_utils.h"

BYTE* GetModuleBaseAddress(DWORD dwProcessID, const WCHAR* lpszModuleName)
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, dwProcessID);
    BYTE* moduleBaseAddress = nullptr;
    if (hSnapshot != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32 ModuleEntry32 = { 0 };
        ModuleEntry32.dwSize = sizeof(MODULEENTRY32);
        if (Module32First(hSnapshot, &ModuleEntry32))
        {
            do
            {
                if (_wcsicmp(ModuleEntry32.szModule, lpszModuleName) == 0)
                {
                    moduleBaseAddress = ModuleEntry32.modBaseAddr;
                    break;
                }
            }
			while (Module32Next(hSnapshot, &ModuleEntry32));
        }

		CloseHandle(hSnapshot);
    }

	return moduleBaseAddress;
}
