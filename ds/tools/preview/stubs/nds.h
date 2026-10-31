// Stand-in for libnds used by the PC preview build. Only what ChessDS uses.

#ifndef PREVIEW_NDS_H
#define PREVIEW_NDS_H

#include <stdbool.h>
#include <stdint.h>

#define KEY_A      (1 << 0)
#define KEY_B      (1 << 1)
#define KEY_SELECT (1 << 2)
#define KEY_START  (1 << 3)
#define KEY_RIGHT  (1 << 4)
#define KEY_LEFT   (1 << 5)
#define KEY_UP     (1 << 6)
#define KEY_DOWN   (1 << 7)
#define KEY_R      (1 << 8)
#define KEY_L      (1 << 9)
#define KEY_X      (1 << 10)
#define KEY_Y      (1 << 11)
#define KEY_TOUCH  (1 << 12)

#define MODE_0_2D          0
#define MODE_5_2D          5
#define VRAM_A_MAIN_BG     0
#define VRAM_C_SUB_BG      0
#define BgType_Bmp16       0
#define BgSize_B16_256x256 0
#define IRQ_VBLANK         1

typedef struct {
    uint16_t rawx, rawy, px, py, z1, z2;
} touchPosition;

static inline void videoSetMode(int mode) { (void)mode; }
static inline void videoSetModeSub(int mode) { (void)mode; }
static inline void vramSetBankA(int type) { (void)type; }
static inline void vramSetBankC(int type) { (void)type; }
static inline void bgSetPriority(int id, int priority) { (void)id; (void)priority; }
static inline void keysSetRepeat(int delay, int repeat) { (void)delay; (void)repeat; }
static inline void scanKeys(void) {}

// Implemented by sim.c
int bgInit(int layer, int type, int size, int map_base, int tile_base);
int bgInitSub(int layer, int type, int size, int map_base, int tile_base);
uint16_t *bgGetGfxPtr(int id);
uint32_t keysDown(void);
uint32_t keysDownRepeat(void);
uint32_t keysHeld(void);
void touchRead(touchPosition *touch);
void cothread_yield_irq(uint32_t flag);

#endif
