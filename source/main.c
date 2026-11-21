// PAWS OFF! -- a jewel-defence game for the GBA.
//
// You are the Duchess, out for a midnight stroll in the manor garden,
// dripping with the family jewels.  The local raccoons want them.  You can't
// fight a raccoon -- but raccoons can't resist anything shiny.  Throw coins
// to keep them busy until the sun comes up.
//
// Built on the libtonc examples:
//   template   - project layout / Makefile
//   obj_demo   - sprites,  obj_aff - affine sprites (kits + chonks are scaled)
//   sbb_reg / bigmap - scrolling tilemaps bigger than the screen
//   bld_demo   - alpha blending (shadows), key_demo - input
//   snd1_demo  - DMG sound (see sound.c), swi_vsync - VBlankIntrWait loop
//   bm_modes   - mode 4 bitmap (title screen)
//
// All graphics, the map and the music come from tools/mkgfx.py.

#include <string.h>
#include <tonc.h>
#include "gfx.h"
#include "sound.h"

// Emulators and flash carts look for this tag to pick the save type.
const char sram_tag[] __attribute__((used)) = "SRAM_V113";

extern const unsigned int sys8Glyphs[192]; // libtonc's 8x8 font, 1bpp

// nav.iwram.c
void bfs(u8 *dist, const u8 *blocked, const u16 *seeds, int nseeds);

// === CONSTANTS ========================================================

#define FX(n) ((int)((n) * 256))

#define WORLD_W (MAP_W * 16)
#define WORLD_H (MAP_H * 16)

// VRAM layout
#define CBB_MAP 0
#define CBB_TXT 2
#define SBB_HUD 23
#define SBB_GND 24    // 24-27: 64x64 ground
#define SBB_OVR 28    // 28-31: 64x64 tree tops / lamp heads
#define T_HUD 96      // HUD icon tiles (bar, coin, disco, track, sun)
#define T_BIG 128     // scratch tiles for double-size text
#define T_BARFONT 256 // font on HUD-bar background

#define PAL_WHITE 4
#define PAL_GOLD 5
#define PAL_PINK 6
#define PAL_CYAN 7
#define PAL_GREY 8

#define OBJPAL_FX 4

#define NJEWEL 5
#define MAX_RAC 12
#define MAX_SHINY 12
#define MAX_PICK 6
#define MAX_PART 28
#define MAX_POP 4
#define NNIGHTS 5

enum {
  GS_TITLE,
  GS_INTRO,
  GS_PLAY,
  GS_PAUSE,
  GS_DAWN,
  GS_SUMMARY,
  GS_PERK,
  GS_OVER,
  GS_WIN
};

enum { J_OWNED, J_CARRIED, J_GROUND, J_LOST };

enum { RT_REG, RT_KIT, RT_CHONK };
enum {
  RS_NONE,
  RS_CHASE,
  RS_WINDUP,
  RS_POUNCE,
  RS_TIRED,
  RS_LURED,
  RS_PLAY,
  RS_DANCE,
  RS_BORED,
  RS_STEAL,
  RS_FLEE
};

enum { SH_NONE, SH_FLY, SH_LAND, SH_HELD };
enum { ST_COIN, ST_DISCO };

enum { PK_NONE, PK_COIN, PK_STACK, PK_DISCO };
enum { PT_SPARK, PT_HEART, PT_NOTE, PT_DUST };

enum {
  PERK_POCKETS,
  PERK_SHOES,
  PERK_POLISH,
  PERK_MAGNET,
  PERK_ARM,
  PERK_DISCO,
  PERK_TWIN,
  PERK_HEIRLOOM,
  NPERK
};

// === TYPES ============================================================

typedef struct {
  u16 chase, steal, pspeed; // 8.8 px/frame
  u8 windup, pframes, tired, range;
  u16 play; // frames a coin keeps them busy
  u8 lure;  // radius at which a coin on the ground tempts them
} RType;

static const RType rtypes[3] = {
    {FX(0.94), FX(1.06), FX(2.75), 26, 16, 55, 42, 300, 52}, // regular
    {FX(1.20), FX(1.26), FX(3.10), 15, 14, 38, 36, 400,
     72}, // kit: quick, adores shinies
    {FX(0.72), FX(0.84), 0, 0, 0, 0, 0, 180,
     0}, // chonk: slow, only a direct hit works
};

typedef struct {
  u8 state, type, dir, flip; // dir: 0 down, 1 up, 2 side
  u8 anim, stuck, wander, wdir;
  u8 exit, leaving, immune, cool;
  s8 jewel, shiny;
  s16 timer;
  int x, y, vx, vy; // 24.8, feet centre
} Rac;

typedef struct {
  u8 state, type;
  s8 owner;
  s16 timer, age, dur;
  int x, y, vx, vy, z;
} Shiny;

typedef struct {
  u8 type;
  u8 anim;
  int x, y;
} Pickup;
typedef struct {
  u8 type, life;
  int x, y, vx, vy;
} Part;
typedef struct {
  u8 type, life;
  s16 x, y;
} Popup;

typedef struct {
  const char *name, *desc;
  u8 max;
} PerkDef;

static const PerkDef perks[NPERK] = {
    {"DEEP POCKETS", "Carry 3 more coins", 3},
    {"DANCING SHOES", "Walk faster", 3},
    {"EXTRA POLISH", "Coins distract for longer", 3},
    {"MAGNET BROOCH", "Pull in coins from afar", 2},
    {"CRICKET ARM", "Throw farther and faster", 2},
    {"DISCO FEVER", "+1 disco ball every night", 2},
    {"TWIN TOSS", "Throw two coins at once", 1},
    {"HEIRLOOM", "Win back a lost jewel", 9},
};

static const u16 jewel_tile[NJEWEL] = {OT_J_TIARA, OT_J_NECK, OT_J_RING,
                                       OT_J_BROOCH, OT_J_EARR};

static const char *night_blurb[NNIGHTS] = {
    "Distract them with shiny things!",
    "The little ones are quick...",
    "Something big is coming.",
    "More masks in the dark.",
    "The final heist!",
};

// per night: length (s), max raccoons, spawn interval, % kits, % chonks
static const u8 night_cfg[NNIGHTS][5] = {
    {45, 3, 240, 0, 0},   {50, 4, 200, 30, 0},   {55, 6, 170, 25, 15},
    {60, 8, 145, 30, 20}, {70, 10, 118, 30, 25},
};

// === GLOBALS ==========================================================

static OBJ_ATTR obj_buffer[128];
static int oam_n;

static int gs;
static u32 frame;
static int gtimer; // general purpose state timer

// palettes
static u16 pal_mix[224]; // bg (144) + obj (80) after the night->dawn blend
static u16 pal_out[144];
static int dawn_a, fade_game, fade_all, pal_dirty;
static int title_mode;

// camera
static int cam_x, cam_y, shake;

// player
static struct {
  int x, y;
  int dir, flip, anim, moving;
  int fx, fy; // facing vector, each -1..1
  int inv, cool;
  int coins, discos;
  int noflash; // flashes the coin counter when empty
} pl;

static u8 jst[NJEWEL];
static s16 jx[NJEWEL], jy[NJEWEL];
static u8 jgrace[NJEWEL];

static Rac racs[MAX_RAC];
static Shiny shs[MAX_SHINY];
static Pickup picks[MAX_PICK];
static Part parts[MAX_PART];
static Popup pops[MAX_POP];

static int night, night_t, night_len, spawn_t, pick_t;
static int score, hiscore, hinight;
static u8 perk_lv[NPERK];
static int perk_opt[3], perk_sel;

// navigation
static u8 cellblk[MAP_W * MAP_H] ALIGN4;
static u8 dist_p[MAP_W * MAP_H] ALIGN4; // steps to the player
static u8 dist_e[MAP_W * MAP_H] ALIGN4; // steps to the nearest way out

// text
static int big_n;

// profiling: scanlines of CPU time used by the last / the worst frame (228 =
// one full frame)
volatile u16 dbg_load, dbg_load_max;

// === SMALL HELPERS ====================================================

static inline int rnd(int n) { return (qran() * n) >> 15; }
static inline int iabs(int v) { return v < 0 ? -v : v; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int dist2(int x0, int y0, int x1, int y1) {
  int dx = x1 - x0, dy = y1 - y0;
  return dx * dx + dy * dy;
}

//! Scale (dx,dy) to length \a speed (8.8).
static void aim(int dx, int dy, int speed, int *vx, int *vy) {
  int len = Sqrt(dx * dx + dy * dy);
  if (len == 0) {
    *vx = *vy = 0;
    return;
  }
  *vx = dx * speed / len;
  *vy = dy * speed / len;
}

static char numbuf[12];
static const char *num(int v, int width, char pad) {
  int i = width;
  numbuf[i] = 0;
  do {
    numbuf[--i] = '0' + v % 10;
    v /= 10;
  } while (v && i > 0);
  while (i > 0)
    numbuf[--i] = pad;
  return numbuf;
}

// === SAVE =============================================================

static void save_load(void) {
  const u8 *s = sram_mem;
  hiscore = hinight = 0;
  if (s[0] == 'P' && s[1] == 'A' && s[2] == 'W' && s[3] == '1') {
    hiscore = s[4] | (s[5] << 8) | (s[6] << 16) | (s[7] << 24);
    hinight = s[8];
    if (hiscore < 0 || hiscore > 999999 || hinight > NNIGHTS + 1)
      hiscore = hinight = 0;
  }
}

static void save_store(void) {
  u8 *s = sram_mem;
  s[0] = 'P';
  s[1] = 'A';
  s[2] = 'W';
  s[3] = '1';
  s[4] = hiscore;
  s[5] = hiscore >> 8;
  s[6] = hiscore >> 16;
  s[7] = hiscore >> 24;
  s[8] = hinight;
}

// === PALETTE PIPELINE =================================================
// Every colour exists twice: as seen at night and as seen at dawn.  Each
// frame's palette is a blend of the two (dawn_a) and then a fade to black
// (fade_game for the world, fade_all for everything).

static void pal_flush(void) {
  static int phase;
  int i;

  if (title_mode)
    return;
  if (pal_dirty) {
    int fg = imax(fade_game, fade_all);
    clr_blend_fast((COLOR *)gfx_bgpal_night, (COLOR *)gfx_bgpal_dawn, pal_mix,
                   144, dawn_a);
    clr_blend_fast((COLOR *)gfx_objpal_night, (COLOR *)gfx_objpal_dawn,
                   pal_mix + 144, 80, dawn_a);
    clr_fade_fast(pal_mix, CLR_BLACK, pal_out, 64, fg);
    clr_fade_fast(pal_mix + 64, CLR_BLACK, pal_out + 64, 80, fade_all);
    memcpy16(pal_bg_mem, pal_out, 144);
    clr_fade_fast(pal_mix + 144, CLR_BLACK, pal_obj_mem, 80, fg);
  }
  if (pal_dirty || (frame & 7) == 0) {
    // water shimmer: rotate three colours of the water palette
    if ((frame & 7) == 0)
      phase = (phase + 1) % 3;
    for (i = 0; i < 3; i++)
      pal_bg_mem[2 * 16 + 10 + i] = pal_out[2 * 16 + 10 + (i + 3 - phase) % 3];
  }
  pal_dirty = 0;
}

static void vsync(void) {
  VBlankIntrWait();
  pal_flush();
  if (!title_mode) {
    oam_copy(oam_mem, obj_buffer, 128);
    REG_BG1HOFS = REG_BG2HOFS = cam_x;
    REG_BG1VOFS = REG_BG2VOFS = cam_y;
  }
  key_poll();
  snd_update();
  frame++;
}

static void fade_all_to(int target) {
  while (fade_all != target) {
    fade_all += (target > fade_all) ? 2 : -2;
    pal_dirty = 1;
    vsync();
  }
}

// === TEXT LAYER (BG0) =================================================

static void font_init(void) {
  const u8 *gl = (const u8 *)sys8Glyphs;
  int g, x, y;
  for (g = 0; g < 96; g++) {
    for (y = 0; y < 8; y++) {
      u32 row = 0, rowb = 0;
      for (x = 0; x < 8; x++) {
        int ink = (gl[g * 8 + y] >> x) & 1;
        int sh = !ink && x > 0 && y > 0 && ((gl[g * 8 + y - 1] >> (x - 1)) & 1);
        int c = ink ? 1 : (sh ? 2 : 0);
        row |= c << (x * 4);
        rowb |= (c ? c : 3) << (x * 4);
      }
      tile_mem[CBB_TXT][g].data[y] = row;
      tile_mem[CBB_TXT][T_BARFONT + g].data[y] = rowb;
    }
  }
  memcpy32(&tile_mem[CBB_TXT][T_HUD], gfx_hud_tiles, sizeof(gfx_hud_tiles) / 4);
}

static void txt_clear(void) {
  memset16(&se_mem[SBB_HUD][32], 0, 32 * 30);
  big_n = 0;
}

static void txt_puts(int col, int row, const char *s, int pal) {
  SCR_ENTRY *dst = &se_mem[SBB_HUD][row * 32 + col];
  while (*s)
    *dst++ = (*s++ - 32) | SE_PALBANK(pal);
}

static void txt_center(int row, const char *s, int pal) {
  txt_puts((30 - (int)strlen(s)) / 2, row, s, pal);
}

//! Double-size text: glyphs are scaled into scratch tiles on the fly.
static void big_puts(int col, int row, const char *s, int pal) {
  const u8 *gl = (const u8 *)sys8Glyphs;
  static u8 px[16][16];
  int x, y, q;

  for (; *s; s++, col += 2) {
    if (*s == ' ' || big_n >= 16)
      continue;
    const u8 *g = &gl[(*s - 32) * 8];
    memset(px, 0, sizeof(px));
    for (y = 0; y < 16; y++)
      for (x = 0; x < 16; x++)
        if ((g[y >> 1] >> (x >> 1)) & 1)
          px[y][x] = 1;
    for (y = 15; y >= 2; y--)
      for (x = 15; x >= 2; x--)
        if (!px[y][x] && px[y - 2][x - 2] == 1)
          px[y][x] = 2;
    int base = T_BIG + big_n * 4;
    for (q = 0; q < 4; q++) {
      TILE *t = &tile_mem[CBB_TXT][base + q];
      for (y = 0; y < 8; y++) {
        u32 r = 0;
        for (x = 0; x < 8; x++)
          r |= px[(q >> 1) * 8 + y][(q & 1) * 8 + x] << (x * 4);
        t->data[y] = r;
      }
      se_mem[SBB_HUD][(row + (q >> 1)) * 32 + col + (q & 1)] =
          (base + q) | SE_PALBANK(pal);
    }
    big_n++;
  }
}

static void big_center(int row, const char *s, int pal) {
  big_puts((30 - 2 * (int)strlen(s)) / 2, row, s, pal);
}

// === MAP + NAVIGATION =================================================

//! Is the pixel blocked?  mask 1 = walls, 2 = hedge gaps (raccoons only).
static inline int solid_at(int x, int y, int mask) {
  if (x < 0 || y < 0 || x >= WORLD_W || y >= WORLD_H)
    return mask & 2; // off the map: only raccoons go there
  return gfx_solid[(y >> 3) * MAP_TW + (x >> 3)] & mask;
}

static int box_free(int x, int y, int hw, int hh, int mask) {
  return !solid_at(x - hw, y - hh, mask) &&
         !solid_at(x + hw - 1, y - hh, mask) && !solid_at(x - hw, y, mask) &&
         !solid_at(x + hw - 1, y, mask);
}

//! Move with sliding.  Returns bit0: x blocked, bit1: y blocked.
static int move_box(int *x, int *y, int vx, int vy, int hw, int hh, int mask) {
  int blocked = 0;
  if (vx) {
    if (box_free((*x + vx) >> 8, *y >> 8, hw, hh, mask))
      *x += vx;
    else
      blocked |= 1;
  }
  if (vy) {
    if (box_free(*x >> 8, (*y + vy) >> 8, hw, hh, mask))
      *y += vy;
    else
      blocked |= 2;
  }
  return blocked;
}

static void nav_init(void) {
  int cx, cy, i;
  u16 seeds[NEXITS];
  for (cy = 0; cy < MAP_H; cy++)
    for (cx = 0; cx < MAP_W; cx++) {
      const u8 *s = &gfx_solid[cy * 2 * MAP_TW + cx * 2];
      cellblk[cy * MAP_W + cx] = (s[0] | s[1] | s[MAP_TW] | s[MAP_TW + 1]) & 1;
    }
  for (i = 0; i < NEXITS; i++)
    seeds[i] = gfx_exits[i * 4 + 1] * MAP_W + gfx_exits[i * 4];
  bfs(dist_e, cellblk, seeds, NEXITS);
}

//! Next waypoint downhill on a distance field.  Returns 0 if there is none
//! (within \a near steps, or standing somewhere the field doesn't cover).
static int flow_target(const u8 *dist, int x, int y, int *tx, int *ty,
                       int near) {
  static const s8 dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1},  {0, -1},
                                {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
  int cx = x >> 4, cy = y >> 4, i;
  int best = dist[cy * MAP_W + cx], bx = 0, by = 0;

  if (best == 255 || best <= near)
    return 0;
  for (i = 0; i < 8; i++) {
    int dx = dirs[i][0], dy = dirs[i][1];
    int nx = cx + dx, ny = cy + dy;
    if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H)
      continue;
    int d = dist[ny * MAP_W + nx];
    if (d >= best)
      continue;
    if (dx && dy &&
        (dist[cy * MAP_W + nx] == 255 || dist[ny * MAP_W + cx] == 255))
      continue; // don't cut corners
    best = d;
    bx = dx;
    by = dy;
  }
  if (!bx && !by)
    return 0;
  *tx = (cx + bx) * 16 + 8;
  *ty = (cy + by) * 16 + 10;
  return 1;
}

// === EFFECTS ==========================================================

static void part_add(int type, int x, int y, int vx, int vy, int life) {
  int i;
  for (i = 0; i < MAX_PART; i++)
    if (!parts[i].life) {
      Part *p = &parts[i];
      p->type = type;
      p->life = life;
      p->x = x;
      p->y = y;
      p->vx = vx;
      p->vy = vy;
      return;
    }
}

static void sparkle_burst(int x, int y, int n) {
  while (n--)
    part_add(PT_SPARK, x + FX(rnd(13) - 6), y + FX(rnd(13) - 10),
             rnd(257) - 128, rnd(257) - 200, 18 + rnd(14));
}

static void popup_add(int type, int x, int y) {
  static const u16 pts[4] = {20, 50, 100, 150};
  int i;
  score += pts[type];
  for (i = 0; i < MAX_POP; i++)
    if (!pops[i].life) {
      pops[i].type = type;
      pops[i].life = 50;
      pops[i].x = x >> 8;
      pops[i].y = (y >> 8) - 22;
      return;
    }
}

// === JEWELS ===========================================================

static int jewels_in(int state) {
  int i, n = 0;
  for (i = 0; i < NJEWEL; i++)
    n += (jst[i] == state);
  return n;
}

static void jewel_drop(Rac *r) {
  int j = r->jewel;
  if (j < 0)
    return;
  r->jewel = -1;
  jst[j] = J_GROUND;
  jx[j] = clamp(r->x >> 8, 22, WORLD_W - 22);
  jy[j] = clamp(r->y >> 8, 56, WORLD_H - 22);
  jgrace[j] = 150;
}

// === SHINIES ==========================================================

static int shiny_alloc(void) {
  int i;
  for (i = 0; i < MAX_SHINY; i++)
    if (shs[i].state == SH_NONE)
      return i;
  return -1;
}

static void coin_throw(int dx, int dy, int dist) {
  int i = shiny_alloc();
  if (i < 0)
    return;
  Shiny *s = &shs[i];
  int speed = FX(3.2) + perk_lv[PERK_ARM] * FX(0.7);
  s->state = SH_FLY;
  s->type = ST_COIN;
  s->owner = -1;
  s->x = pl.x;
  s->y = pl.y - FX(6);
  s->z = 0;
  aim(dx, dy, speed, &s->vx, &s->vy);
  s->age = 0;
  s->dur = imax(6, (dist << 8) / speed);
}

static int play_time(const Rac *r) {
  return rtypes[r->type].play * (10 + 4 * perk_lv[PERK_POLISH]) / 10;
}

//! A raccoon gets its paws on a coin.
static void rac_take_coin(Rac *r, int ri, int si, int direct) {
  Shiny *s = &shs[si];
  int saved = r->jewel >= 0;

  if (r->state == RS_LURED && r->shiny >= 0 && r->shiny != si &&
      shs[r->shiny].owner == ri)
    shs[r->shiny].owner = -1;
  jewel_drop(r);
  s->state = SH_HELD;
  s->owner = ri;
  r->shiny = si;
  r->state = RS_PLAY;
  r->timer = play_time(r);
  r->leaving = 0;
  r->dir = 0;
  sparkle_burst(r->x, r->y - FX(10), direct ? 6 : 3);
  popup_add(saved ? 3 : (direct ? 1 : 0), r->x, r->y);
  sfx_play(saved ? SFX_RECOVER : (direct ? SFX_HIT : SFX_LURE));
}

static void shinies_update(void) {
  int i, k;
  for (i = 0; i < MAX_SHINY; i++) {
    Shiny *s = &shs[i];
    switch (s->state) {
    case SH_FLY: {
      int nx = (s->x + s->vx) >> 8, ny = (s->y + s->vy) >> 8;
      if (solid_at(nx, ny, 1) || nx < 18 || nx > WORLD_W - 18 ||
          ny > WORLD_H - 18)
        s->vx = s->vy = 0; // clink off a wall or hedge
      s->x += s->vx;
      s->y += s->vy;
      s->age++;
      s->z = 56 * s->age * (s->dur - s->age) / (s->dur * s->dur);
      // bonk!
      for (k = 0; k < MAX_RAC; k++) {
        Rac *r = &racs[k];
        if (r->state == RS_NONE || r->state == RS_PLAY || r->state == RS_DANCE)
          continue;
        if (dist2(s->x >> 8, s->y >> 8, r->x >> 8, (r->y >> 8) - 5) < 11 * 11) {
          rac_take_coin(r, k, i, 1);
          break;
        }
      }
      if (s->state == SH_FLY && s->age >= s->dur) {
        s->state = SH_LAND;
        s->timer = 420;
        s->z = 0;
        part_add(PT_SPARK, s->x, s->y - FX(4), 0, -64, 14);
      }
      break;
    }
    case SH_LAND:
      if (--s->timer <= 0) {
        s->state = SH_NONE;
        if (s->type == ST_DISCO)
          sparkle_burst(s->x, s->y, 6);
        break;
      }
      if (s->type == ST_DISCO) {
        if ((frame & 3) == 0)
          part_add(PT_SPARK, s->x + FX(rnd(49) - 24), s->y + FX(rnd(41) - 30),
                   0, -40, 16);
      } else if (s->owner < 0 && s->age > 20 &&
                 pl.coins < 6 + 3 * perk_lv[PERK_POCKETS] &&
                 dist2(s->x >> 8, s->y >> 8, pl.x >> 8, pl.y >> 8) < 11 * 11) {
        // picked your own coin back up
        s->state = SH_NONE;
        pl.coins++;
        sfx_play(SFX_PICKUP);
      }
      s->age++;
      break;
    case SH_HELD:
      if (s->owner < 0 || racs[s->owner].state != RS_PLAY)
        s->state = SH_NONE;
      else {
        s->x = racs[s->owner].x;
        s->y = racs[s->owner].y;
      }
      break;
    }
  }
}

// === RACCOONS =========================================================

static void rac_spawn(int type) {
  int i, tries, e = 0;
  for (i = 0; i < MAX_RAC; i++)
    if (racs[i].state == RS_NONE)
      break;
  if (i == MAX_RAC)
    return;
  // prefer a gap the player can't currently see
  for (tries = 0; tries < 8; tries++) {
    e = rnd(NEXITS);
    int ex = gfx_exits[e * 4] * 16 + 8 - cam_x,
        ey = gfx_exits[e * 4 + 1] * 16 + 8 - cam_y;
    if (ex < -8 || ex > 248 || ey < -8 || ey > 168)
      break;
  }
  Rac *r = &racs[i];
  memset(r, 0, sizeof(Rac));
  r->state = RS_CHASE;
  r->type = type;
  r->exit = e;
  r->jewel = r->shiny = -1;
  r->x = (gfx_exits[e * 4] * 16 + 8 + gfx_exits[e * 4 + 2] * 30) << 8;
  r->y = (gfx_exits[e * 4 + 1] * 16 + 10 + gfx_exits[e * 4 + 3] * 30) << 8;
}

static inline int rac_speed(int base) { return base * (100 + 4 * night) / 100; }

static inline int off_map(int x, int y) {
  return x < 0 || x >= WORLD_W || y < 0 || y >= WORLD_H;
}

//! Walk towards (fx,fy), following \a dist downhill when given.
static void rac_nav(Rac *r, const u8 *dist, int fx, int fy, int speed) {
  static const s8 wd[8][2] = {{1, 0},  {1, 1},   {0, 1},  {-1, 1},
                              {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
  int x = r->x >> 8, y = r->y >> 8;
  int vx, vy, blocked;

  if (r->wander) {
    r->wander--;
    vx = wd[r->wdir][0] * speed;
    vy = wd[r->wdir][1] * speed;
  } else {
    if (off_map(x, y)) {
      if (!r->leaving) // on the way in: aim for the gap we came through
      {
        fx = gfx_exits[r->exit * 4] * 16 + 8 - gfx_exits[r->exit * 4 + 2] * 10;
        fy = gfx_exits[r->exit * 4 + 1] * 16 + 10 -
             gfx_exits[r->exit * 4 + 3] * 10;
      }
    } else if (dist)
      flow_target(dist, x, y, &fx, &fy, dist == dist_p ? 1 : 0);
    aim(fx - x, fy - y, speed, &vx, &vy);
  }
  r->vx = vx;
  r->vy = vy;
  blocked = move_box(&r->x, &r->y, vx, vy, 3, 3, 1);
  if (blocked) {
    if (++r->stuck > 22 && !r->wander) {
      r->stuck = 0;
      r->wander = 16 + rnd(16);
      r->wdir = rnd(8);
    }
  } else if (r->stuck)
    r->stuck--;

  if (iabs(vx) >= iabs(vy)) {
    r->dir = 2;
    r->flip = vx < 0;
  } else
    r->dir = vy < 0;
  r->anim++;
}

//! Is there something shiny nearby?  Returns 1 if the raccoon went for it.
static int rac_lure(Rac *r, int ri) {
  const RType *t = &rtypes[r->type];
  int i, x = r->x >> 8, y = r->y >> 8;
  int best = -1, bestd = 0x7FFFFFFF;

  if (off_map(x, y))
    return 0;
  for (i = 0; i < MAX_SHINY; i++) {
    Shiny *s = &shs[i];
    if (s->state != SH_LAND)
      continue;
    int d = dist2(x, y, s->x >> 8, s->y >> 8);
    if (s->type == ST_DISCO) {
      if (d < 92 * 92) {
        best = i;
        break;
      } // nobody resists a disco ball
    } else if (s->owner < 0 && !r->immune && d < t->lure * t->lure &&
               d < bestd) {
      best = i;
      bestd = d;
    }
  }
  if (best < 0)
    return 0;
  if (shs[best].type == ST_COIN)
    shs[best].owner = ri;
  if (r->jewel >= 0) {
    jewel_drop(r);
    popup_add(3, r->x, r->y);
    sfx_play(SFX_RECOVER);
  }
  r->shiny = best;
  r->state = RS_LURED;
  r->timer = 0;
  r->leaving = 0;
  return 1;
}

static void rac_steal(Rac *r) {
  int j, k;
  for (j = 0; j < NJEWEL; j++)
    if (jst[j] == J_OWNED)
      break;
  if (j == NJEWEL)
    return;
  jst[j] = J_CARRIED;
  r->jewel = j;
  r->state = RS_STEAL;
  r->leaving = 0;
  pl.inv = 110;
  shake = 12;
  sfx_play(SFX_STEAL);
  sfx_noise(SSQR_ENV_BUILD(12, 0, 2), 0x53, 14);
  sparkle_burst(pl.x, pl.y - FX(16), 5);
  // the rest of the gang stops to gawk for a moment
  for (k = 0; k < MAX_RAC; k++) {
    Rac *o = &racs[k];
    if (o != r &&
        (o->state == RS_CHASE || o->state == RS_WINDUP ||
         o->state == RS_POUNCE) &&
        dist2(o->x >> 8, o->y >> 8, pl.x >> 8, pl.y >> 8) < 56 * 56) {
      o->state = RS_TIRED;
      o->timer = 50 + rnd(30);
    }
  }
}

static void rac_update(Rac *r, int ri) {
  const RType *t = &rtypes[r->type];
  int x = r->x >> 8, y = r->y >> 8;
  int px = pl.x >> 8, py = pl.y >> 8;
  int j, d;

  if (r->cool)
    r->cool--;
  if (r->immune)
    r->immune--;

  switch (r->state) {
  case RS_CHASE: {
    int tk = -1, tx = px, ty = py, best = 0x7FFFFFFF;

    if (((frame + ri) & 3) == 0 && rac_lure(r, ri))
      break;
    if (jewels_in(J_OWNED) && gs == GS_PLAY) {
      best = dist2(x, y, px, py);
      tk = NJEWEL;
    }
    for (j = 0; j < NJEWEL; j++)
      if (jst[j] == J_GROUND && !jgrace[j]) {
        d = dist2(x, y, jx[j], jy[j]);
        if (d < best) {
          best = d;
          tk = j;
          tx = jx[j];
          ty = jy[j];
        }
      }
    if (tk < 0) {
      // nothing to steal right now: loiter near the Duchess
      if (dist2(x, y, px, py) > 48 * 48)
        rac_nav(r, dist_p, px, py, rac_speed(t->chase) * 3 / 4);
      break;
    }
    if (tk == NJEWEL) {
      if (best < 9 * 9 && !pl.inv) {
        rac_steal(r);
        break;
      }
      if (t->range && !r->cool && best < t->range * t->range &&
          !off_map(x, y)) {
        r->state = RS_WINDUP;
        r->timer = t->windup;
        break;
      }
      rac_nav(r, dist_p, px, py, rac_speed(t->chase));
    } else {
      if (best < 8 * 8) {
        jst[tk] = J_CARRIED;
        r->jewel = tk;
        r->state = RS_STEAL;
        r->leaving = 0;
        sfx_play(SFX_STEAL);
        break;
      }
      rac_nav(r, NULL, tx, ty, rac_speed(t->chase));
    }
    break;
  }
  case RS_WINDUP:
    if (iabs(px - x) >= iabs(py - y)) {
      r->dir = 2;
      r->flip = px < x;
    } else
      r->dir = py < y;
    if (--r->timer <= 0) {
      aim(px - x, py - y, rac_speed(t->pspeed), &r->vx, &r->vy);
      r->state = RS_POUNCE;
      r->timer = t->pframes;
      sfx_play(SFX_POUNCE);
      part_add(PT_DUST, r->x, r->y, -r->vx / 4, -r->vy / 4, 16);
    }
    break;
  case RS_POUNCE:
    d = move_box(&r->x, &r->y, r->vx, r->vy, 3, 3, 1);
    r->anim += 2;
    if ((r->timer & 3) == 0)
      part_add(PT_DUST, r->x, r->y, 0, 0, 12);
    if (dist2(r->x >> 8, r->y >> 8, px, py) < 10 * 10 && !pl.inv &&
        jewels_in(J_OWNED) && gs == GS_PLAY) {
      rac_steal(r);
      break;
    }
    if (--r->timer <= 0 || d == 3) {
      r->state = RS_TIRED;
      r->timer = t->tired;
    }
    break;
  case RS_TIRED:
    if (--r->timer <= 0) {
      r->state = RS_CHASE;
      r->cool = 45;
    }
    break;
  case RS_LURED: {
    Shiny *s = &shs[r->shiny];
    int tx, ty;
    if (s->state != SH_LAND || (s->type == ST_COIN && s->owner != ri) ||
        ++r->timer > 300) {
      if (s->state == SH_LAND && s->owner == ri)
        s->owner = -1;
      r->state = RS_CHASE;
      r->immune = 90;
      break;
    }
    tx = s->x >> 8;
    ty = s->y >> 8;
    if (s->type == ST_DISCO) {
      // form a ring around the ball
      static const s8 ring[8][2] = {{18, 4},  {-18, 4},  {10, 14}, {-10, 14},
                                    {12, -8}, {-12, -8}, {0, 18},  {0, -12}};
      tx += ring[ri & 7][0];
      ty += ring[ri & 7][1];
    }
    if (dist2(x, y, tx, ty) < 6 * 6) {
      if (s->type == ST_DISCO) {
        r->state = RS_DANCE;
        popup_add(0, r->x, r->y);
      } else
        rac_take_coin(r, ri, r->shiny, 0);
      break;
    }
    rac_nav(r, NULL, tx, ty, rac_speed(t->chase) * 3 / 2);
    break;
  }
  case RS_PLAY:
    r->anim++;
    if ((r->anim & 31) == 0)
      part_add(PT_HEART, r->x + FX(rnd(9) - 4), r->y - FX(14), 0, -70, 30);
    if (--r->timer <= 0) {
      if (r->shiny >= 0)
        shs[r->shiny].state = SH_NONE; // pocketed
      r->shiny = -1;
      r->state = RS_BORED;
      r->timer = 40;
      r->immune = 100;
    }
    break;
  case RS_DANCE:
    r->anim++;
    if ((r->anim & 31) == 8)
      part_add(PT_NOTE, r->x + FX(rnd(9) - 4), r->y - FX(14), rnd(65) - 32, -70,
               30);
    if (shs[r->shiny].state != SH_LAND || shs[r->shiny].type != ST_DISCO) {
      r->state = RS_BORED;
      r->timer = 70;
      r->immune = 60;
    }
    break;
  case RS_BORED:
    if (--r->timer <= 0)
      r->state = RS_CHASE;
    break;
  case RS_STEAL:
  case RS_FLEE: {
    int fx = x, fy = y, e;
    if (r->state == RS_STEAL && ((frame + ri) & 3) == 0 && rac_lure(r, ri))
      break;
    if (!r->leaving && !off_map(x, y) &&
        dist_e[(y >> 4) * MAP_W + (x >> 4)] == 0) {
      for (e = 0; e < NEXITS; e++)
        if (gfx_exits[e * 4] == (x >> 4) && gfx_exits[e * 4 + 1] == (y >> 4)) {
          r->exit = e;
          r->leaving = 1;
        }
    }
    if (r->leaving) {
      fx = gfx_exits[r->exit * 4] * 16 + 8 + gfx_exits[r->exit * 4 + 2] * 64;
      fy = gfx_exits[r->exit * 4 + 1] * 16 + 10 +
           gfx_exits[r->exit * 4 + 3] * 64;
      rac_nav(r, NULL, fx, fy, rac_speed(t->steal));
    } else if (!off_map(x, y) && dist_e[(y >> 4) * MAP_W + (x >> 4)] == 255) {
      // standing somewhere odd: make for the gap we came in by
      fx = gfx_exits[r->exit * 4] * 16 + 8;
      fy = gfx_exits[r->exit * 4 + 1] * 16 + 10;
      rac_nav(r, NULL, fx, fy, rac_speed(t->steal));
    } else
      rac_nav(r, dist_e, fx, fy, rac_speed(t->steal));

    x = r->x >> 8;
    y = r->y >> 8;
    if (x < -20 || x > WORLD_W + 20 || y > WORLD_H + 20) {
      if (r->jewel >= 0) {
        jst[r->jewel] = J_LOST;
        sfx_play(SFX_LOST);
        shake = 16;
      }
      r->state = RS_NONE;
    }
    break;
  }
  }
}

static void racs_update(void) {
  int i, k;
  for (i = 0; i < MAX_RAC; i++)
    if (racs[i].state != RS_NONE)
      rac_update(&racs[i], i);

  // keep them from stacking up on one spot
  for (i = 0; i < MAX_RAC; i++) {
    Rac *a = &racs[i];
    if (a->state != RS_CHASE && a->state != RS_STEAL && a->state != RS_LURED)
      continue;
    for (k = 0; k < MAX_RAC; k++) {
      Rac *b = &racs[k];
      if (k == i || b->state == RS_NONE)
        continue;
      int dx = (a->x - b->x) >> 8, dy = (a->y - b->y) >> 8;
      if (iabs(dx) < 9 && iabs(dy) < 7) {
        int px = dx > 0 || (dx == 0 && i > k) ? 96 : -96;
        int py = dy > 0 ? 64 : -64;
        move_box(&a->x, &a->y, px, py, 3, 3, 1);
      }
    }
  }
}

// === PICKUPS ==========================================================

static void pickup_spawn(int type) {
  int i, tries;
  for (i = 0; i < MAX_PICK; i++)
    if (picks[i].type == PK_NONE)
      break;
  if (i == MAX_PICK)
    return;
  for (tries = 0; tries < 12; tries++) {
    int cx = 1 + rnd(MAP_W - 2), cy = 3 + rnd(MAP_H - 4);
    int x = cx * 16 + 8, y = cy * 16 + 10, k, ok = 1;
    if (cellblk[cy * MAP_W + cx] || dist_e[cy * MAP_W + cx] == 255)
      continue;
    if (dist2(x, y, pl.x >> 8, pl.y >> 8) < 44 * 44)
      continue;
    for (k = 0; k < MAX_PICK; k++)
      if (picks[k].type &&
          dist2(x, y, picks[k].x >> 8, picks[k].y >> 8) < 40 * 40)
        ok = 0;
    if (!ok)
      continue;
    picks[i].type = type;
    picks[i].anim = rnd(64);
    picks[i].x = x << 8;
    picks[i].y = y << 8;
    return;
  }
}

static void pickups_update(void) {
  int i, n = 0, has_disco = 0;
  int maxc = 6 + 3 * perk_lv[PERK_POCKETS];
  int reach = 12 + 26 * perk_lv[PERK_MAGNET];

  for (i = 0; i < MAX_PICK; i++) {
    Pickup *p = &picks[i];
    if (p->type == PK_NONE)
      continue;
    n++;
    p->anim++;
    if (p->type == PK_DISCO)
      has_disco = 1;
    if ((p->anim & 63) == 0)
      part_add(PT_SPARK, p->x + FX(rnd(9) - 4), p->y - FX(6 + rnd(6)), 0, -24,
               20);

    int full = (p->type == PK_DISCO) ? pl.discos >= 3 : pl.coins >= maxc;
    int d = dist2(p->x >> 8, p->y >> 8, pl.x >> 8, (pl.y >> 8) - 4);
    if (full)
      continue;
    if (d < 12 * 12) {
      if (p->type == PK_DISCO) {
        pl.discos++;
        sfx_play(SFX_DISCO);
      } else {
        pl.coins = imin(maxc, pl.coins + (p->type == PK_STACK ? 3 : 1));
        sfx_play(SFX_PICKUP);
      }
      sparkle_burst(p->x, p->y, 4);
      p->type = PK_NONE;
    } else if (d < reach * reach) {
      int vx, vy; // magnet brooch
      aim((pl.x - p->x) >> 8, (pl.y - p->y) >> 8, FX(2.2), &vx, &vy);
      p->x += vx;
      p->y += vy;
    }
  }

  if (gs == GS_PLAY && ++pick_t >= 95) {
    pick_t = 0;
    if (n < 5) {
      int k = rnd(100);
      if (k < 9 && !has_disco && pl.discos < 3)
        pickup_spawn(PK_DISCO);
      else
        pickup_spawn(k < 40 ? PK_STACK : PK_COIN);
    }
  }
}

// === PLAYER ===========================================================

static void player_throw(void) {
  static const s16 norm[2] = {256, 181};
  int dx = pl.fx * norm[pl.fx && pl.fy], dy = pl.fy * norm[pl.fx && pl.fy];
  int range = 92 + 30 * perk_lv[PERK_ARM];
  int speed = FX(3.2) + perk_lv[PERK_ARM] * FX(0.7);
  int dist = range;
  int i, best = -1, bestd = 0x7FFFFFFF;
  int px = pl.x >> 8, py = (pl.y >> 8) - 6;

  // gentle aim assist: the nearest raccoon inside a cone around the facing
  for (i = 0; i < MAX_RAC; i++) {
    Rac *r = &racs[i];
    if (r->state == RS_NONE || r->state == RS_PLAY || r->state == RS_DANCE)
      continue;
    int rx = (r->x >> 8) - px, ry = (r->y >> 8) - 5 - py;
    int d = rx * rx + ry * ry;
    if (d > (range + 8) * (range + 8) || d >= bestd)
      continue;
    int len = Sqrt(d);
    if (rx * dx + ry * dy < len * 210) // ~35 degrees
      continue;
    best = i;
    bestd = d;
  }
  if (best >= 0) {
    Rac *r = &racs[best];
    int len = Sqrt(bestd);
    int t = (len << 8) / speed; // frames of flight: lead the target
    int moving = (r->state == RS_CHASE || r->state == RS_STEAL ||
                  r->state == RS_LURED || r->state == RS_FLEE);
    dx = (r->x >> 8) - px + (moving ? (r->vx * t >> 8) : 0);
    dy = (r->y >> 8) - 5 - py + (moving ? (r->vy * t >> 8) : 0);
    dist = imin(range, Sqrt(dx * dx + dy * dy) + 4);
  }
  if (perk_lv[PERK_TWIN]) {
    coin_throw((dx * 251 - dy * 50) >> 8, (dy * 251 + dx * 50) >> 8, dist);
    coin_throw((dx * 251 + dy * 50) >> 8, (dy * 251 - dx * 50) >> 8, dist);
  } else
    coin_throw(dx, dy, dist);
  pl.coins--;
  pl.cool = 14;
  sfx_play(SFX_THROW);
}

static void player_update(void) {
  int ix = key_tri_horz(), iy = key_tri_vert();
  int speed = FX(1.40) + perk_lv[PERK_SHOES] * FX(0.17);
  int i;

  pl.moving = (ix || iy);
  if (pl.moving) {
    if (ix && iy)
      speed = speed * 181 >> 8;
    move_box(&pl.x, &pl.y, ix * speed, iy * speed, 4, 4, 3);
    if (!key_is_down(KEY_L | KEY_R)) // hold a shoulder button to strafe
    {
      pl.fx = ix;
      pl.fy = iy;
      if (ix) {
        pl.dir = 2;
        pl.flip = ix < 0;
      } else
        pl.dir = iy < 0;
    }
    pl.anim++;
  } else
    pl.anim = 0;

  if (pl.inv)
    pl.inv--;
  if (pl.cool)
    pl.cool--;
  if (pl.noflash)
    pl.noflash--;

  if (key_hit(KEY_A) && !pl.cool) {
    if (pl.coins > 0)
      player_throw();
    else {
      pl.noflash = 40;
      sfx_play(SFX_EMPTY);
    }
  }
  if (key_hit(KEY_B)) {
    i = pl.discos ? shiny_alloc() : -1;
    if (i >= 0) {
      Shiny *s = &shs[i];
      s->state = SH_LAND;
      s->type = ST_DISCO;
      s->owner = -1;
      s->x = pl.x;
      s->y = pl.y + FX(2);
      s->z = 0;
      s->timer = 480;
      s->age = 0;
      pl.discos--;
      sfx_play(SFX_DISCO);
      sparkle_burst(s->x, s->y - FX(8), 8);
    } else
      sfx_play(SFX_EMPTY);
  }

  // scoop up dropped jewels
  for (i = 0; i < NJEWEL; i++) {
    if (jgrace[i])
      jgrace[i]--;
    if (jst[i] == J_GROUND &&
        dist2(jx[i], jy[i], pl.x >> 8, pl.y >> 8) < 13 * 13) {
      jst[i] = J_OWNED;
      popup_add(2, pl.x, pl.y - FX(8));
      sparkle_burst(pl.x, pl.y - FX(14), 6);
      sfx_play(SFX_RECOVER);
    }
  }
}

// === DRAWING ==========================================================

typedef struct {
  s16 key;
  u16 a0, a1, a2;
} DSpr;
static DSpr dl[110];
static u8 dorder[110];
static int dn;

static void dpush(int key, int x, int y, u16 a0, u16 a1, u16 a2) {
  if (dn >= 110 || x <= -32 || x >= 240 || y <= -32 || y >= 160)
    return;
  DSpr *d = &dl[dn++];
  d->key = key;
  d->a0 = a0 | (y & 0xFF);
  d->a1 = a1 | (x & 0x1FF);
  d->a2 = a2;
}

static void hud_spr(int x, int y, u16 a0, u16 a1, u16 a2) {
  if (oam_n < 128)
    obj_set_attr(&obj_buffer[oam_n++], a0 | (y & 0xFF), a1 | (x & 0x1FF), a2);
}

#define A2(tile, pal) ATTR2_BUILD(tile, pal, 2)
#define KEY_SHADOW (-30000)
#define KEY_TOP 30000

static void draw_world(void) {
  static const u8 walk[4] = {0, 1, 0, 2};
  static const u16 coinspin[4] = {OT_COIN0, OT_COIN1, OT_COIN2, OT_COIN1};
  int i, k;
  int ox = -cam_x, oy = -cam_y;

  if (shake) {
    ox += (shake & 2) ? 1 : -1;
    oy += (shake & 1) ? 1 : -1;
  }
  dn = 0;

  // the Duchess
  {
    int sx = (pl.x >> 8) + ox, sy = (pl.y >> 8) + oy;
    static const u16 base[3] = {OT_DU_D0, OT_DU_U0, OT_DU_S0};
    int f = pl.moving ? walk[(pl.anim >> 3) & 3] : 0;
    if (!(pl.inv && (frame & 2)))
      dpush(sy, sx - 8, sy - 30, ATTR0_TALL,
            ATTR1_SIZE_16x32 | (pl.flip && pl.dir == 2 ? ATTR1_HFLIP : 0),
            A2(base[pl.dir] + f * 8, 0));
    dpush(KEY_SHADOW, sx - 8, sy - 5, ATTR0_WIDE | ATTR0_BLEND, ATTR1_SIZE_16x8,
          A2(OT_SHADOW, OBJPAL_FX));
  }

  // raccoons
  for (i = 0; i < MAX_RAC; i++) {
    Rac *r = &racs[i];
    if (r->state == RS_NONE)
      continue;
    int sx = (r->x >> 8) + ox, sy = (r->y >> 8) + oy;
    int tile, flip = 0, bob = 0, top;
    static const u16 fr[3] = {OT_RC_D0, OT_RC_U0, OT_RC_S0};

    if (sx < -24 || sx > 264 || sy < -24 || sy > 190) {
      // a thief is getting away off-screen: point at it
      if (r->state == RS_STEAL && (frame & 8)) {
        int ax = clamp(sx - 4, 2, 230), ay = clamp(sy - 12, 14, 150);
        if (sx < 0 || sx > 240)
          hud_spr(ax, ay, 0, sx < 0 ? ATTR1_HFLIP : 0,
                  ATTR2_BUILD(OT_ARROW_R, OBJPAL_FX, 0));
        else
          hud_spr(ax, ay, 0, sy > 80 ? ATTR1_VFLIP : 0,
                  ATTR2_BUILD(OT_ARROW_U, OBJPAL_FX, 0));
      }
      continue;
    }

    switch (r->state) {
    case RS_PLAY:
      tile = (r->anim & 16) ? OT_RC_P1 : OT_RC_P0;
      break;
    case RS_DANCE:
      tile = (r->anim & 8) ? OT_RC_P1 : OT_RC_P0;
      flip = (r->anim & 16) != 0;
      bob = (r->anim & 8) ? -2 : 0;
      break;
    case RS_WINDUP:
      tile = fr[r->dir];
      flip = r->flip;
      sx += (frame & 2) ? 1 : -1; // trembling with anticipation
      break;
    case RS_TIRED:
    case RS_BORED:
      tile = OT_RC_D0;
      break;
    default:
      tile = fr[r->dir] + ((r->anim >> 3) & 1) * 4;
      flip = r->flip;
    }
    if (r->dir != 2 && r->state != RS_DANCE)
      flip = 0;

    if (r->type == RT_REG) {
      top = sy - 15 + bob;
      dpush(sy, sx - 7, top, 0, ATTR1_SIZE_16x16 | (flip ? ATTR1_HFLIP : 0),
            A2(tile, 1));
      dpush(KEY_SHADOW, sx - 8, sy - 4, ATTR0_WIDE | ATTR0_BLEND,
            ATTR1_SIZE_16x8, A2(OT_SHADOW, OBJPAL_FX));
    } else if (r->type == RT_KIT) {
      top = sy - 13 + bob;
      dpush(sy, sx - 7, sy - 14 + bob, ATTR0_AFF,
            ATTR1_SIZE_16x16 | ATTR1_AFF_ID(flip ? 1 : 0), A2(tile, 2));
      dpush(KEY_SHADOW, sx - 4, sy - 5, ATTR0_BLEND, ATTR1_SIZE_8x8,
            A2(OT_SHADOWS, OBJPAL_FX));
    } else {
      top = sy - 22 + bob;
      dpush(sy, sx - 16, sy - 27 + bob, ATTR0_AFF_DBL,
            ATTR1_SIZE_16x16 | ATTR1_AFF_ID(flip ? 3 : 2), A2(tile, 3));
      dpush(KEY_SHADOW, sx - 8, sy - 4, ATTR0_WIDE | ATTR0_BLEND,
            ATTR1_SIZE_16x8, A2(OT_SHADOW, OBJPAL_FX));
    }

    if (r->jewel >= 0)
      dpush(KEY_TOP, sx - 8, top - 13 + ((frame >> 3) & 1), 0, ATTR1_SIZE_16x16,
            A2(jewel_tile[r->jewel], OBJPAL_FX));
    else if (r->state == RS_WINDUP)
      dpush(KEY_TOP, sx - 3, top - 9, 0, 0, A2(OT_ALERT, OBJPAL_FX));
    else if (r->state == RS_BORED || r->state == RS_TIRED)
      dpush(KEY_TOP, sx - 4, top - 8 + ((frame >> 3) & 1), 0, 0,
            A2(OT_DIZZY, OBJPAL_FX));
  }

  // coins + disco balls
  for (i = 0; i < MAX_SHINY; i++) {
    Shiny *s = &shs[i];
    if (s->state == SH_NONE)
      continue;
    int sx = (s->x >> 8) + ox, sy = (s->y >> 8) + oy;
    if (s->type == ST_DISCO) {
      if (s->timer < 90 && (frame & 4))
        continue;
      dpush(sy + 2, sx - 8, sy - 20 + ((frame >> 4) & 1), 0, ATTR1_SIZE_16x16,
            A2((frame & 8) ? OT_DISCO1 : OT_DISCO0, OBJPAL_FX));
      dpush(KEY_SHADOW, sx - 8, sy - 4, ATTR0_WIDE | ATTR0_BLEND,
            ATTR1_SIZE_16x8, A2(OT_SHADOW, OBJPAL_FX));
    } else if (s->state == SH_HELD) {
      // held aloft by a delighted raccoon
      int lift = racs[s->owner].type == RT_CHONK ? 30 : 22;
      dpush(KEY_TOP, sx - 4, sy - lift - ((frame >> 3) & 1), 0, 0,
            A2(coinspin[(frame >> 3) & 3], OBJPAL_FX));
    } else {
      if (s->state == SH_LAND && s->timer < 90 && (frame & 4))
        continue;
      int spin = s->state == SH_FLY ? (frame >> 1) : (frame >> 3);
      dpush(sy, sx - 4, sy - 7 - s->z, 0, 0, A2(coinspin[spin & 3], OBJPAL_FX));
      dpush(KEY_SHADOW, sx - 4, sy - 5, ATTR0_BLEND, 0,
            A2(OT_SHADOWS, OBJPAL_FX));
    }
  }

  // pickups
  for (i = 0; i < MAX_PICK; i++) {
    Pickup *p = &picks[i];
    if (p->type == PK_NONE)
      continue;
    int sx = (p->x >> 8) + ox, sy = (p->y >> 8) + oy;
    int bob = (p->anim >> 3) & 3;
    bob = bob == 3 ? 1 : bob;
    if (p->type == PK_COIN)
      dpush(sy, sx - 4, sy - 9 - bob, 0, 0,
            A2(coinspin[(p->anim >> 3) & 3], OBJPAL_FX));
    else if (p->type == PK_STACK)
      dpush(sy, sx - 8, sy - 14, 0, ATTR1_SIZE_16x16, A2(OT_STACK, OBJPAL_FX));
    else
      dpush(sy, sx - 8, sy - 16 - bob, 0, ATTR1_SIZE_16x16,
            A2((p->anim & 8) ? OT_DISCO1 : OT_DISCO0, OBJPAL_FX));
    if (p->type != PK_STACK)
      dpush(KEY_SHADOW, sx - 4, sy - 5, ATTR0_BLEND, 0,
            A2(OT_SHADOWS, OBJPAL_FX));
  }

  // jewels lying on the lawn
  for (i = 0; i < NJEWEL; i++)
    if (jst[i] == J_GROUND && !(jgrace[i] && (frame & 2) && jgrace[i] < 60)) {
      int sx = jx[i] + ox, sy = jy[i] + oy;
      dpush(sy, sx - 8, sy - 13 - ((frame >> 4) & 1), 0, ATTR1_SIZE_16x16,
            A2(jewel_tile[i], OBJPAL_FX));
      dpush(KEY_SHADOW, sx - 4, sy - 4, ATTR0_BLEND, 0,
            A2(OT_SHADOWS, OBJPAL_FX));
      if ((frame & 15) == 0)
        part_add(PT_SPARK, (jx[i] + rnd(13) - 6) << 8,
                 (jy[i] - 4 - rnd(10)) << 8, 0, -20, 16);
    }

  // particles
  for (i = 0; i < MAX_PART; i++) {
    Part *p = &parts[i];
    if (!p->life)
      continue;
    int sx = (p->x >> 8) + ox, sy = (p->y >> 8) + oy, tile;
    switch (p->type) {
    case PT_SPARK:
      tile = OT_SPARK0 + (p->life > 12 ? 2 : p->life > 6 ? 1 : 0);
      break;
    case PT_HEART:
      tile = OT_HEART;
      break;
    case PT_NOTE:
      tile = OT_NOTE;
      break;
    default:
      tile = p->life > 6 ? OT_DUST0 : OT_DUST1;
    }
    dpush(p->type == PT_DUST ? sy : KEY_TOP, sx - 4, sy - 4, 0, 0,
          A2(tile, OBJPAL_FX));
  }

  // score popups
  for (i = 0; i < MAX_POP; i++)
    if (pops[i].life)
      dpush(KEY_TOP, pops[i].x + ox - 16,
            pops[i].y + oy - (50 - pops[i].life) / 3, ATTR0_WIDE,
            ATTR1_SIZE_32x8, A2(OT_POP20 + pops[i].type * 4, OBJPAL_FX));

  // depth sort (insertion sort: the list is short and nearly sorted)
  for (i = 0; i < dn; i++) {
    int key = dl[i].key;
    for (k = i; k > 0 && dl[dorder[k - 1]].key < key; k--)
      dorder[k] = dorder[k - 1];
    dorder[k] = i;
  }
  for (i = 0; i < dn && oam_n < 128; i++) {
    DSpr *d = &dl[dorder[i]];
    obj_set_attr(&obj_buffer[oam_n++], d->a0, d->a1, d->a2);
  }
}

static void draw_hud(void) {
  int i;
  SCR_ENTRY *row = &se_mem[SBB_HUD][0];
  int maxc = 6 + 3 * perk_lv[PERK_POCKETS];
  const char *s;

  // jewels
  for (i = 0; i < NJEWEL; i++) {
    if (jst[i] == J_LOST)
      continue;
    if (jst[i] != J_OWNED && (frame & 8))
      continue; // in raccoon paws: blink
    hud_spr(1 + i * 14, -3, 0, ATTR1_SIZE_16x16,
            ATTR2_BUILD(jewel_tile[i], OBJPAL_FX, 0));
  }
  // coins
  row[10] = (T_HUD + 1) | SE_PALBANK(PAL_WHITE);
  s = num(pl.coins, 2, ' ');
  i = (pl.noflash & 4) ? PAL_PINK : (pl.coins >= maxc ? PAL_GOLD : PAL_WHITE);
  row[11] = (T_BARFONT + s[0] - 32) | SE_PALBANK(i);
  row[12] = (T_BARFONT + s[1] - 32) | SE_PALBANK(i);
  // disco balls
  row[14] = (T_HUD + 2) | SE_PALBANK(PAL_WHITE);
  row[15] = (T_BARFONT + '0' + pl.discos - 32) |
            SE_PALBANK(pl.discos ? PAL_CYAN : PAL_GREY);
  // night -> dawn track
  hud_spr(160 + night_t * 56 / night_len, 2, 0, 0,
          ATTR2_BUILD(OT_MOON, OBJPAL_FX, 0));
  // score
  txt_puts(23, 18, num(score, 6, '0'), PAL_WHITE);
}

static void hud_init(void) {
  int i;
  SCR_ENTRY *row = &se_mem[SBB_HUD][0];
  memset16(se_mem[SBB_HUD], 0, 32 * 32);
  for (i = 0; i < 32; i++) {
    row[i] = T_HUD | SE_PALBANK(PAL_WHITE);
    row[31 * 32 + i] = T_HUD | SE_PALBANK(PAL_WHITE);
  }
  for (i = 20; i < 27; i++)
    row[i] = (T_HUD + 3) | SE_PALBANK(PAL_WHITE);
  row[27] = (T_HUD + 4) | SE_PALBANK(PAL_WHITE);
}

static void oam_begin(void) { oam_n = 0; }
static void oam_end(void) {
  int i;
  for (i = oam_n; i < 128; i++)
    obj_buffer[i].attr0 = ATTR0_HIDE;
}

static void camera_update(void) {
  cam_x = clamp((pl.x >> 8) - 120, 0, WORLD_W - 240 + 1);
  cam_y = clamp((pl.y >> 8) - 92, 0, WORLD_H - 160 + 1);
}

static void fx_update(void) {
  int i;
  for (i = 0; i < MAX_PART; i++)
    if (parts[i].life) {
      parts[i].life--;
      parts[i].x += parts[i].vx;
      parts[i].y += parts[i].vy;
    }
  for (i = 0; i < MAX_POP; i++)
    if (pops[i].life)
      pops[i].life--;
  if (shake)
    shake--;
}

static void draw_all(void) {
  oam_begin();
  draw_hud();
  draw_world();
  oam_end();
}

// === VIDEO MODES ======================================================

static void video_game(void) {
  int tx, ty;

  REG_DISPCNT = DCNT_BLANK;
  title_mode = 0;
  memset32(vid_mem, 0, 0x10000 / 4);
  memcpy32(tile_mem[CBB_MAP], gfx_bg_tiles, sizeof(gfx_bg_tiles) / 4);
  memcpy32(tile_mem[4], gfx_obj_tiles, sizeof(gfx_obj_tiles) / 4);
  font_init();
  for (ty = 0; ty < MAP_TH; ty++)
    for (tx = 0; tx < MAP_TW; tx++) {
      int sbb = (ty >> 5) * 2 + (tx >> 5), o = (ty & 31) * 32 + (tx & 31);
      se_mem[SBB_GND + sbb][o] = gfx_map_ground[ty * MAP_TW + tx];
      se_mem[SBB_OVR + sbb][o] = gfx_map_over[ty * MAP_TW + tx];
    }
  hud_init();

  oam_init(obj_buffer, 128);
  // affine slots: 0/1 kit (small), 2/3 chonk (big); odd slots are mirrored
  OBJ_AFFINE *aff = (OBJ_AFFINE *)obj_buffer;
  obj_aff_set(&aff[0], 0x148, 0, 0, 0x148);
  obj_aff_set(&aff[1], -0x148, 0, 0, 0x148);
  obj_aff_set(&aff[2], 0x0B8, 0, 0, 0x0B8);
  obj_aff_set(&aff[3], -0x0B8, 0, 0, 0x0B8);

  REG_BG0CNT = BG_CBB(CBB_TXT) | BG_SBB(SBB_HUD) | BG_REG_32x32 | BG_PRIO(0);
  REG_BG1CNT = BG_CBB(CBB_MAP) | BG_SBB(SBB_GND) | BG_REG_64x64 | BG_PRIO(3);
  REG_BG2CNT = BG_CBB(CBB_MAP) | BG_SBB(SBB_OVR) | BG_REG_64x64 | BG_PRIO(1);
  REG_BG0HOFS = 0;
  REG_BG0VOFS = -2;
  // only the (semi-transparent) shadow sprites blend
  REG_BLDCNT = BLD_BUILD(0, BLD_BG1 | BLD_BG2 | BLD_BACKDROP, 1);
  REG_BLDALPHA = BLDA_BUILD(9, 8);
  pal_dirty = 1;
  pal_flush();
  oam_copy(oam_mem, obj_buffer, 128);
  REG_DISPCNT =
      DCNT_MODE0 | DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_OBJ | DCNT_OBJ_1D;
}

// --- mode 4 helpers for the title screen ---

static inline void m4_px(int x, int y, int c) {
  u16 *dst = &vid_mem[(y * 240 + x) >> 1];
  if (x & 1)
    *dst = (*dst & 0x00FF) | (c << 8);
  else
    *dst = (*dst & 0xFF00) | c;
}

static void m4_text(int x, int y, const char *s, int clr) {
  const u8 *gl = (const u8 *)sys8Glyphs;
  int ix, iy, pass;
  for (pass = 0; pass < 2; pass++) {
    const char *p = s;
    int cx = x + (pass ? 0 : 1), cy = y + (pass ? 0 : 1);
    for (; *p; p++, cx += 8)
      for (iy = 0; iy < 8; iy++)
        for (ix = 0; ix < 8; ix++)
          if ((gl[(*p - 32) * 8 + iy] >> ix) & 1)
            m4_px(cx + ix, cy + iy, pass ? clr : 4);
  }
}

static void m4_restore(int x, int y, int w, int h) {
  const u8 *src = (const u8 *)gfx_title_bmp;
  int ix, iy;
  for (iy = y; iy < y + h; iy++)
    for (ix = x; ix < x + w; ix++)
      m4_px(ix, iy, src[iy * 240 + ix]);
}

static void title_fade(int level) {
  clr_fade_fast((COLOR *)gfx_title_pal, CLR_BLACK, pal_bg_mem, 256, level);
}

static void video_title(void) {
  REG_DISPCNT = DCNT_BLANK;
  title_mode = 1;
  REG_BLDCNT = 0;
  memcpy32(vid_mem, gfx_title_bmp, sizeof(gfx_title_bmp) / 4);
  title_fade(32);
  if (hiscore) {
    char buf[24] = "BEST ";
    strcat(buf, num(hiscore, 6, '0'));
    m4_text(120 - 44, 132, buf, 5);
  }
  REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;
}

// === GAME FLOW ========================================================

static void world_clear(void) {
  memset(racs, 0, sizeof(racs));
  memset(shs, 0, sizeof(shs));
  memset(picks, 0, sizeof(picks));
  memset(parts, 0, sizeof(parts));
  memset(pops, 0, sizeof(pops));
}

static void night_start(void) {
  int i;
  world_clear();
  pl.x = FX(240);
  pl.y = FX(72);
  pl.dir = 0;
  pl.flip = 0;
  pl.fx = 0;
  pl.fy = 1;
  pl.inv = pl.cool = pl.anim = pl.moving = 0;
  pl.coins = 6 + 3 * perk_lv[PERK_POCKETS];
  pl.discos = imin(3, pl.discos + perk_lv[PERK_DISCO]);
  night_len = night_cfg[night][0] * 60;
  night_t = 0;
  spawn_t = night_cfg[night][2] - 70;
  pick_t = 0;
  dawn_a = 0;
  fade_game = 0;
  shake = 0;
  for (i = 0; i < 3; i++)
    pickup_spawn(i ? PK_COIN : PK_STACK);
  camera_update();

  txt_clear();
  {
    char buf[12] = "NIGHT ";
    strcat(buf, num(night + 1, 1, ' '));
    big_center(5, buf, PAL_GOLD);
  }
  txt_center(9, night_blurb[night], PAL_CYAN);
  if (night == 0) {
    txt_center(13, "+ MOVE      A THROW A COIN", PAL_WHITE);
    txt_center(15, "B DISCO BALL    L/R STRAFE", PAL_WHITE);
  }
  gs = GS_INTRO;
  gtimer = night == 0 ? 210 : 130;
  music_tempo(night >= 3 ? -1 : 0);
  music_play(SONG_GAME);
  pal_dirty = 1;
}

static void game_new(void) {
  int i;
  sqran(frame * 7919 + 12345);
  score = 0;
  night = 0;
  memset(perk_lv, 0, sizeof(perk_lv));
  for (i = 0; i < NJEWEL; i++) {
    jst[i] = J_OWNED;
    jgrace[i] = 0;
  }
  pl.discos = 1;
  video_game();
  fade_all = 32;
  night_start();
  draw_all();
  fade_all_to(0);
}

static void to_title(void) {
  int i;
  if (!title_mode)
    fade_all_to(32);
  music_stop();
  oam_init(obj_buffer, 128);
  oam_copy(oam_mem, obj_buffer, 128);
  video_title();
  gs = GS_TITLE;
  gtimer = 0;
  music_tempo(0);
  music_play(SONG_TITLE);
  for (i = 32; i >= 0; i -= 2) {
    VBlankIntrWait();
    title_fade(i);
    key_poll();
    snd_update();
    frame++;
  }
}

static void record_score(void) {
  if (score > hiscore) {
    hiscore = score;
    hinight = night + 1;
    save_store();
  }
}

static void spawner(void) {
  const u8 *cfg = night_cfg[night];
  int i, n = 0;
  // the gang grows as the night wears on
  int cap = 1 + (cfg[1] - 1) * imin(night_len, night_t * 3 / 2) / night_len;

  for (i = 0; i < MAX_RAC; i++)
    n += racs[i].state != RS_NONE;
  if (++spawn_t >= cfg[2]) {
    spawn_t = 0;
    if (n < cap) {
      int k = rnd(100);
      rac_spawn(k < cfg[3] ? RT_KIT
                           : (k < cfg[3] + cfg[4] ? RT_CHONK : RT_REG));
    }
  }
}

static void dawn_begin(void) {
  int i;
  gs = GS_DAWN;
  gtimer = 0;
  music_play(SONG_DAWN);
  for (i = 0; i < MAX_RAC; i++) {
    Rac *r = &racs[i];
    if (r->state == RS_NONE)
      continue;
    jewel_drop(r);
    r->state = RS_FLEE;
    r->leaving = 0;
  }
  for (i = 0; i < MAX_SHINY; i++)
    if (shs[i].state == SH_HELD)
      shs[i].state = SH_NONE;
  // anything still on the lawn finds its way home
  for (i = 0; i < NJEWEL; i++)
    if (jst[i] == J_GROUND || jst[i] == J_CARRIED)
      jst[i] = J_OWNED;
  sparkle_burst(pl.x, pl.y - FX(14), 8);
  txt_clear();
  big_center(6, "DAWN!", PAL_GOLD);
}

static void summary_show(void) {
  char buf[32];
  int kept = jewels_in(J_OWNED);
  int bonus = kept * 200 + pl.coins * 10;

  txt_clear();
  strcpy(buf, "NIGHT ");
  strcat(buf, num(night + 1, 1, ' '));
  strcat(buf, " SURVIVED");
  txt_center(4, buf, PAL_GOLD);

  strcpy(buf, "JEWELS KEPT   ");
  strcat(buf, num(kept, 1, ' '));
  strcat(buf, " x 200");
  txt_center(8, buf, PAL_WHITE);
  strcpy(buf, "COINS LEFT   ");
  strcat(buf, num(pl.coins, 2, ' '));
  strcat(buf, " x  10");
  txt_center(10, buf, PAL_WHITE);
  strcpy(buf, "BONUS  ");
  strcat(buf, num(bonus, 5, ' '));
  txt_center(12, buf, PAL_CYAN);
  score += bonus;
  txt_center(16, "PRESS A", PAL_GREY);
  gs = GS_SUMMARY;
  gtimer = 0;
}

static void perk_draw(void) {
  int i;
  txt_clear();
  txt_center(2, "A LITTLE SOMETHING", PAL_GOLD);
  txt_center(3, "FROM THE BUTLER", PAL_GOLD);
  for (i = 0; i < 3; i++) {
    const PerkDef *p = &perks[perk_opt[i]];
    int sel = (i == perk_sel);
    txt_puts(2, 6 + i * 4, sel ? ">" : " ", PAL_GOLD);
    txt_puts(4, 6 + i * 4, p->name, sel ? PAL_GOLD : PAL_WHITE);
    txt_puts(4, 7 + i * 4, p->desc, sel ? PAL_CYAN : PAL_GREY);
  }
}

static void perk_show(void) {
  int i, k, n = 0;
  int pool[NPERK];
  for (i = 0; i < NPERK; i++) {
    if (perk_lv[i] >= perks[i].max)
      continue;
    if (i == PERK_HEIRLOOM && !jewels_in(J_LOST))
      continue;
    pool[n++] = i;
  }
  for (i = 0; i < 3; i++) {
    k = rnd(n);
    perk_opt[i] = pool[k];
    pool[k] = pool[--n];
  }
  perk_sel = 0;
  perk_draw();
  gs = GS_PERK;
}

static void perk_take(int id) {
  int i;
  perk_lv[id]++;
  if (id == PERK_HEIRLOOM) {
    perk_lv[id] = 0;
    for (i = 0; i < NJEWEL; i++)
      if (jst[i] == J_LOST) {
        jst[i] = J_OWNED;
        break;
      }
  }
}

static void over_show(void) {
  char buf[32];
  record_score();
  txt_clear();
  big_center(4, "GAME OVER", PAL_PINK);
  txt_center(8, "The raccoons got the lot.", PAL_WHITE);
  strcpy(buf, "SCORE ");
  strcat(buf, num(score, 6, '0'));
  txt_center(11, buf, PAL_GOLD);
  strcpy(buf, "BEST  ");
  strcat(buf, num(hiscore, 6, '0'));
  txt_center(12, buf, PAL_GREY);
  txt_center(16, "PRESS START", PAL_GREY);
  music_play(SONG_LOSE);
  gs = GS_OVER;
  gtimer = 0;
}

static void win_show(void) {
  char buf[32];
  int kept = jewels_in(J_OWNED);
  score += kept * 500;
  record_score();
  txt_clear();
  big_center(3, "YOU WIN!", PAL_GOLD);
  txt_center(7, "Five nights, and the family", PAL_WHITE);
  if (kept == NJEWEL)
    txt_center(8, "jewels never left your side!", PAL_WHITE);
  else {
    strcpy(buf, "still has ");
    strcat(buf, num(kept, 1, ' '));
    strcat(buf, " of its 5 jewels.");
    txt_center(8, buf, PAL_WHITE);
  }
  strcpy(buf, "SCORE ");
  strcat(buf, num(score, 6, '0'));
  txt_center(11, buf, PAL_GOLD);
  strcpy(buf, "BEST  ");
  strcat(buf, num(hiscore, 6, '0'));
  txt_center(12, buf, PAL_GREY);
  txt_center(16, "PRESS START", PAL_GREY);
  music_play(SONG_VICTORY);
  gs = GS_WIN;
  gtimer = 0;
}

static void set_dawn(int a) {
  if (a != dawn_a) {
    dawn_a = a;
    pal_dirty = 1;
  }
}

static void set_dim(int f) {
  if (f != fade_game) {
    fade_game = f;
    pal_dirty = 1;
  }
}

static void update_nav(void) {
  if ((frame & 7) == 0) {
    u16 seed = clamp(pl.y >> 12, 0, MAP_H - 1) * MAP_W +
               clamp(pl.x >> 12, 0, MAP_W - 1);
    bfs(dist_p, cellblk, &seed, 1);
  }
}

static void state_update(void) {
  int i;
  switch (gs) {
  case GS_TITLE:
    gtimer++;
    // twinkling stars and blinking eyes, all by palette
    pal_bg_mem[1] = (gtimer & 32) ? gfx_title_pal[1] : RGB15(14, 16, 26);
    pal_bg_mem[7] = ((gtimer % 230) < 8) ? RGB15(1, 4, 4) : gfx_title_pal[7];
    pal_bg_mem[8] =
        (((gtimer + 90) % 310) < 8) ? RGB15(1, 4, 4) : gfx_title_pal[8];
    if ((gtimer & 31) == 1) {
      if (!(gtimer & 32))
        m4_text(120 - 44, 146, "PRESS START", 3);
      else
        m4_restore(120 - 44, 146, 90, 10);
    }
    if (key_hit(KEY_START | KEY_A)) {
      sfx_play(SFX_OK);
      for (i = 0; i <= 32; i += 2) {
        VBlankIntrWait();
        title_fade(i);
        snd_update();
        frame++;
      }
      music_stop();
      game_new();
    }
    break;

  case GS_INTRO:
    fx_update();
    draw_all();
    if (--gtimer <= 0 || (key_hit(KEY_A | KEY_START) && gtimer < 100)) {
      txt_clear();
      gs = GS_PLAY;
    }
    break;

  case GS_PLAY:
    if (key_hit(KEY_START)) {
      gs = GS_PAUSE;
      big_center(8, "PAUSED", PAL_WHITE);
      set_dim(14);
      sfx_play(SFX_MENU);
      break;
    }
    update_nav();
    player_update();
    spawner();
    racs_update();
    shinies_update();
    pickups_update();
    fx_update();
    camera_update();
    draw_all();

    night_t++;
    i = night_t * 100 / night_len;
    set_dawn(i < 60 ? 0 : (i - 60) * 12 / 40);
    if (jewels_in(J_LOST) == NJEWEL) {
      set_dim(16);
      over_show();
    } else if (night_t >= night_len)
      dawn_begin();
    break;

  case GS_PAUSE:
    if (key_hit(KEY_START)) {
      txt_clear();
      set_dim(0);
      gs = GS_PLAY;
    }
    break;

  case GS_DAWN:
    gtimer++;
    update_nav();
    player_update();
    racs_update();
    shinies_update();
    pickups_update();
    fx_update();
    camera_update();
    draw_all();
    set_dawn(imin(32, 12 + gtimer / 5));
    if (gtimer == 150)
      txt_clear();
    if (gtimer >= 210) {
      set_dim(18);
      music_play(SONG_WIN);
      summary_show();
    }
    break;

  case GS_SUMMARY:
    gtimer++;
    draw_all();
    if (gtimer > 30 && key_hit(KEY_A | KEY_START)) {
      sfx_play(SFX_OK);
      if (night == NNIGHTS - 1)
        win_show();
      else
        perk_show();
    }
    break;

  case GS_PERK:
    draw_all();
    i = key_tri_vert();
    if (i && key_hit(KEY_UP | KEY_DOWN)) {
      perk_sel = (perk_sel + i + 3) % 3;
      perk_draw();
      sfx_play(SFX_MENU);
    }
    if (key_hit(KEY_A)) {
      sfx_play(SFX_OK);
      perk_take(perk_opt[perk_sel]);
      fade_all_to(32);
      night++;
      night_start();
      draw_all();
      fade_all_to(0);
    }
    break;

  case GS_OVER:
  case GS_WIN:
    gtimer++;
    if (gs == GS_WIN) {
      if ((gtimer & 7) == 0)
        sparkle_burst(pl.x + FX(rnd(161) - 80), pl.y + FX(rnd(101) - 50), 2);
      fx_update();
    }
    draw_all();
    if (gtimer > 60 && key_hit(KEY_START | KEY_A))
      to_title();
    break;
  }
}

int main(void) {
  irq_init(NULL);
  irq_add(II_VBLANK, NULL);
  snd_init();
  save_load();
  nav_init();

  REG_DISPCNT = DCNT_BLANK;
  title_mode = 1;
  to_title();

  while (1) {
    vsync();
    state_update();
    dbg_load = (REG_VCOUNT + 228 - 160) % 228;
    if (dbg_load > dbg_load_max)
      dbg_load_max = dbg_load;
  }
  return 0;
}
