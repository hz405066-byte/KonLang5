; KonLang v5 (COM) -- автоматически
        org 100h
        jmp kon_main
kon_main:
        cld
        mov     word [scratch_len], 0
        mov     ax, 33
        call    append_int
        mov     si, scratch
        mov     di, v0+2
        call    strcpy_ds
        mov     word [scratch_len], 0
        mov     si, s0
        call    append_str
        mov     si, scratch
        call    puts
        mov     dx, v1
        call    input_line
        mov     word [scratch_len], 0
        mov     si, s1
        call    append_str
        mov     si, scratch
        mov     di, v2+2
        call    strcpy_ds
        mov     word [scratch_len], 0
        mov     si, s2
        call    append_str
        mov     si, scratch
        mov     di, v3+2
        call    strcpy_ds
        mov     word [scratch_len], 0
        mov     ax, 0
        call    append_int
        mov     si, scratch
        mov     di, v4+2
        call    strcpy_ds
        push    cx
        mov     si, v2+2
        call    strlen_ax
        mov     cx, ax
.cyc0:
        mov     word [scratch_len], 0
        mov     si, v4+2
        call    str2int
        mov     bx, ax
        mov     si, v2+2
        call    str_at
        call    append_char_at
        mov     si, scratch
        mov     di, v5+2
        call    strcpy_ds
        mov     word [scratch_len], 0
        mov     ax, 0
        call    append_int
        mov     si, scratch
        mov     di, v6+2
        call    strcpy_ds
        push    cx
        mov     ax, 33
        mov     cx, ax
.cyc1:
        mov     si, v0+2
        call    str2int
        push    ax
        mov     si, v5+2
        call    str2int
        mov     bx, ax
        pop     ax
        cmp     ax, bx
        jne     near .iff3
        mov     word [scratch_len], 0
        mov     si, v6+2
        call    str2int
        push    ax
        mov     si, v1+2
        call    str2int
        mov     bx, ax
        pop     ax
        add     ax, bx
        call    append_int
        mov     si, scratch
        mov     di, v7+2
        call    strcpy_ds
        mov     si, v7+2
        call    str2int
        push    ax
        mov     ax, 33
        mov     bx, ax
        pop     ax
        cmp     ax, bx
        jl      near .iff5
        mov     word [scratch_len], 0
        mov     si, v7+2
        call    str2int
        push    ax
        mov     ax, 33
        mov     bx, ax
        pop     ax
        sub     ax, bx
        call    append_int
        mov     si, scratch
        mov     di, v7+2
        call    strcpy_ds
        jmp     near .ife4
.iff5:
.ife4:
        mov     word [scratch_len], 0
        mov     si, v3+2
        call    append_str
        mov     si, v7+2
        call    str2int
        mov     bx, ax
        shl     bx, 1
        mov     si, lst0
        add     si, bx
        lodsw
        mov     si, ax
        call    append_str
        mov     si, scratch
        mov     di, v3+2
        call    strcpy_ds
        jmp     near .cye1
        jmp     near .ife2
.iff3:
.ife2:
        mov     word [scratch_len], 0
        mov     si, v6+2
        call    str2int
        push    ax
        mov     ax, 1
        mov     bx, ax
        pop     ax
        add     ax, bx
        call    append_int
        mov     si, scratch
        mov     di, v6+2
        call    strcpy_ds
        dec     cx
        jnz     near .cyc1
.cye1:
        pop     cx
        mov     word [scratch_len], 0
        mov     si, v4+2
        call    str2int
        push    ax
        mov     ax, 1
        mov     bx, ax
        pop     ax
        add     ax, bx
        call    append_int
        mov     si, scratch
        mov     di, v4+2
        call    strcpy_ds
        dec     cx
        jnz     near .cyc0
.cye0:
        pop     cx
        mov     word [scratch_len], 0
        mov     si, s3
        call    append_str
        mov     si, v3+2
        call    append_str
        mov     si, scratch
        call    puts
        mov     ax, 4C00h
        int     21h

; rt_console.inc -- runtime KonLang для .COM (одна сегментная группа DS=CS=SS=ES).
; Все строки -- NUL-терминированные, склейка идёт через SCRATCH.

; ---------------------------------------------------------------------------
; PUTS -- вывод строки DS:SI в stdout через INT 21h AH=02h
; (не AH=09h: тот останавливается на '$', а в тексте могут быть любые байты)
; ---------------------------------------------------------------------------
puts:
        push    si
puts_lp:
        lodsb
        test    al, al
        jz      puts_done
        mov     dl, al
        mov     ah, 02h
        int     21h
        jmp     puts_lp
puts_done:
        pop     si
        ret

; ---------------------------------------------------------------------------
; PRINT_AT -- DL=x, DH=y, DS:SI=строка; INT 10h AH=02h + AH=09h, атрибут 07h
; ---------------------------------------------------------------------------
print_at:
        push    si
        push    dx
        mov     ah, 02h
        xor     bh, bh                  ; видеостраница 0
        int     10h
print_lp:
        lodsb
        test    al, al
        jz      print_done
        push    ax
        mov     ah, 09h
        mov     bl, 07h                 ; серый на чёрном
        mov     cx, 1
        int     10h
        pop     ax
        pop     dx
        inc     dl
        cmp     dl, 80
        jb      print_next
        xor     dl, dl
        inc     dh
        cmp     dh, 25
        jb      print_next
        mov     dh, 24
print_next:
        push    dx
        mov     ah, 02h
        xor     bh, bh
        int     10h
        jmp     print_lp
print_done:
        pop     dx
        pop     si
        ret

; ---------------------------------------------------------------------------
; CLRSCR -- очистка экрана INT 10h AH=06h и курсор в (0,0)
; ---------------------------------------------------------------------------
clrscr:
        mov     ax, 0600h
        mov     bh, 07h
        xor     cx, cx                  ; CH=0, CL=0
        mov     dh, 24
        mov     dl, 79
        int     10h
        mov     ah, 02h
        xor     bh, bh
        xor     dx, dx
        int     10h
        ret

; ---------------------------------------------------------------------------
; APPEND_CHAR -- AL=байт -> конец SCRATCH (лимит 255)
; ---------------------------------------------------------------------------
append_char:
        push    bx
        mov     bx, [scratch_len]
        cmp     bx, 255
        jae     append_char_done
        mov     [scratch+bx], al
        inc     bx
        mov     byte [scratch+bx], 0
        mov     [scratch_len], bx
append_char_done:
        pop     bx
        ret

; ---------------------------------------------------------------------------
; APPEND_STR -- DS:SI=NUL-строка -> конец SCRATCH
; ---------------------------------------------------------------------------
append_str:
        push    si
append_str_lp:
        lodsb
        test    al, al
        jz      append_str_done
        call    append_char
        jmp     append_str_lp
append_str_done:
        pop     si
        ret

; ---------------------------------------------------------------------------
; APPEND_INT -- AX=знаковое 16-битное число -> десятичный текст в SCRATCH
; ---------------------------------------------------------------------------
append_int:
        push    bx
        push    cx
        push    dx
        test    ax, ax
        jns     append_int_body
        push    ax
        mov     al, '-'
        call    append_char
        pop     ax
        neg     ax
append_int_body:
        xor     cx, cx
        mov     bx, 10
append_int_div:
        xor     dx, dx
        div     bx
        push    dx
        inc     cx
        test    ax, ax
        jnz     append_int_div
append_int_dig:
        pop     dx
        add     dl, '0'
        mov     al, dl
        call    append_char
        loop    append_int_dig
        pop     dx
        pop     cx
        pop     bx
        ret

; ---------------------------------------------------------------------------
; APPEND_LEN -- DS:SI=строка -> длина десятичным числом в SCRATCH
; ---------------------------------------------------------------------------
append_len:
        push    si
        xor     cx, cx
append_len_lp:
        lodsb
        test    al, al
        jz      append_len_done
        inc     cx
        jmp     append_len_lp
append_len_done:
        mov     ax, cx
        pop     si
        call    append_int
        ret

; ---------------------------------------------------------------------------
; INPUT_LINE -- DS:DX = буфер [max][len][...]. Читает до Enter, ставит NUL,
; печатает CRLF. Строка доступна по адресу буфер+2.
; ---------------------------------------------------------------------------
input_line:
        push    dx
        mov     ah, 0Ah
        int     21h
        pop     dx
        push    si
        mov     si, dx
        lodsb                           ; max
        lodsb                           ; фактическая длина
        xor     ah, ah
        add     si, ax
        mov     byte [si], 0            ; CR заменяем на NUL
        mov     dl, 13
        mov     ah, 02h
        int     21h
        mov     dl, 10
        mov     ah, 02h
        int     21h
        pop     si
        ret

; ---------------------------------------------------------------------------
; STR2INT -- DS:SI = десятичная строка -> AX = знаковое целое
; ---------------------------------------------------------------------------
str2int:
        push    bx
        push    cx
        push    si
        xor     bx, bx                  ; накопитель
        xor     cx, cx                  ; флаг минуса
        mov     al, [si]
        inc     si
        cmp     al, '-'
        jne     str2int_lp
        mov     cx, 1
        mov     al, [si]
        inc     si
str2int_lp:
        cmp     al, '0'
        jb      str2int_end
        cmp     al, '9'
        ja      str2int_end
        push    ax
        mov     ax, bx
        mov     dx, 10
        mul     dx                      ; AX = накопитель * 10
        mov     bx, ax
        pop     ax
        sub     al, '0'
        xor     ah, ah
        add     bx, ax
        mov     al, [si]
        inc     si
        jmp     str2int_lp
str2int_end:
        test    cx, cx
        jz      str2int_pos
        neg     bx
str2int_pos:
        mov     ax, bx
        pop     si
        pop     cx
        pop     bx
        ret

; ---------------------------------------------------------------------------
; STRCPY_DS -- DS:SI -> DS:DI, включая завершающий ноль
; ---------------------------------------------------------------------------
strcpy_ds:
        push    si
        push    di
strcpy_lp:
        lodsb
        mov     [di], al
        inc     di
        test    al, al
        jnz     strcpy_lp
        pop     di
        pop     si
        ret

; ---------------------------------------------------------------------------
; CMP_STR -- DS:SI и DS:DI -> AX = -1 / 0 / 1
; ---------------------------------------------------------------------------
cmp_str:
        push    si
        push    di
cmp_str_lp:
        mov     al, [si]
        mov     dl, [di]
        cmp     al, dl
        ja      cmp_str_gt
        jb      cmp_str_lt
        test    al, al
        jz      cmp_str_eq
        inc     si
        inc     di
        jmp     cmp_str_lp
cmp_str_gt:
        mov     ax, 1
        jmp     cmp_str_done
cmp_str_lt:
        mov     ax, -1
        jmp     cmp_str_done
cmp_str_eq:
        xor     ax, ax
cmp_str_done:
        pop     di
        pop     si
        ret

; ---------------------------------------------------------------------------
; APPEND_FDIV -- AX / BX -> "x.xxxx" (дробная часть 4 знака, без хвоста)
; ---------------------------------------------------------------------------
append_fdiv:
        push    bx
        push    cx
        test    bx, bx
        jz      append_fdiv_zero
        mov     cx, 10000
        mul     cx                      ; DX:AX = AX * 10000
        pop     cx
        div     bx                      ; AX = AX*10000 / BX
        push    ax
        mov     bx, 10000
        xor     dx, dx
        div     bx                      ; AX = целая, DX = дробная
        push    dx
        call    append_int
        mov     al, '.'
        call    append_char
        pop     ax                      ; AX = дробная часть
        ; Четыре цифры дроби без цикла (исключаем зацикливание).
        xor     dx, dx
        mov     bx, 1000
        div     bx
        push    dx
        add     al, '0'
        call    append_char
        pop     ax
        xor     dx, dx
        mov     bx, 100
        div     bx
        push    dx
        add     al, '0'
        call    append_char
        pop     ax
        xor     dx, dx
        mov     bx, 10
        div     bx
        push    dx
        add     al, '0'
        call    append_char
        pop     ax
        xor     dx, dx
        mov     bx, 1
        div     bx
        push    dx
        add     al, '0'
        call    append_char
        pop     ax
        call    trim_frac
        pop     ax
        pop     bx
        ret
append_fdiv_zero:
        pop     cx
        pop     bx
        mov     ax, 0
        call    append_int
        ret

; Убирает нули в конце дробной части и точку, если дробь пуста.
trim_frac:
        push    bx
        push    cx
        mov     bx, [scratch_len]
        mov     cx, 5                   ; защита от зацикливания
trim_lp:
        test    bx, bx
        jz      trim_done
        test    cx, cx
        jz      trim_done
        cmp     byte [scratch+bx-1], '0'
        jne     trim_stop
        dec     bx
        dec     cx
        jmp     trim_lp
trim_stop:
        cmp     byte [scratch+bx-1], '.'
        jne     trim_done
        dec     bx
trim_done:
        mov     byte [scratch+bx], 0
        mov     [scratch_len], bx
        pop     cx
        pop     bx
        ret

; DS:SI -> AX = длина NUL-строки
strlen_ax:
        push    si
        xor     ax, ax
strlen_ax_lp:
        lodsb
        test    al, al
        jz      strlen_ax_done
        inc     ax
        jmp     strlen_ax_lp
strlen_ax_done:
        pop     si
        ret

; Деление на ноль из сгенерированного math{}
div0_error:
        mov     si, div0_msg
        call    puts
        mov     ax, 4C01h
        int     21h
div0_msg:
        db      'math: division by zero', 0

; CX = количество элементов -> AX = случайный индекс [0..CX-1]
rand_index:
        push    bx
        push    cx
        push    dx
        mov     bx, cx                  ; BX = количество элементов
        mov     ah, 00h
        int     1Ah                     ; CX:DX = тики BIOS (CX затирается!)
        mov     ax, dx
        xor     dx, dx
        div     bx
        mov     ax, dx
        pop     dx
        pop     cx
        pop     bx
        ret

; DS:SI = строка, BX = индекс -> SI = адрес символа (CP866, 1 байт)
str_at:
        push    ax
str_at_lp:
        test    bx, bx
        jz      str_at_done
        mov     al, [si]
        test    al, al
        jz      str_at_done
        inc     si
        dec     bx
        jmp     str_at_lp
str_at_done:
        pop     ax
        ret

; DS:SI = адрес символа -> appended ровно один байт
append_char_at:
        mov     al, [si]
        call    append_char
        ret

scratch:    times 256 db 0
scratch_len dw 0
tmp1:       times 256 db 0

v0   db 255, 0
          times 256 db 0
v1   db 255, 0
          times 256 db 0
v2   db 255, 0
          times 256 db 0
v3   db 255, 0
          times 256 db 0
v4   db 255, 0
          times 256 db 0
v5   db 255, 0
          times 256 db 0
v6   db 255, 0
          times 256 db 0
v7   db 255, 0
          times 256 db 0
lst0   dw s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15, s16, s17, s18, s19, s20, s21, s22, s23, s24, s25, s26, s27, s28, s29, s30, s31, s32, s33, s34, s35, s36
s0   db 0x91,0xA4,0xA2,0xA8,0xA3,0x3A,0x20, 0
s1   db 0xAF,0xE0,0xA8,0xA2,0xA5,0xE2,0xAC,0xA8,0xE0, 0
s2   db 0, 0
s3   db 0x98,0xA8,0xE4,0xE0,0x3A,0x20, 0
s4   db 0xA0, 0
s5   db 0xA1, 0
s6   db 0xA2, 0
s7   db 0xA3, 0
s8   db 0xA4, 0
s9   db 0xA5, 0
s10   db 0xF1, 0
s11   db 0xA6, 0
s12   db 0xA7, 0
s13   db 0xA8, 0
s14   db 0xA9, 0
s15   db 0xAA, 0
s16   db 0xAB, 0
s17   db 0xAC, 0
s18   db 0xAD, 0
s19   db 0xAE, 0
s20   db 0xAF, 0
s21   db 0xE0, 0
s22   db 0xE1, 0
s23   db 0xE2, 0
s24   db 0xE3, 0
s25   db 0xE4, 0
s26   db 0xE5, 0
s27   db 0xE6, 0
s28   db 0xE7, 0
s29   db 0xE8, 0
s30   db 0xE9, 0
s31   db 0xEA, 0
s32   db 0xEB, 0
s33   db 0xEC, 0
s34   db 0xED, 0
s35   db 0xEE, 0
s36   db 0xEF, 0
