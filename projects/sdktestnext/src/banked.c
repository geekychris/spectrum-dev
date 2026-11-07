/* Data in an 8K page the program never maps (PAGE_84, linked at 0x54C000):
   the harness's tests read and write it by symbol (test_features.py). */
#include <stdint.h>

const char banked_msg[] = "BANKED PAGE 84";
uint8_t banked_var[4] = {1, 2, 3, 4};
