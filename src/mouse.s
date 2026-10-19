;
; mouse.s - the mouse pointer, for cc65's mouse drivers: sprite 7, whose image is in the RAM
; under the KERNAL (dmconv.py puts it there, hw_init points sprite 7 at it). The drivers call
; these from their interrupt handler, so they're in assembly, and leave the C stack alone.
;

        .export _mouse_callbacks

SPR7_X  = $D00E
SPR7_Y  = $D00F
SPR_HIX = $D010
SPR_ENA = $D015

.rodata

_mouse_callbacks:
        .addr   hide, show, prep, prep, movex, movey

.code

hide:   lda     SPR_ENA
        and     #$7F
        sta     SPR_ENA
prep:   rts

show:   lda     SPR_ENA
        ora     #$80
        sta     SPR_ENA
        rts

; X position in A (low) and X (high), 0-319: the sprite's is 24 more, its 9th bit in SPR_HIX.
movex:  clc
        adc     #24
        sta     SPR7_X
        txa
        adc     #0
        beq     @low
        lda     SPR_HIX
        ora     #$80
        sta     SPR_HIX
        rts
@low:   lda     SPR_HIX
        and     #$7F
        sta     SPR_HIX
        rts

; Y position in A, 0-199: the sprite's is 50 more.
movey:  clc
        adc     #50
        sta     SPR7_Y
        rts
