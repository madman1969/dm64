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
        .export _blit_row, _stamp_cell, _bm_col, _bm_row
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2


        .segment "DATA"

_bm_col:    .byte 0
_bm_row:    .byte 0
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

; void __fastcall__ stamp_cell (unsigned char glyph);
;   A letter of DM's inscription font (dmview.py: 8 bytes of pattern, 8 of mask, at $DC00) into
;   cell bm_col, bm_row, which turns white.
HI_INSCRIPTION = $DC00

_stamp_cell:
        ldx #0
        stx ptr1+1
        asl
        rol ptr1+1
        asl
        rol ptr1+1
        asl
        rol ptr1+1
        asl
        rol ptr1+1
        sta ptr1
        lda ptr1+1
        clc
        adc #>HI_INSCRIPTION
        sta ptr1+1
        jsr cell_at
        lda $01
        pha
        lda #$34
        sta $01
        ldy #7
@byte:  tya
        ora #8
        tay
        lda (ptr1),y                ; the mask
        eor #$FF
        sta tmp1
        tya
        and #7
        tay
        lda (ptr2),y
        and tmp1
        ora (ptr1),y                ; the pattern
        sta (ptr2),y
        dey
        bpl @byte
        pla
        sta $01
        lda #1                      ; white
        ldy #0
        sta (ptr3),y
        rts

; void __fastcall__ object_row (const unsigned char* row);
;   One row of an object sprite (dmview.py's object_sprite: for each 4 double pixels a byte of
;   pattern, one of mask; ob_cells of them) at pixel row ob_y of the bitmap, starting at cell
;   column ob_col (may be off the left: those cells are skipped, as are those past 27) and
;   ob_shift double pixels further right. Cells it covers take colour ob_colour, if below 16.
        .export _object_row, _ob_cells, _ob_col, _ob_shift, _ob_y, _ob_colour

        .segment "DATA"
_ob_cells:  .byte 0
_ob_col:    .byte 0
_ob_shift:  .byte 0
_ob_y:      .byte 0
_ob_colour: .byte 255
ocol:       .byte 0
oleft:      .byte 0
ocarry_p:   .byte 0
ocarry_m:   .byte 0
onew_p:     .byte 0
onew_m:     .byte 0

        .segment "CODE"
_object_row:
        sta ptr1
        stx ptr1+1
        lda _ob_y
        lsr
        lsr
        lsr
        sta _bm_row
        lda #0
        sta _bm_col
        jsr cell_at                 ; ptr2, ptr3: the start of that row of cells
        lda _ob_y
        and #7
        clc
        adc ptr2
        sta ptr2
        bcc :+
        inc ptr2+1
:       lda _ob_col                 ; then to the first column (signed)
        sta ocol
        bpl @right
        ; left of the view: move back by 8 bytes a column
        eor #$FF
        clc
        adc #1                      ; how many columns left
        tax
@back:  lda ptr2
        sec
        sbc #8
        sta ptr2
        bcs :+
        dec ptr2+1
:       lda ptr3
        bne :+
        dec ptr3+1
:       dec ptr3
        dex
        bne @back
        jmp @ready
@right: tax
        beq @ready
@fwd:   lda ptr2
        clc
        adc #8
        sta ptr2
        bcc :+
        inc ptr2+1
:       inc ptr3
        bne :+
        inc ptr3+1
:       dex
        bne @fwd
@ready: lda _ob_cells
        ldx _ob_shift
        beq :+
        clc
        adc #1                      ; a shifted row spills into one more cell
:       sta oleft
        lda #0
        sta ocarry_p
        sta ocarry_m
        lda $01
        pha
        lda #$35
        sta $01

@cell:  lda _ob_cells               ; past the input: only the carry is left
        beq @empty
        dec _ob_cells
        ldy #0
        lda (ptr1),y
        sta onew_p
        iny
        lda (ptr1),y
        sta onew_m
        lda ptr1
        clc
        adc #2
        sta ptr1
        bcc @shift
        inc ptr1+1
        bcs @shift
@empty: lda #0
        sta onew_p
        sta onew_m
@shift: ldx _ob_shift               ; shift right by 2 bits a double pixel, carrying over
        lda #0
        sta tmp1                    ; what falls out of the pattern
        sta tmp2                    ; ... of the mask
        cpx #0
        beq @shifted
@sh:    lsr onew_p
        ror tmp1
        lsr onew_p
        ror tmp1
        lsr onew_m
        ror tmp2
        lsr onew_m
        ror tmp2
        dex
        bne @sh
@shifted:
        lda onew_p
        ora ocarry_p
        sta onew_p
        lda tmp1
        sta ocarry_p
        lda onew_m
        ora ocarry_m
        sta onew_m
        lda tmp2
        sta ocarry_m
        lda ocol                    ; in the view?
        cmp #28
        bcs @skip                   ; (negative columns are 128 and up)
        ldy #0
        lda onew_m
        beq @skip
        eor #$FF
        and (ptr2),y
        sta tmp1
        lda onew_p
        and onew_m
        ora tmp1
        sta (ptr2),y
        lda _ob_colour
        cmp #16
        bcs @skip
        sta (ptr3),y
@skip:  inc ocol
        lda ptr2
        clc
        adc #8
        sta ptr2
        bcc :+
        inc ptr2+1
:       inc ptr3
        bne :+
        inc ptr3+1
:       dec oleft
        beq :+
        jmp @cell
:       pla
        sta $01
        rts
