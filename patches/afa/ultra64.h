#ifndef AFA_ULTRA64_H
#define AFA_ULTRA64_H

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;
typedef float f32;
typedef double f64;

typedef u32 uintptr_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

#define ARRAY_COUNT(arr) (s32)(sizeof(arr) / sizeof(arr[0]))

#endif
