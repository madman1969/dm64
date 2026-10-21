;
; irq.s - the raster split: the top 17 rows of the screen are a multicolour bitmap (the view and
; the panel beside it), the rest text. A raster interrupt near the top switches the bitmap on, and
; one at the end of row 16 switches back to text. With split_on 0 the whole screen is text (the
; inventory, map, help and recruiting screens).
;
; The bitmap shares the text screen's memory ($CC00) as its colours (view.c), so the sprites'
; pointers are the same for both, and only these registers change:
;   $D011 bitmap on/off, $D016 multicolour on/off, $D018 bitmap at $E000 / characters at $D000,
;   $D021 the bitmap's background (grey) / the text's (black).
;
; The view is drawn with the KERNAL switched out (bitmap.s), so that interrupts can go on
; meanwhile: the RAM under the KERNAL's vectors points to irq_ram, which switches it back in for
; the KERNAL's handler, and out again afterwards.
;

        .export _irq_vectors, _irq_init, _split_on, _split_bg
        .import sound_frame

        .segment "DATA"

_split_on:  .byte 0
_split_bg:  .byte 12                ; the bitmap's background: darker in the dark (view.c)
old_irq:    .word 0
port:       .byte 0                 ; $01 as it normally is: the KERNAL and I/O in
phase:      .byte 0

TOP_LINE    = 20                    ; in the top border
SPLIT_LINE  = 50 + 17 * 8 - 2       ; two lines before the text starts

        .segment "CODE"

; The vectors seen when the KERNAL is switched out: first thing, before anything switches it out.
_irq_vectors:
        sei
        lda $01
        sta port
        lda #<irq_ram
        sta $FFFE
        lda #>irq_ram
        sta $FFFF
        lda #<nmi_ram
        sta $FFFA
        lda #>nmi_ram
        sta $FFFB
        cli
        rts

; The split, once the screen has moved (hw.c).
_irq_init:
        sei
        lda $0314
        sta old_irq
        lda $0315
        sta old_irq + 1
        lda #<split
        sta $0314
        lda #>split
        sta $0315
        lda #TOP_LINE
        sta $D012
        lda $D011
        and #$7F
        sta $D011
        lda #0
        sta phase
        lda #1
        sta $D019
        sta $D01A                   ; raster interrupts on
        cli
        rts

; An interrupt with the KERNAL switched out: in again for its handler (which ends with an RTI to
; irq_back, made to look like the interrupted code), then out again.
irq_ram:
        pha
        lda $01
        pha
        lda port
        sta $01
        lda #>irq_back
        pha
        lda #<irq_back
        pha
        lda #$04                    ; the flags its RTI restores: interrupts off, not BRK
        pha
        jmp $FF48
irq_back:
        pla
        sta $01
        pla
nmi_ram:
        rti

; Reached through $0314: the raster's interrupts are the split; others go on to cc65's handler
; (the mouse) and the KERNAL's.
split:
        lda $D019
        and #1
        bne @raster
        jmp (old_irq)
@raster:
        sta $D019
        lda phase
        bne @bottom
        jsr sound_frame             ; once a frame, at the top
        lda phase
        lda _split_on
        beq @text                   ; all text
        lda #$3B
        ldx #$18
        ldy #$38
        sta $D011
        stx $D016
        sty $D018
        lda _split_bg
        sta $D021
        lda #SPLIT_LINE
        ldx #1
        bne @next
@bottom:
        lda #SPLIT_LINE + 2         ; the text's first line is a bad line: switch just before it
@wait:  cmp $D012
        bne @wait
        ldx #8
@delay: dex
        bne @delay
@text:  lda #$1B
        ldx #$08
        ldy #$34
        sta $D011
        stx $D016
        sty $D018
        lda #0
        sta $D021
@done:  lda #TOP_LINE
        ldx #0
@next:  sta $D012
        stx phase
        jmp $EA81
