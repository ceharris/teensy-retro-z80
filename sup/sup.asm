	;--------------------------------------------------------------
	; Constants
	;
con_io		equ 0x80
con_status	equ 0x81
ustack_size	equ 64


		org 0x0
	;--------------------------------------------------------------
	; RST 0 vector (system reset)
		jr sys_reset
rst_nop:
		ret

		org 0x8
	;--------------------------------------------------------------
	; RST 0x08 vector (user assignable)
		push hl
		ld hl,(ctrl_rst08)
		ex (sp),hl
		ret

		org 0x10
	;--------------------------------------------------------------
	; RST 0x10 vector (user assignable)
		push hl
		ld hl,(ctrl_rst10)
		ex (sp),hl
		ret

		org 0x18
	;--------------------------------------------------------------
	; RST 0x18 vector (user assignable)
		push hl
		ld hl,(ctrl_rst18)
		ex (sp),hl
		ret

		org 0x20
	;--------------------------------------------------------------
	; RST 0x20 vector (user assignable)
		push hl
		ld hl,(ctrl_rst20)
		ex (sp),hl
		ret

		org 0x28
	;--------------------------------------------------------------
	; RST 0x28 vector (supervisor call handler)
		jr svc_dispatch

		org 0x30
	;--------------------------------------------------------------
	; RST 0x30 vector (controller register peek)
		jp reg_peek

		org 0x38
	;--------------------------------------------------------------
	; RST 0x38 vector (user assignable)
		push hl
		ld hl,(ctrl_rst38)
		ex (sp),hl
		ret

		org 0x40
	;--------------------------------------------------------------
	; System reset handler
	;
	; This routine resets the user-assignable restart vector handlers,
	; sets up a stack in low writable memory and passes control to the
	; user program.

sys_reset:
		ld sp,ctrl_rst_top
		ld hl,rst_nop
		push hl			; set default RST 0x38 vector
		push hl			; set default RST 0x20 vector
		push hl			; set default RST 0x18 vector
		push hl			; set default RST 0x10 vector
		push hl			; set default RST 0x08 vector
		ld sp,user_prog		; set stack below the user program
		jp user_prog		; transfer control to the user program

	;--------------------------------------------------------------
	; Supervisor call dispatch
	;
	; This routine dispatches to the supervisor function identified
	; by the A register.

svc_dispatch:
        	push hl			; save caller's HL
        	ld h,svc_page		; point to SVC function table page
		add a,a			; 2 bytes per SVC entry point
 		ld l,a			; now HL points at entry point address

		ld a,(hl)		; get entry point LSB
		inc hl
		ld h,(hl)		; get entry point MSB
		ld l,a			; now HL is entry point address

        	ex (sp),hl		; now (SP) is entry point address
					; and caller's HL is restored

        	ret			; jumps to entry point

		org 0x66
	;--------------------------------------------------------------
	; NMI restart vector
	;
		jp 0x0			; system reset

		org 0x80
	;--------------------------------------------------------------
	; External controller memory area
	;
ctrl_mem:
ctrl_rst08	defw 0			; RST 0x08 handler
ctrl_rst10	defw 0			; RST 0x10 handler
ctrl_rst18	defw 0			; RST 0x18 handler
ctrl_rst20	defw 0			; RST 0x20 handler
ctrl_rst38	defw 0			; RST 0x38 handler
ctrl_rst_top:

		org 0xe8
ctrl_rset_size	equ 2*4
ctrl_rset	defs ctrl_rset_size	; general purpose registers
ctrl_rset_top:
ctrl_arset	defs ctrl_rset_size	; alternate general purpose registers
ctrl_arset_top:
ctrl_spset	defs ctrl_rset_size	; special purpose registers
ctrl_spset_top:

        	org 0x100
	;--------------------------------------------------------------
	; Start of Supervisor Control Program
	;

svc_table:
svc_page	equ svc_table/256		; page address of svc_table

_exit		defw __exit
@exit		equ (_exit - svc_table)/2

_setvec		defw __setvec
@setvec		equ (_setvec - svc_table)/2

_getc		defw __getc
@getc		equ (_getc - svc_table)/2

_putc		defw __putc
@putc		equ (_putc - svc_table)/2

_puts		defw __puts
@puts		equ (_puts - svc_table)/2


	;--------------------------------------------------------------
	; SVC: exit
	; Halts the CPU.
	; If an interrupt occurs the system restarts

__exit:
		halt
		jp 0x0

	;--------------------------------------------------------------
	; SVC: setvec
	; Sets a restart vector. Restart vectors 0x8, 0x10, 0x18, 0x20,
	; and 0x38 are user configurable. Attempts to set other vectors
	; are ignored.
	;
	; On entry:
	; 	C = restart vector to set (0x8, 0x10, 0x18, 0x20, or 0x38)
	;	HL = handler address
	;
	; On return:
	;	A is modified

__setvec:
		ld a,c			; get selected vector
		cp 0x38			; handle 0x38 as a special case
		jr z,__setvec38

		; divide vector address by 8 to get handler index
		srl a
		srl a
		srl a

		; make sure selected vector is 0x8, 0x10, 0x18, or 0x20
		ret z			; can't set vector 0
		dec a			; 0 => 0x8, 1 => 0x10, 2 => 0x18, 3 => 0x20
		cp 4			; 4 = 0x28 or above
		ret p

		; set HL to offset into table based on selected vector
		push de			; preserve register
		ex de,hl		; put handler address into DE
		ld hl,ctrl_rst08	; point to dispatch table
		add a,a			; two bytes per handler address
		add a,l			; add to address LSB
		ld l,a

		; store new handler address in table
		ld (hl),e
		inc hl
		ld (hl),d

		ex de,hl		; put handler address back into HL
		pop de			; restore register
		ret

__setvec38:
		ld (ctrl_rst38),hl	; set handler for RST 0x38
		ret

	;--------------------------------------------------------------
	; SVC: getc
	; Gets a character from console input.
	;
	; On return:
        ;   	If NZ then no input was available, otherwise A is the
	;	character that was read from the console.
	;
__getc:
		in a,(con_status)	; get status flags
		and 1			; bit 0 is input ready
		ret nz			; input ready is active low
		in a,(con_io)		; read the character
		ret

	;--------------------------------------------------------------
	; SVC: putc
	; Puts a character to console output.
	; This function blocks until the console is ready to accept output.
	;
        ; On entry:
	;   	C is the character to output
	;
	; On return:
	;	Z flag set
	;
__putc:
		in a,(con_status)	; get status flags
		and 2			; bit 1 is output ready
		jr nz,__putc		; output ready is active low
		ld a,c			; get character to output
		out (con_io),a		; write the character
		ret

	;--------------------------------------------------------------
	; SVC: puts
	; Puts a string to console output.
	; This function blocks until the entire string can be written to
	; the console.
	;
        ; On entry:
	;   	HL points to a null-terminated string
	;
	; On return:
	;	C iz zero
	;	HL points at the null terminator
	;	Z flag set
	;
__puts:
		ld a,(hl)		; get next character from string
		ld c,a			; prepare for call to _putc
		or a			; is it the null terminator?
		ret z
		call __putc		; put the charactor
		inc hl			; point to next character
		jr __puts

	;--------------------------------------------------------------
	; Controller register peek
	; This routine copies the contents of all machine registers
	; into the memory region that is mapped to the controller.

reg_peek:
		; save the registers we use
		push af
		push hl
		push ix

		; save current in SP in IX
		ld ix,0
		add ix,sp

		; store general purpose registers in controller memory
		ld sp,ctrl_rset_top
		push af
		push hl
		push de
		push bc

		; store alternate register set in controller memory
		ld sp,ctrl_arset_top
		ex af,af'
		exx
		push af
		push hl
		push de
		push bc
		exx
		ex af,af'

		; point HL to registers saved on entry
		push ix
		pop hl

		; add 8 to HL so that it equals SP before we were called
		ld a,8
		add a,l
		ld l,a
		adc a,h
		sub l
		ld h,a

		; store pre-entry SP in controller memory
		ld sp,ctrl_spset_top
		push hl

		; load HL with contents of I and R
		; and store it in controller memory
		ld a,r
		ld l,a
		ld a,i
		ld h,a
		push hl

		push iy		; store IY in controller memory

		; load HL with contents with saved value of IX
		; and store it in controller memory
		ld l,(ix+0)
		ld h,(ix+1)
		push hl

		; restore stack and saved registers
		ld sp,ix
		pop ix
		pop hl
		pop af

		ret

	;-----------------------------------------------------------------
	; Start of user memory
	;
		org 0x200
user_mem:
		defs ustack_size
user_prog:
		; write the hello string
		ld hl,hello		; point to string
		ld a,@puts		; invoke the puts function
		rst 0x28

		; add trailing newline
		ld c,newline		; character to put
		ld a,@putc		; invoke the putc function
		rst 0x28

		; exit the program
		ld a,@exit		
		rst 0x28

hello   	defb "Hello, world.", 0
newline		equ 0xa	

		end
