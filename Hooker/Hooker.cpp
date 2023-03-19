#include "stdafx.h"
#include "TextParser.h"
#include "process_utils.h"

// 00000001403FCFC7 | 49:8BF8                  | mov rdi,r8                              |
LPVOID relative_instruction_address_outer = (LPVOID)0x3FCFC7;

// .text:00000000003FD0ED 0F B6 0F                                movzx   ecx, byte ptr [rdi]
LPVOID relative_instruction_address_loop = (LPVOID)0x3FD0ED;

LPVOID instruction_address_outer = (LPVOID)0;
LPVOID instruction_address_loop = (LPVOID)0;

LPCWSTR GAME_PATH = L"ed8_ps5_D3D11.exe";
HANDLE game_process = NULL;
HANDLE game_thread = NULL;

thread_local TextParser text_parser;

void on_breakpoint_outer(HANDLE game_process, HANDLE game_thread, CONTEXT* dbg_context)
{
	// Reset text parser on new text request
	text_parser = {};

	// Reset the context flags to write out in case the call to GetThreadContext changed the flags for some reason.
	dbg_context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;

	// The instruction that we overwrote was `movzx ecx, byte ptr [eax]`, which is coded on 3 bytes, so we need to jump over the
	// next two bytes which are garbage.
	dbg_context->Rip += 2;

	// Simulate the overwritten instruction
	dbg_context->Rdi = dbg_context->R8;

	// Flush the context out to the processor registers.
	if(!SetThreadContext(game_thread, dbg_context))
	{
		printf("Failed to jump over borked instructions.");
	}
}

void on_breakpoint_loop(HANDLE game_process, HANDLE game_thread, CONTEXT* dbg_context)
{
	char sjis[RPM_BUF_SIZE];
	SIZE_T b;
	LPVOID sjis_addr;

	sjis_addr = (LPVOID)dbg_context->Rdi;

	if(!ReadProcessMemory(game_process, sjis_addr, sjis, RPM_BUF_SIZE, &b))
	{
		printf("Failed to read SJIS character from game memory @ 0x%08x. Read %d bytes. (Error %d.)\n", sjis_addr, b, GetLastError());
	}

	// Reset the context flags to write out in case the call to GetThreadContext changed the flags for some reason.
	dbg_context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;

	// The instruction that we overwrote was `movzx ecx, byte ptr [rdi]`, which is coded on 3 bytes, so we need to jump over the
	// next two bytes which are garbage.
	dbg_context->Rip += 2;

	// Simulate the overwritten instruction by moving the lowest read byte into ECX.
	dbg_context->Rcx = (unsigned char)sjis[0];

	// Flush the context out to the processor registers.
	if(!SetThreadContext(game_thread, dbg_context))
	{
		printf("Failed to jump over borked instructions.");
		return;
	}

	text_parser.buf_current_address = dbg_context->Rdi;
	text_parser.parse(sjis);
	memcpy(text_parser.prev_rpm_buf, sjis, RPM_BUF_SIZE);
}

void on_breakpoint(HANDLE game_process, HANDLE game_thread)
{
	CONTEXT context;
	LPVOID bp_addr;

	// Set a dummy value in EIP so we can check whether the GetThreadContext call actually worked.
	context.Rip = 0xDEADBEEF;

	// We want to read the integer registers (esp. EAX) and the control registers (esp. EIP)
	context.ContextFlags = CONTEXT_INTEGER | CONTEXT_CONTROL;

	if(!GetThreadContext(game_thread, &context))
	{
		printf("Failed to get thread context.\n");
	}

	bp_addr = (LPVOID)context.Rip;
	if (bp_addr == (LPVOID)((DWORD_PTR)instruction_address_outer + 1))
		on_breakpoint_outer(game_process, game_thread, &context);
	else if (bp_addr == (LPVOID)((DWORD_PTR)instruction_address_loop + 1))
		on_breakpoint_loop(game_process, game_thread, &context);
}

void dispatch_event_handler(LPDEBUG_EVENT event)
{
	// We need to skip the first breakpoint event, since it's artificially generated.
	static bool first_try = true;

	switch(event->dwDebugEventCode)
	{
	case EXCEPTION_DEBUG_EVENT:
		// We only bother with the exception events
		switch(event->u.Exception.ExceptionRecord.ExceptionCode)
		{
		case EXCEPTION_BREAKPOINT:
			if (first_try)
			{
				first_try = false;
				break;
			}

			if (game_thread == nullptr)
			{
				if(NULL == (game_thread = OpenThread(THREAD_ALL_ACCESS, FALSE, event->dwThreadId)))
				{
					printf("Failed to open game thread.\n");
					exit(1);
				}
				else
				{
					printf("Game thread opened. Thread ID: %d\n", event->dwThreadId);
				}
			}

			on_breakpoint(game_process, game_thread);
			break;

			default:
				printf("Unhandled exception occurred.\n");
				break;
		}
		break;

	default:
		//printf("Unhandled debug event occurred.\n");
		break;
	}
}

void set_breakpoint(HANDLE game_process, LPVOID instruction_address)
{
	SIZE_T b;
	DWORD newprot = PAGE_EXECUTE_READWRITE;
	DWORD oldprot;

	if(!VirtualProtectEx(game_process, instruction_address, 1, newprot, &oldprot))
	{
		printf("Failed to weaken memory protection. (Error %d.)\n", GetLastError());
		exit(1);
	}

	printf("Memory protection weakened.\n");

	const BYTE int3 = 0xCC;
	if(!WriteProcessMemory(game_process, instruction_address, &int3, 1, &b))
	{
		printf("Failed to set breakpoint.\n");
		exit(1);
	}

	printf("Breakpoint set.\n");

	if(!VirtualProtectEx(game_process, instruction_address, 1, oldprot, &newprot))
	{
		printf("Failed to reset memory protection. (Error %d.)\n", GetLastError());
		exit(1);
	}

	printf("Memory protection restored.\n");
}

void debug_loop()
{
	DEBUG_EVENT event;
	ZeroMemory(&event, sizeof(event));

	for(;;)
	{
		if(!WaitForDebugEvent(&event, INFINITE))
		{
			printf("Failed to get next debug event. (Error %d.)", GetLastError());
			exit(1);
		}

		dispatch_event_handler(&event);
		ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE);
	}
}

void find_game()
{
	HANDLE process_snapshot;
	PROCESSENTRY32 pe;
	bool found = false;

	if(INVALID_HANDLE_VALUE == (process_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)))
	{
		printf("Failed to get process list. (Error %d.)\n", GetLastError());
		exit(1);
	}

	pe.dwSize = sizeof(pe);

	if(!Process32First(process_snapshot, &pe))
	{
		printf("Failed to get process from list. (Error %d.)\n", GetLastError());
		exit(1);
	}

	do
	{
		if(wcscmp(pe.szExeFile, GAME_PATH) == 0)
		{
			found = true;
			break;
		}
	}
	while(Process32Next(process_snapshot, &pe));

	if(!found)
	{
		printf("Failed to find %ls; is the game running?\n", GAME_PATH);
		exit(1);
	}

	printf("Found %ls\n", GAME_PATH);

	if(NULL == (game_process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe.th32ProcessID)))
	{
		printf("Failed to open %ls process. (Error %d.)\n", GAME_PATH, GetLastError());
		exit(1);
	}

	printf("Opened %ls process at %p.\n", GAME_PATH, game_process);

	BYTE* module_base_address = GetModuleBaseAddress(pe.th32ProcessID, pe.szExeFile);
	if (module_base_address == nullptr)
	{
		printf("Failed to find the module base address for %ls.\n", GAME_PATH);
		exit(1);
	}

	instruction_address_outer = (LPVOID)((ULONGLONG)module_base_address + (ULONGLONG)relative_instruction_address_outer);
	instruction_address_loop = (LPVOID)((ULONGLONG)module_base_address + (ULONGLONG)relative_instruction_address_loop);

	if(!DebugActiveProcess(pe.th32ProcessID))
	{
		printf("Failed to debug %ls process.\n", GAME_PATH);
	}

	printf("Debugging %ls...\n", GAME_PATH);
}

void escalate_privileges()
{
	HANDLE token;
	LUID debug_luid;

	if(!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &debug_luid))
	{
		printf("Failed to look up debug privilege name. (Error %d.)\n", GetLastError());
		exit(1);
	}

	if(!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
	{
		printf("Failed to open process access token. (Error %d.)\n", GetLastError());
		exit(1);
	}
	
	TOKEN_PRIVILEGES tp = {};

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = debug_luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	if(!AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), NULL, NULL))
	{
		printf("Failed to adjust token privileges. (Error %d.)\n", GetLastError());
		exit(1);
	}

	CloseHandle(token);
}

int _tmain(int argc, _TCHAR* argv[])
{
	escalate_privileges();
	printf("Privileges escalated.\n");
	find_game();
	set_breakpoint(game_process, instruction_address_outer);
	set_breakpoint(game_process, instruction_address_loop);
	printf("Entering debug loop.\n");
	debug_loop();
	getchar();
	return 0;
}
