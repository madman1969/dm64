;
; sound.s - sound effects on the SID, made up to stand in for Dungeon Master's own (which are
; sampled sound, too much for a C64 to play alongside the game). Each effect is a note on one of
; the three voices: a waveform, an envelope, a start pitch that slides each frame, and how many
; frames before it's released. irq.s calls sound_frame 50 times a second.
;
; void __fastcall__ sfx (unsigned char effect);     SFX_ in dm.h
;

        .export _sfx, sound_frame, _sound_init

SID = $D400

        .segment "RODATA"

; Per effect: pitch (word), slide a frame (signed word), waveform, attack/decay,
; sustain/release, frames.
effects:
        .word $8500, $0000
        .byte $41, $00, $00, 3          ; 0 a click: a switch, a button
        .word $0700, $FFF8
        .byte $81, $0A, $00, 25         ; 1 a door grinding
        .word $2000, $FF00
        .byte $81, $08, $00, 6          ; 2 a blow landing
        .word $1800, $FF80
        .byte $21, $09, $00, 12         ; 3 a champion hurt
        .word $0600, $0000
        .byte $81, $05, $00, 2          ; 4 bumping into a wall
        .word $3000, $0200
        .byte $81, $1A, $00, 6          ; 5 a swing that misses
        .word $4000, $FD00
        .byte $81, $0A, $00, 14         ; 6 a fireball flying
        .word $1200, $FFC0
        .byte $81, $0C, $00, 30         ; 7 an explosion
        .word $1000, $0180
        .byte $11, $0A, $00, 16         ; 8 a spell
        .word $0700, $0020
        .byte $21, $2A, $00, 18         ; 9 a creature's growl
        .word $3000, $FF00
        .byte $41, $1A, $00, 14         ; 10 ... screech
        .word $1800, $0000
        .byte $41, $0A, $00, 12         ; 11 ... buzz
        .word $0400, $FFF8
        .byte $21, $3B, $00, 30         ; 12 ... roar
        .word $2400, $0400
        .byte $11, $05, $00, 3          ; 13 picking up, putting down
        .word $0C00, $FFA0
        .byte $11, $08, $00, 8          ; 14 eating, drinking
        .word $1400, $FFA0
        .byte $21, $0B, $00, 24         ; 15 a creature slain
        .word $0800, $0300
        .byte $11, $0C, $00, 20         ; 16 a teleporter
        .word $2800, $FF00
        .byte $21, $0C, $00, 25         ; 17 falling

        .segment "DATA"

next:       .byte 0                     ; the voice the next effect plays on
frames:     .byte 0, 0, 0               ; per voice: frames left before release
freq_lo:    .byte 0, 0, 0
freq_hi:    .byte 0, 0, 0
slide_lo:   .byte 0, 0, 0
slide_hi:   .byte 0, 0, 0
wave:       .byte 0, 0, 0
voice:      .byte 0, 7, 14              ; each voice's registers

        .segment "CODE"

_sound_init:
        lda #$0F
        sta SID + 24                    ; volume
        lda #$08
        ldx #0
        ldy #2
@pw:    sta SID + 3, x                  ; pulse width, half
        pha
        txa
        clc
        adc #7
        tax
        pla
        dey
        bpl @pw
        rts

_sfx:
        asl
        asl
        asl
        tay                             ; the effect's record
        ldx next
        inx
        cpx #3
        bcc :+
        ldx #0
:       stx next
        lda effects, y
        sta freq_lo, x
        lda effects + 1, y
        sta freq_hi, x
        lda effects + 2, y
        sta slide_lo, x
        lda effects + 3, y
        sta slide_hi, x
        lda effects + 4, y
        sta wave, x
        lda effects + 7, y
        sta frames, x
        lda effects + 5, y              ; attack/decay
        pha
        lda effects + 6, y              ; sustain/release
        pha
        lda voice, x
        tay
        lda #0
        sta SID + 4, y                  ; the old note off, at once
        pla
        sta SID + 6, y
        pla
        sta SID + 5, y
        lda freq_lo, x
        sta SID + 0, y
        lda freq_hi, x
        sta SID + 1, y
        lda wave, x
        ora #1                          ; gate on
        sta SID + 4, y
        rts

; Each frame: pitches slide, notes run out and are released.
sound_frame:
        ldx #2
@voice: lda frames, x
        beq @next
        ldy voice, x
        lda freq_lo, x
        clc
        adc slide_lo, x
        sta freq_lo, x
        sta SID + 0, y
        lda freq_hi, x
        adc slide_hi, x
        sta freq_hi, x
        sta SID + 1, y
        dec frames, x
        bne @next
        lda wave, x                     ; gate off: the release
        sta SID + 4, y
@next:  dex
        bpl @voice
        rts
