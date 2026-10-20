;
; unpackasm.s - the inner loop of unpack.c: unpacks lz_pack's bytes (tools/dmview.py) through a
; 1K ring, as fast as the input comes. In overlay 4 with unpack.c.
;
; unsigned int __fastcall__ unpack_run (unsigned char* ring);
;   ring: 1K, page-aligned, followed by a page for the input. It calls unpack_refill (puts up to
;   255 bytes after the ring, returns how many: 0 at the end) and unpack_flush (the ring's 1K is
;   full) in unpack.c, and returns how many bytes are in the ring at the end.
;
; The ring's pages are written as (wpage):X and read as (rpage):Y, by changing the instructions'
; own addresses; the C functions it calls may change any register, so X and Y are kept round them.
;

        .export _unpack_run
        .import _unpack_refill, _unpack_flush

        .segment "OVL5"

_unpack_run:
        stx ring_hi
        stx wpage
        txa
        clc
        adc #4
        sta ring_end
        sta inpage
        lda #0
        sta inpos
        sta inlen
        tax

token:  jsr gettoken
        bcs finish
        cmp #$80
        bcs match
        adc #1                  ; a run of A + 1 bytes (carry is clear)
        sta count
lit:    jsr getb
        jsr put
        dec count
        bne lit
        beq token

match:  pha
        jsr getb
        sta back_lo             ; how far back, less 1
        pla
        pha
        and #3
        sta back_hi
        pla
        lsr
        lsr
        and #31
        clc
        adc #3
        sta count
        txa                     ; from = where it's writing - (back + 1); carry is clear
        sbc back_lo
        tay
        lda wpage
        sbc back_hi
        cmp ring_hi
        bcs @in
        adc #4                  ; carry is clear: wrapped round the ring
@in:    sta rpage
mloop:  lda $FF00,y
rpage   = * - 1
        jsr put
        iny
        bne @next
        inc rpage
        lda rpage
        cmp ring_end
        bne @next
        lda ring_hi
        sta rpage
@next:  dec count
        bne mloop
        beq token

finish: txa                     ; the bytes in the ring
        pha
        lda wpage
        sec
        sbc ring_hi
        tax
        pla
        rts

; Writes A to the ring; flushes it when full.
put:    sta $FF00,x
wpage   = * - 1
        inx
        beq @page
        rts
@page:  inc wpage
        lda wpage
        cmp ring_end
        bne @done
        sty ysave
        jsr _unpack_flush
        ldy ysave
        lda ring_hi
        sta wpage
        ldx #0
@done:  rts

; The next byte, at a token: carry set at the end of the input.
gettoken:
        ldy inpos
        cpy inlen
        bne have
        jsr refill
        bne have
        sec
        rts

; The next byte, inside a token (0s if the input ended early: unpack.c sees it's short).
getb:   ldy inpos
        cpy inlen
        bne have
        jsr refill
        bne have
        lda #0
        rts

have:   lda $FF00,y
inpage  = * - 1
        iny
        sty inpos
        clc
        rts

refill: stx xsave
        jsr _unpack_refill
        ldx xsave
        ldy #0
        sty inpos
        sta inlen
        cmp #0
        rts

ring_hi:  .byte 0
ring_end: .byte 0
inpos:    .byte 0
inlen:    .byte 0
count:    .byte 0
back_lo:  .byte 0
back_hi:  .byte 0
xsave:    .byte 0
ysave:    .byte 0
