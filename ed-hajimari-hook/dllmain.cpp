#include "stdafx.h"
#include "constants.h"
#include "TextParser.h"

LPVOID INSTRUCTION_ADDRESS_OUTER = nullptr;
LPVOID INSTRUCTION_ADDRESS_LOOP = nullptr;

thread_local TextParser g_textParser; 
LPVOID g_lpVectoredExceptionHandler = nullptr;

void OnProcessAttach();
void OnProcessDetach();
LONG WINAPI VectoredExceptionHandler(_EXCEPTION_POINTERS* ExceptionInfo);

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
		OnProcessAttach();
		break;

    case DLL_PROCESS_DETACH:
		OnProcessDetach();
        break;
    }

    return TRUE;
}

bool SetBreakpoint(LPVOID lpInstructionAddress)
{
	const uint8_t HLT = 0xF4;
	DWORD flOldProtection, flOldProtection2;
	if (!VirtualProtect(lpInstructionAddress, 1, PAGE_EXECUTE_READWRITE, &flOldProtection))
		return false;

	*(uint8_t *)lpInstructionAddress = HLT;

	if (!VirtualProtect(lpInstructionAddress, 1, flOldProtection, &flOldProtection2))
		return false;

	return true;
}

void OnProcessAttach()
{
	auto hModBase = GetModuleHandle(nullptr);

	INSTRUCTION_ADDRESS_OUTER = (LPVOID)((ULONGLONG)hModBase + (ULONGLONG)RELATIVE_INSTRUCTION_ADDRESS_OUTER);
	INSTRUCTION_ADDRESS_LOOP = (LPVOID)((ULONGLONG)hModBase + (ULONGLONG)RELATIVE_INSTRUCTION_ADDRESS_LOOP);

	g_lpVectoredExceptionHandler = AddVectoredExceptionHandler(1, &VectoredExceptionHandler);

	SetBreakpoint(INSTRUCTION_ADDRESS_OUTER);
	SetBreakpoint(INSTRUCTION_ADDRESS_LOOP);
}

void OnProcessDetach()
{
	RemoveVectoredExceptionHandler(g_lpVectoredExceptionHandler);
	g_lpVectoredExceptionHandler = nullptr;
}

LONG WINAPI VectoredExceptionHandler(_EXCEPTION_POINTERS* ExceptionInfo)
{
	if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_PRIV_INSTRUCTION)
	{
		if (ExceptionInfo->ContextRecord->Rip == ((DWORD_PTR)INSTRUCTION_ADDRESS_OUTER))
		{
			// Reset text parser on new text request
			g_textParser = {};

			// The instruction that we overwrote was `mov rdi, r8`, which is coded on 3 bytes, so we need to skip over
			// it as we simulate it below.
			ExceptionInfo->ContextRecord->Rip += 3;

			// Simulate the overwritten instruction
			ExceptionInfo->ContextRecord->Rdi = ExceptionInfo->ContextRecord->R8;
			return EXCEPTION_CONTINUE_EXECUTION;
		}

		if (ExceptionInfo->ContextRecord->Rip == ((DWORD_PTR)INSTRUCTION_ADDRESS_LOOP))
		{
			const char* input = (const char*)ExceptionInfo->ContextRecord->Rdi;

			// The instruction that we overwrote was `movzx ecx, byte ptr [rdi]`, which is coded on 3 bytes, so we need to skip over
			// it as we simulate it below.
			ExceptionInfo->ContextRecord->Rip += 3;

			// Simulate the overwritten instruction by moving the lowest read byte into ECX.
			ExceptionInfo->ContextRecord->Rcx = input[0];

			g_textParser.CurrentAddress = ExceptionInfo->ContextRecord->Rdi;
			g_textParser.Parse(input);
			memcpy(g_textParser.PreviousInputBuffer, input, RPM_BUF_SIZE);
			return EXCEPTION_CONTINUE_EXECUTION;
		}
	}

	return EXCEPTION_CONTINUE_SEARCH;
}
