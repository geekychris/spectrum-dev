/* made by tools/gen_drone.py: the drone's sprite frames (see there) */
#ifndef DRONE_DATA_H
#define DRONE_DATA_H
#define DRONE_PAGE     50      /* frames from here, 6 to a page */
#define DRONE_LAST_BANK 39
#define DRONE_FRAME    1280    /* bytes: 10 4-bit patterns */
#define DRONE_W        80
#define DRONE_H        32
#define DRONE_CX       40       /* the drone's middle in the box */
#define DRONE_CY       19
#define DRONE_PITCHES  9
#define DRONE_HEADINGS 5
#define DRONE_PITCH0   1200      /* pitch steps: DRONE_PITCH0 + 1000 k, then 9000 */
#endif
