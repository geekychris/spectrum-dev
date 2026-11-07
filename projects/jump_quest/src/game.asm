; jq.h: the game's state (Game game) at a fixed address above the program,
; out of the contended RAM (GAME_ADDR; jq.c checks it ends below 0xFCC0)

PUBLIC _game
defc _game = 0xF200
