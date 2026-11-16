/* Ace Pilot's autopilot and state hash (bot.c): resident, and built on the
   host by tools/hash_host.c with the same rules, so the test can compare the
   Next's game with the host's tick by tick. */
#ifndef ACE_BOT_H
#define ACE_BOT_H
#include <stdint.h>
#include "game.h"
extern UWORD ace_bot(const GameWorld *w, uint8_t pi);   /* the original's input flags */
extern uint16_t ace_hash(const GameWorld *w);
#endif
