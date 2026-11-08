/* made by tools/gen_course.py */
#ifndef DATA_H
#define DATA_H
#include <stdint.h>
static const uint8_t course_page[6] = { 36, 62, 94, 121, 148, 176 };   /* physics: header page */
static const uint8_t pic_page[6] = { 55, 86, 113, 140, 169, 195 };      /* pictures */
static const uint8_t obj_page[6] = { 208, 208, 210, 210, 212, 216 };   /* moving scenery */
static const uint16_t obj_addr[6] = { 0x4000, 0x4020, 0x4109, 0x5f5b, 0x59aa, 0x4063 };
#define DATA_LAST_PAGE 203
#endif
