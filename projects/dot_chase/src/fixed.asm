; Dot Chase's data at fixed addresses below the program (see main.c's memory
; map): free once the tape loader has run, so it costs no program space.

PUBLIC _game
defc _game = 0x7B00             ; GameState (game.h), 1080 bytes
