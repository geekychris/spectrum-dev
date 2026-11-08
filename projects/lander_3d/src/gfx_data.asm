; the sprite patterns (bank 25) and the song's rows (bank 26), made at build
; time by tools/gen_gfx.py and tools/make_music.py (the Makefile)

SECTION BANK_25
BINARY "build/gen/sprites.bin"
SECTION BANK_26
BINARY "build/gen/music_rows.bin"
