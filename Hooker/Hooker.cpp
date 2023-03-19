// Hooker.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <string>
#include <vector>

const size_t RPM_BUF_SIZE = 8;
const size_t MSG_BUF_SIZE = 8192;

// 00000001403FCFC7 | 49:8BF8                  | mov rdi,r8                              |
LPVOID relative_instruction_address_outer = (LPVOID)0x3FCFC7;

// .text:00000000003FD0ED 0F B6 0F                                movzx   ecx, byte ptr [rdi]
LPVOID relative_instruction_address_loop = (LPVOID)0x3FD0ED;

LPVOID instruction_address_outer = (LPVOID)0;
LPVOID instruction_address_loop = (LPVOID)0;

LPCWSTR GAME_PATH = L"ed8_ps5_D3D11.exe";
HANDLE game_process = NULL;
HANDLE game_thread = NULL;

unsigned char int3 = 0xCC;

enum ParserState
{
	PARSER_READY,
	PARSER_READ_COLOR,
	PARSER_READ_HASHCODE,
	PARSER_TEXT,
	PARSER_FINISHED
};

struct ParserContext
{
	DWORD_PTR buf_current_address = 0;
	DWORD_PTR defer_until_address = 0;

	unsigned int color = 0xffffffff;
	
	int face_id = -1;
	int character_id = -1;

	DWORD_PTR prev_msg_address = 0;
	char prev_rpm_buf[RPM_BUF_SIZE] = {};

	DWORD_PTR msg_start_address = 0;
	char msg_buf[MSG_BUF_SIZE + 1] = {};
	unsigned int msg_offset = 0;

	bool msg_unk_1a = false;

	enum ParserState parser_state = PARSER_READY;
};

thread_local ParserContext parser_context;

char oldstr[MSG_BUF_SIZE + 1] = {};
wchar_t wstr[MSG_BUF_SIZE + 1] = {};
char utf8str[MSG_BUF_SIZE + 1] = {};

void flush_message_buffer(ParserContext& ctx)
{
	// this case can occur at the very beginning of a message right after parsing a face ID but before any text has been parsed
	if (ctx.msg_offset == 0)
		return; // nothing to do !
	
	// null-terminate the buffer
	ctx.msg_buf[ctx.msg_offset] = '\0';

	// No change, don't bother updating
	if (memcmp(ctx.msg_buf, oldstr, ctx.msg_offset) == 0)
		return;

	memcpy(oldstr, ctx.msg_buf, ctx.msg_offset);

	const UINT CP_SHIFT_JIS = 932;
	const DWORD LCID_JA_JA = 1041;

	int wchars_num = MultiByteToWideChar(CP_UTF8, 0, ctx.msg_buf, -1, nullptr, 0);
	if (wchars_num > MSG_BUF_SIZE)
		return;

	MultiByteToWideChar(CP_UTF8, 0, ctx.msg_buf, -1, wstr, wchars_num);

	int utf8chars_num = WideCharToMultiByte(CP_SHIFT_JIS, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
	if (utf8chars_num > MSG_BUF_SIZE)
		return;

	WideCharToMultiByte(CP_SHIFT_JIS, 0, wstr, wchars_num, utf8str, utf8chars_num, nullptr, nullptr);

	HGLOBAL locale_ptr = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
	DWORD japanese = LCID_JA_JA;
	memcpy(GlobalLock(locale_ptr), &japanese, sizeof(japanese));
	GlobalUnlock(locale_ptr);
	
	// allocate a buffer to give to the system with the clipboard data.
	HGLOBAL clip_buf = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, utf8chars_num);

	// copy the message buffer into the global buffer
	memcpy(GlobalLock(clip_buf), utf8str, utf8chars_num);
	GlobalUnlock(clip_buf);

	// Write the data to the clipboard.
	if (!OpenClipboard(nullptr))
	{
		printf("Failed to open clipboard. (Error %d.)\n", GetLastError());
		GlobalFree(locale_ptr);
		GlobalFree(clip_buf);
		goto cleanup;
	}

	if (!EmptyClipboard())
	{
		printf("Failed to empty clipboard. (Error %d.)\n", GetLastError());
		GlobalFree(locale_ptr);
		GlobalFree(clip_buf);
		goto cleanup;
	}

	if (!SetClipboardData(CF_TEXT, clip_buf))
	{
		printf("Failed to set clipboard data. (Error %d.)\n", GetLastError());
		GlobalFree(locale_ptr);
		goto cleanup;
	}

	if(!SetClipboardData(CF_LOCALE, locale_ptr))
	{
		printf("Failed to set clipboard locale. (Error %d.)\n", GetLastError());
		GlobalFree(locale_ptr);
		goto cleanup;
	}

cleanup:
	CloseClipboard();
}

void parse_color(ParserContext& ctx, char buf[])
{
	unsigned int offset = 1;
	unsigned int start_offset = offset;
	offset += 4; /* assume color codes are always 4 (hex) digits */

	size_t length = (offset - start_offset);
	std::vector<char> tmpBuffer(length + 1U, 0);
	memcpy(&tmpBuffer[0], buf + start_offset, length);

	ctx.color = std::strtoull(&tmpBuffer[0], nullptr, 16);
	ctx.defer_until_address = ctx.buf_current_address + length + 1;
}

void parse_hashcode(ParserContext& ctx, char buf[])
{
	unsigned int offset = 1;
	unsigned int start_offset = offset;
	while(isdigit(buf[offset]))
		++offset;

	size_t length = (offset - start_offset);
	std::vector<char> tmpBuffer(length + 1U, 0);
	memcpy(&tmpBuffer[0], buf + start_offset, length);

	// now i is at the offset of the first non-digit character, so we can see whether we parsed a face or a character id.
	char code = buf[offset];
	switch (code)
	{
		case 'P':
			ctx.character_id = atoi(&tmpBuffer[0]);
			break;

		case 'F':
			ctx.face_id = atoi(&tmpBuffer[0]);
			break;

		default:
			printf("Parse failure: unknown hashcode %c, value %u\n", code, (unsigned int)atoi(&tmpBuffer[0]));
	}

	ctx.defer_until_address = ctx.buf_current_address + length + 2;
}

void end_of_message(ParserContext& ctx)
{
	flush_message_buffer(ctx);

	ctx.prev_msg_address = 0;
	ctx.parser_state = PARSER_FINISHED;
}

void parse_buf(ParserContext& ctx, char buf[])
{
	if (ctx.defer_until_address != 0
		&& ctx.buf_current_address < ctx.defer_until_address)
		return;

	// This parser needs to mimic the official behaviour better
	// Official behaviour iterates over the string and handles it character by character,
	// parsing further depending on where it's at - but even when parsing out the message,
	// it will still do that only 3 bytes at a time for appropriate characters.
	// Its states may well be unnecessary.
	// Note that each call of this method is triggered by each loop iteration
	// or each main character it should process.
	// Any that it has "skipped" will have been handled by its previous processing.
	switch(ctx.parser_state)
	{
	case PARSER_READY:
		switch (buf[0])
		{
			case '':
				ctx.parser_state = PARSER_READ_COLOR;
				break;

			case '#':
				ctx.parser_state = PARSER_READ_HASHCODE;
				parse_buf(ctx, buf);
				break;

			default:
				ctx.parser_state = PARSER_TEXT;
				parse_buf(ctx, buf);
		}
		break;

	case PARSER_READ_COLOR:
		parse_color(ctx, buf);
		ctx.parser_state = PARSER_READY;
		break;

	case PARSER_READ_HASHCODE:
		parse_hashcode(ctx, buf);
		ctx.parser_state = PARSER_READY;
		break;

	case PARSER_TEXT:
		if (ctx.msg_start_address == 0)
			ctx.msg_start_address = ctx.buf_current_address;

		if (ctx.prev_msg_address != 0)
		{
			size_t len = ctx.buf_current_address - ctx.prev_msg_address;
			if (ctx.msg_offset + len > MSG_BUF_SIZE)
			{
				printf("Parse failure: message buffer overflow.\n");
			}
			else
			{
				memcpy(ctx.msg_buf + ctx.msg_offset, ctx.prev_rpm_buf, len);
				ctx.msg_offset += len;
			}
		}

		ctx.prev_msg_address = ctx.buf_current_address;

		switch (buf[0])
		{
			case 0x1A:
				ctx.msg_unk_1a = true;
				end_of_message(ctx);
				break;

			case 0:
			case 1:
			case 2:
			case 3:
				end_of_message(ctx);
				break;
		}
		break;

	case PARSER_FINISHED:
		break;
	}
}

void on_breakpoint_outer(HANDLE game_process, HANDLE game_thread, CONTEXT* dbg_context)
{
	// Reset parser context on new text request
	parser_context = {};

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

	parser_context.buf_current_address = dbg_context->Rdi;
	parse_buf(parser_context, sjis);
	memcpy(parser_context.prev_rpm_buf, sjis, RPM_BUF_SIZE);
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
            } while (Module32Next(hSnapshot, &ModuleEntry32));
        }
        CloseHandle(hSnapshot);
    }
    return moduleBaseAddress;
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
