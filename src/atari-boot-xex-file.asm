	;; Atari Boot XEX File
	;;
	;; Does a binary load starting at the next sector
	;; Just raw sectors; detect end when address is 0000
	;;
	;; This uses 0100-017F as the sector buffer (the bottom of the stack)
	;; The code is in page 4.  Hopefully this will minimize conflicts.
	;;
	;; Copyright (c) 2026 Preston Crow
	;; Released under the same licensing as tnfsd
	;; 
;System equates used
	BOOT	= $09
	FMSZPG	= $43
	COLDST	= $0244
	RUNAD	= $02E0
	INITAD	= $02E2
	DBUF	= $0304		; word
	DBUFHI	= $0305
	DAUX1	= $030A		; sector number
	DAUX2	= $030B
	BASICF	= $03F8
	PORTB	= $D301
	COLDSV	= $E477
;End of system equates
;Zero-page equates
;End of zero-page equates
	;; Local equates
	LOADAT  = FMSZPG
	LOADEND = FMSZPG+2
	SAVEX	= FMSZPG+4
	SECBUF  = $0100
	START	= $0700		; Note - Didn't work at $0400
	*= START
	;; boot sector header
	.byte $00 ;dos flag
	.byte CODESECTORS
	.byte START&255,START/256
	.byte COLDSV&255,COLDSV/256 ; DOSINI vector on warm start (coldstart)
	;; code starts executing here
	;; disable BASIC
	LDA PORTB
	ORA #$02
	STA PORTB
	LDA #$01
	STA BASICF	; BASIC shadow register (any non-zero disables on reset)
	STA BOOT	; 1 indicates disk boot (will trigger DOSINI on warm boot)
	;; Start binary load
	INC DAUX1
	LDX #$80	; Bytes read in current sector; force new sector read on next getbyte
	LDA #SECBUF/256
	STA DBUF+1
	LDA #SECBUF & 255
	STA DBUF
	;; Get load/end address, skipping $FFFF header if any
GETADR	JSR GETBYTE
	STA LOADAT
	JSR GETBYTE
	STA LOADAT+1
	AND LOADAT
	CMP #$FF	; Are both bytes FF
	BEQ GETADR
	JSR GETBYTE
	STA LOADEND
	JSR GETBYTE
	STA LOADEND+1
	ORA LOADEND
	BEQ DONE	; End address 0000
	;; Increment LOADEND by 1 to point past the last byte (makes comparisons simpler)
	INC LOADEND
	BNE GETDATA
	INC LOADEND+1
	;; Now load bytes from LOADAT to LOADEND (LOADEND is now exclusive)
GETDATA JSR FULLSEC
	BCC CHKDONE	; Check if block is done
	JSR GETBYTE	; Need to read byte-by-byte
	LDY #$00
	STA (LOADAT),Y
	;; Increment the loadat address
	INC LOADAT
	BNE CHKDONE
	INC LOADAT+1
	BEQ BLKDONE	; error - past $FFFF, force end of block; bad things are happening
	;; Check if this was the last byte in the block
CHKDONE	LDA LOADEND+1
	CMP LOADAT+1
	BNE GETDATA
	LDA LOADEND
	CMP LOADAT
	BNE GETDATA	; nope - get more data
	;; Check if init is set
BLKDONE	LDA INITAD
	ORA INITAD+1
	BEQ GETADR	; no init routine - next memory block
	STX SAVEX
	JSR DOINIT
	LDX SAVEX
	LDY #$00
	STY INITAD
	STY INITAD+1
	BEQ GETADR	; unconditional branch to next memory block
DOINIT:	JMP (INITAD)	; no JSR indirect instruction
DONE:	JMP (RUNAD)	; hope this was set
	;; Read one byte of data, reloading buffer from disk if needed
GETBYTE CPX #$80
	BNE SAMESEC
READSEC	JSR $E453
	BMI READSEC	; error - keep re-reading
	;; set next sector - current sector + 1
RSDONE	INC DAUX1
	BNE HISECOK
	INC DAUX2
HISECOK	LDX #$00 	; byte offset into buffer
	STX COLDST	; do not reboot on reset (here to save two bytes)
SAMESEC LDA SECBUF,X
	INX
	RTS
	;; Try to read a full sector into the target location directly
FULLSEC CPX #$80
	BNE NOFULL
	SEC
	LDA LOADEND
	SBC LOADAT
	BMI DOFULL	; >= 128 bytes left to load
	LDA LOADEND+1
	SBC LOADAT+1
	BNE DOFULL	; >= 256 bytes left to load
NOFULL	SEC
	RTS
DOFULL	LDA LOADAT	; switch to direct sector load
	STA DBUF
	LDA LOADAT+1
	STA DBUF+1
READSC2	JSR $E453
	BMI READSC2	; error - keep re-reading
	LDX #$80	; restore value - next byte requires another full sector
	INC DAUX1
	BNE HISECOK2
	INC DAUX2
HISECOK2:
	LDA #SECBUF & 255 ; back to regular buffer (this will be LDA #00)
	STA DBUF
	LDA #SECBUF/256
	STA DBUF+1
	;; Update LOADAT
	CLC
	LDA LOADAT
	ADC #$80
	STA LOADAT
	BCC NOHILA
	INC LOADAT+1
NOHILA:
	CLC	
	RTS
	END = *
	CODESIZE = END - START
	CODESECTORS = (CODESIZE + 127) / 128
;	.END      
