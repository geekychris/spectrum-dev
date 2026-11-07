/* The game's state, in 8K page 46, which main.c maps at 0x0000 (over the
   ROM) for good once the IM 2 interrupts are on (the Makefile puts this
   file's variables there). */
#include "game.h"

GameState gs;
