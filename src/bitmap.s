;
; bitmap.s - drawing into the bitmap (rows 0-16 of the screen: irq.s), which is in the RAM under
; the KERNAL at $E000, so these switch it out while they work (interrupts go on: irq.s). A cell's
; fourth colour is its byte of colour RAM.
;
; void __fastcall__ blit_row (const unsigned char* row);
;   One row of one of DMWAL's pieces (tools/dmview.py): its cell row, first column, number of
;   cells, then each cell: a flag (bit 0 drawn, bit 1 only partly covered, bit 2 sets the cell's
;   colour to bits 4-7), and if drawn its 8 bytes, and if partly covered 8 bytes of mask.
;
; void __fastcall__ stamp (unsigned char glyph);
;   A character of the tiny font or an icon (art.py: 8 bytes of mask each, at HI_BMGLYPHS) into
;   cell bm_col, bm_row: the pixels it covers become bm_ink (a colour's two bits repeated: $55,
;   $AA or $FF), and if bm_colour is below 16 it becomes the cell's colour.
;

        .export _blit_row, _stamp, _bm_col, _bm_row, _bm_ink, _bm_colour
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2

HI_BMGLYPHS = $D800

        .segment "DATA"

_bm_col:    .byte 0
_bm_row:    .byte 0
_bm_ink:    .byte $FF
_bm_colour: .byte 255
count:      .byte 0
flag:       .byte 0

        .segment "CODE"

; ptr2 = the bitmap's bytes for cell (bm_col, bm_row), ptr3 its colour RAM.
cell_at:
        lda _bm_row
        asl
        asl
        clc
        adc _bm_row
        sta ptr3
        lda #0
        sta ptr3+1
        asl ptr3                    ; row * 40
        rol ptr3+1
        asl ptr3
        rol ptr3+1
        asl ptr3
        rol ptr3+1
        lda ptr3
        clc
        adc _bm_col
        sta ptr3
        sta ptr2
        lda ptr3+1
        adc #0
        sta ptr2+1
        adc #$D8
        sta ptr3+1
        asl ptr2                    ; the bitmap: 8 bytes a cell
        rol ptr2+1
        asl ptr2
        rol ptr2+1
        asl ptr2
        rol ptr2+1
        lda ptr2+1
        adc #$E0                    ; carry is clear: under $2000
        sta ptr2+1
        rts

_blit_row:
        sta ptr1
        stx ptr1+1
        ldy #0
        lda (ptr1),y
        sta _bm_row
        iny
        lda (ptr1),y
        sta _bm_col
        iny
        lda (ptr1),y
        sta count
        jsr cell_at
        lda ptr1
        clc
        adc #3
        sta ptr1
        bcc :+
        inc ptr1+1
:       lda $01
        pha
        lda #$35                    ; the KERNAL out, I/O (colour RAM) in
        sta $01

@cell:  ldy #0
        lda (ptr1),y
        sta flag
        inc ptr1
        bne :+
        inc ptr1+1
:       lsr
        bcc @next                   ; nothing here
        lda flag
        and #4
        beq @nocolour
        lda flag
        lsr
        lsr
        lsr
        lsr
        sta (ptr3),y                ; y is 0
@nocolour:
        lda flag
        and #2
        bne @partial
        ldy #7
@copy:  lda (ptr1),y
        sta (ptr2),y
        dey
        bpl @copy
        lda #8
        bne @advance
@partial:
        lda ptr1
        clc
        adc #8
        sta ptr4
        lda ptr1+1
        adc #0
        sta ptr4+1
        ldy #7
@mask:  lda (ptr4),y
        eor #$FF
        and (ptr2),y
        ora (ptr1),y
        sta (ptr2),y
        dey
        bpl @mask
        lda #16
@advance:
        clc
        adc ptr1
        sta ptr1
        bcc @next
        inc ptr1+1
@next:  lda ptr2
        clc
        adc #8
        sta ptr2
        bcc :+
        inc ptr2+1
:       inc ptr3
        bne :+
        inc ptr3+1
:       dec count
        bne @cell
        pla
        sta $01
        rts

_stamp:
        ldx #0
        stx ptr1+1
        asl
        rol ptr1+1
        asl
        rol ptr1+1
        asl
        rol ptr1+1
        sta ptr1
        lda ptr1+1
        clc
        adc #>HI_BMGLYPHS
        sta ptr1+1
        jsr cell_at
        lda $01
        pha
        lda #$34                    ; all RAM: the glyphs are under I/O, the bitmap the KERNAL
        sta $01
        ldy #7
@pixel: lda (ptr1),y
        sta tmp1
        eor #$FF
        and (ptr2),y
        sta tmp2
        lda tmp1
        and _bm_ink
        ora tmp2
        sta (ptr2),y
        dey
        bpl @pixel
        pla
        sta $01
        lda _bm_colour
        cmp #16
        bcs :+
        ldy #0
        sta (ptr3),y
:       rts
