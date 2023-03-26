#pragma once

const size_t RPM_BUF_SIZE = 8;

// .text:00000000003FCFC7 49 8B F8                                mov     rdi, r8
// .text:00000000003FCFCA                         ; 103:   if ( a17 )
// .text:00000000003FCFCA 48 85 D2                                test    rdx, rdx
// .text:00000000003FCFCD 74 06                                   jz      short loc_3FCFD5
const LPVOID RELATIVE_INSTRUCTION_ADDRESS_OUTER = (LPVOID) 0x3FCDC7;

// .text:00000000003FD0ED 0F B6 0F                                movzx   ecx, byte ptr [rdi]
// .text:00000000003FD0F0                         ; 150:     if ( (unsigned __int8)v47 < ' ' )
// .text:00000000003FD0F0 80 F9 20                                cmp     cl, 20h ; ' '
// .text:00000000003FD0F3 0F 83 EA 29 00 00                       jnb     loc_3FFAE3
const LPVOID RELATIVE_INSTRUCTION_ADDRESS_LOOP = (LPVOID) 0x3FCEED;
