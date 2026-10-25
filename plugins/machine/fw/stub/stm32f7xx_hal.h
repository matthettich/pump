/* Stand-in for the STM32 HAL so the LXR-02 sound engine compiles to WebAssembly (Thunder Plus).
 * Only what the voice and parameter code use: integer types, the hardware random number generator
 * (a seeded xorshift here, so renders repeat exactly) and the Cortex-M DSP intrinsics, written out in C
 * with the same results as the ARM instructions. GPL-3.0-only, as the firmware it builds. */
#ifndef LXR_WASM_HAL_STUB
#define LXR_WASM_HAL_STUB
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
typedef struct { int unused; } RNG_HandleTypeDef;
typedef enum { HAL_OK = 0 } HAL_StatusTypeDef;
uint32_t lxr_rng_next(void);
#define HAL_RNG_GetRandomNumber(h) lxr_rng_next()
#define __IO volatile
#define __STATIC_INLINE static inline
#define __ASM __asm__
#ifndef UNUSED
#define UNUSED(x) ((void)(x))
/* the cycle counter and clock that Inc/delay.h reads (never used by the sound engine) */
typedef struct { volatile uint32_t CYCCNT, CTRL; } LxrDwt;
extern LxrDwt lxr_dwt;
#define DWT (&lxr_dwt)
static inline uint32_t HAL_RCC_GetHCLKFreq(void){ return 216000000u; }
#endif
static inline int32_t __SSAT(int32_t x, uint32_t n){ const int32_t mx = (int32_t)((1u << (n - 1)) - 1), mn = -mx - 1; return x > mx ? mx : x < mn ? mn : x; }
static inline uint32_t __USAT(int32_t x, uint32_t n){ const int32_t mx = (int32_t)((1u << n) - 1); return x > mx ? (uint32_t)mx : x < 0 ? 0u : (uint32_t)x; }
static inline int32_t lxr_sat16(int32_t x){ return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
static inline uint32_t __QADD16(uint32_t a, uint32_t b){ int32_t lo = lxr_sat16((int16_t)a + (int16_t)b), hi = lxr_sat16((int16_t)(a >> 16) + (int16_t)(b >> 16)); return ((uint32_t)(uint16_t)hi << 16) | (uint16_t)lo; }
static inline uint32_t __QSUB16(uint32_t a, uint32_t b){ int32_t lo = lxr_sat16((int16_t)a - (int16_t)b), hi = lxr_sat16((int16_t)(a >> 16) - (int16_t)(b >> 16)); return ((uint32_t)(uint16_t)hi << 16) | (uint16_t)lo; }
static inline uint32_t __SADD16(uint32_t a, uint32_t b){ uint16_t lo = (uint16_t)((int16_t)a + (int16_t)b), hi = (uint16_t)((int16_t)(a >> 16) + (int16_t)(b >> 16)); return ((uint32_t)hi << 16) | lo; }
static inline uint32_t __PKHBT(uint32_t a, uint32_t b, uint32_t sh){ return (a & 0xFFFFu) | ((b << sh) & 0xFFFF0000u); }
static inline int32_t __QADD(int32_t a, int32_t b){ int64_t s = (int64_t)a + b; return s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : (int32_t)s; }
static inline int32_t __QSUB(int32_t a, int32_t b){ int64_t s = (int64_t)a - b; return s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : (int32_t)s; }
static inline uint64_t __SMLALD(uint32_t a, uint32_t b, uint64_t acc){ return (uint64_t)((int64_t)acc + (int64_t)(int16_t)a * (int16_t)b + (int64_t)(int16_t)(a >> 16) * (int16_t)(b >> 16)); }
static inline uint32_t __CLZ(uint32_t v){ return v ? (uint32_t)__builtin_clz(v) : 32u; }
/* peripheral handles and pins named by main.h and the parameter code (declared, never touched by the sound engine) */
typedef struct { int unused; } I2S_HandleTypeDef, UART_HandleTypeDef, TIM_HandleTypeDef, SPI_HandleTypeDef, DMA_HandleTypeDef, SD_HandleTypeDef, PCD_HandleTypeDef;
typedef struct { volatile uint32_t IDR, ODR, BSRR; } GPIO_TypeDef;
extern GPIO_TypeDef lxr_gpio;
#define GPIOA (&lxr_gpio)
#define GPIOB (&lxr_gpio)
#define GPIOC (&lxr_gpio)
#define GPIOD (&lxr_gpio)
#define GPIOE (&lxr_gpio)
#define GPIO_PIN_0 0x1u
#define GPIO_PIN_1 0x2u
#define GPIO_PIN_2 0x4u
#define GPIO_PIN_3 0x8u
#define GPIO_PIN_4 0x10u
#define GPIO_PIN_5 0x20u
#define GPIO_PIN_6 0x40u
#define GPIO_PIN_7 0x80u
#define GPIO_PIN_8 0x100u
#define GPIO_PIN_9 0x200u
#define GPIO_PIN_10 0x400u
#define GPIO_PIN_11 0x800u
#define GPIO_PIN_12 0x1000u
#define GPIO_PIN_13 0x2000u
#define GPIO_PIN_14 0x4000u
#define GPIO_PIN_15 0x8000u
/* status flags, read only by calcNoise(), which the firmware no longer calls (its callers are commented out) */
typedef union { struct { uint32_t _r:28; uint32_t V:1; uint32_t C:1; uint32_t Z:1; uint32_t N:1; } b; uint32_t w; } APSR_Type;
static inline uint32_t __get_APSR(void){ return 0; }
#endif
