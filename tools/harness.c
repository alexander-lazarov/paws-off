// Headless mGBA test harness: runs a ROM with scripted input, dumps PNG
// screenshots. usage: harness rom.gba [cmd ...]
//   N            run N frames with current keys
//   +A / -A      press / release key (A B SELECT START RIGHT LEFT UP DOWN R L)
//   shot:file    save PNG screenshot (2x scaled)
//   snap         remember the current frame;  rep:N:K  = N times (run K frames,
//   snap) grid:C:file  save all snaps as a C-column contact sheet (1x)
//   bot:N:skill:every  let the auto-player play N frames (snap every `every`)
//   wav:file     start recording audio (mono, 32768 Hz) -- only plain `N` frame
//   runs are captured peek:addr:n  print n bytes at hex address poke:addr:v
//   write byte
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/blip_buf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void nolog(struct mLogger *l, int c, enum mLogLevel lv, const char *f,
                  va_list a) {}
static struct mLogger logger = {.log = nolog};

static void put32(unsigned char *p, unsigned v) {
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}
static void chunk(FILE *f, const char *type, const unsigned char *data,
                  unsigned len) {
  unsigned char b[4];
  put32(b, len);
  fwrite(b, 1, 4, f);
  unsigned crc = crc32(0, (const unsigned char *)type, 4);
  if (len)
    crc = crc32(crc, data, len);
  fwrite(type, 1, 4, f);
  if (len)
    fwrite(data, 1, len, f);
  put32(b, crc);
  fwrite(b, 1, 4, f);
}
static void save_png(const char *fn, const unsigned *px, int w, int h, int sc) {
  int W = w * sc, H = h * sc;
  unsigned long rawlen = (W * 3 + 1) * H;
  unsigned char *raw = malloc(rawlen);
  unsigned char *p = raw;
  for (int y = 0; y < H; y++) {
    *p++ = 0;
    for (int x = 0; x < W; x++) {
      unsigned c = px[(y / sc) * w + (x / sc)];
      *p++ = c & 255;
      *p++ = (c >> 8) & 255;
      *p++ = (c >> 16) & 255;
    }
  }
  unsigned long zl = compressBound(rawlen);
  unsigned char *z = malloc(zl);
  compress(z, &zl, raw, rawlen);
  FILE *f = fopen(fn, "wb");
  fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
  unsigned char hdr[13];
  put32(hdr, W);
  put32(hdr + 4, H);
  hdr[8] = 8;
  hdr[9] = 2;
  hdr[10] = hdr[11] = hdr[12] = 0;
  chunk(f, "IHDR", hdr, 13);
  chunk(f, "IDAT", z, zl);
  chunk(f, "IEND", 0, 0);
  fclose(f);
  free(raw);
  free(z);
}
static unsigned *snaps[64];
static int nsnap;
static void save_grid(const char *fn, int w, int h, int cols) {
  if (!nsnap)
    return;
  int rows = (nsnap + cols - 1) / cols;
  int W = cols * (w + 2), H = rows * (h + 2);
  unsigned *g = calloc(W * H, 4);
  for (int i = 0; i < nsnap; i++)
    for (int y = 0; y < h; y++)
      memcpy(g + ((i / cols) * (h + 2) + y) * W + (i % cols) * (w + 2),
             snaps[i] + y * w, w * 4);
  save_png(fn, g, W, H, 1);
  free(g);
  for (int i = 0; i < nsnap; i++)
    free(snaps[i]);
  nsnap = 0;
}
static int keybit(const char *n) {
  const char *names[] = {"A",    "B",  "SELECT", "START", "RIGHT",
                         "LEFT", "UP", "DOWN",   "R",     "L"};
  for (int i = 0; i < 10; i++)
    if (!strcmp(n, names[i]))
      return 1 << i;
  fprintf(stderr, "bad key %s\n", n);
  exit(1);
}
// ---- audio capture ----
static FILE *wavf;
static long wavn;
static struct mCore *wavcore;
static void wav_pump(void) {
  if (!wavf)
    return;
  blip_t *l = wavcore->getAudioChannel(wavcore, 0);
  blip_t *r = wavcore->getAudioChannel(wavcore, 1);
  int n = blip_samples_avail(l);
  static short buf[8192];
  while (n > 0) {
    int k = n > 8192 ? 8192 : n;
    blip_read_samples(l, buf, k, 0);
    fwrite(buf, 2, k, wavf);
    wavn += k;
    n -= k;
  }
  n = blip_samples_avail(r);
  while (n > 0) {
    int k = n > 8192 ? 8192 : n;
    blip_read_samples(r, buf, k, 0);
    n -= k;
  }
}
static void wav_close(void) {
  if (!wavf)
    return;
  unsigned hdr[11] = {0x46464952, 36 + wavn * 2, 0x45564157, 0x20746d66,
                      16,         0x00010001,    32768,      65536,
                      0x00100002, 0x61746164,    wavn * 2};
  fseek(wavf, 0, SEEK_SET);
  fwrite(hdr, 4, 11, wavf);
  fclose(wavf);
  wavf = 0;
}
// ---- auto-player (dev tool): reads game state straight out of RAM ----
// Symbol addresses come from `nm` (see tools/bot.sh): sym:name=hexaddr
static unsigned A_pl, A_racs, A_picks, A_jst, A_jx, A_jy, A_gs, A_score,
    A_night;
static int rd32(struct mCore *c, unsigned a) { return (int)c->busRead32(c, a); }
static int rd16(struct mCore *c, unsigned a) {
  return (short)c->busRead16(c, a);
}
static int rd8(struct mCore *c, unsigned a) { return c->busRead8(c, a); }
static int dirkeys(int dx, int dy) {
  int k = 0;
  int ax = abs(dx), ay = abs(dy);
  if (ax * 5 > ay * 2)
    k |= dx > 0 ? 16 : 32;
  if (ay * 5 > ax * 2)
    k |= dy > 0 ? 128 : 64;
  return k;
}
static int bot_step(struct mCore *c, long f, int skill) {
  static int lastgs = -1;
  static int cool = 0;
  int gs = rd8(c, A_gs), keys = 0;
  if (gs != lastgs) {
    if (gs == 5 || gs == 7 || gs == 8) {
      int own = 0;
      for (int i = 0; i < 5; i++)
        own += rd8(c, A_jst + i) == 0;
      printf("  night %d %s: jewels %d score %d (frame %ld)\n",
             rd32(c, A_night) + 1,
             gs == 5   ? "survived"
             : gs == 7 ? "GAME OVER"
                       : "WON",
             own, rd32(c, A_score), f);
    }
    lastgs = gs;
  }
  if (gs == 7 || gs == 8)
    return -1;
  if (gs == 1 || gs == 5 || gs == 6)
    return (f % 24 < 3) ? 1 : 0;
  if (gs != 2 && gs != 4)
    return 0;
  int px = rd32(c, A_pl) >> 8, py = rd32(c, A_pl + 4) >> 8,
      coins = rd32(c, A_pl + 40), discos = rd32(c, A_pl + 44);
  if (cool)
    cool--;
  // unstick: if we have not moved for a while, wander off in a random direction
  static int lx, ly, still, wander, wkeys;
  if (px == lx && py == ly)
    still++;
  else
    still = 0;
  lx = px;
  ly = py;
  if (still > 25) {
    wander = 40;
    still = 0;
    static const int wk[8] = {16, 16 | 128, 128, 32 | 128,
                              32, 32 | 64,  64,  16 | 64};
    wkeys = wk[rand() & 7];
  }
  if (wander) {
    wander--;
    return wkeys;
  }
  int nd = 1 << 30, nx = 0, ny = 0, td = 1 << 30, tx = 0, ty = 0, close = 0;
  for (int i = 0; i < 12; i++) {
    unsigned a = A_racs + i * 32;
    int st = rd8(c, a);
    if (!st)
      continue;
    int x = rd32(c, a + 16) >> 8, y = rd32(c, a + 20) >> 8;
    int d = (x - px) * (x - px) + (y - py) * (y - py);
    if (st == 9) {
      if (d < td) {
        td = d;
        tx = x;
        ty = y;
      }
    } else if (st >= 1 && st <= 4) {
      if (d < nd) {
        nd = d;
        nx = x;
        ny = y;
      }
      if (d < 70 * 70)
        close++;
    }
  }
  int jd = 1 << 30, gx = 0, gy = 0;
  for (int i = 0; i < 5; i++)
    if (rd8(c, A_jst + i) == 2) {
      int x = rd16(c, A_jx + i * 2), y = rd16(c, A_jy + i * 2);
      int d = (x - px) * (x - px) + (y - py) * (y - py);
      if (d < jd) {
        jd = d;
        gx = x;
        gy = y;
      }
    }
  int pd = 1 << 30, kx = 0, ky = 0;
  for (int i = 0; i < 6; i++) {
    unsigned a = A_picks + i * 12;
    if (!rd8(c, a))
      continue;
    int x = rd32(c, a + 4) >> 8, y = rd32(c, a + 8) >> 8;
    int d = (x - px) * (x - px) + (y - py) * (y - py);
    if (d < pd) {
      pd = d;
      kx = x;
      ky = y;
    }
  }
  int thr = skill ? 95 : 60;
  if (close >= 3 && discos > 0 && f % 8 == 0)
    return 2;
  if (td < (1 << 30)) {
    keys = dirkeys(tx - px, ty - py);
    if (td < thr * thr && coins > 0 && !cool) {
      keys |= 1;
      cool = 18;
    }
    return keys;
  }
  if (jd < (1 << 30) && (nd > 40 * 40 || jd < 30 * 30))
    return dirkeys(gx - px, gy - py);
  if (nd < thr * thr && coins > 0 && !cool && (skill || f % 3 == 0)) {
    cool = skill ? 20 : 45;
    return dirkeys(nx - px, ny - py) | 1;
  }
  if (nd <
      64 * 64) { // run away, with a sideways drift so we don't just hug a wall
    int dx = px - nx, dy = py - ny;
    int sx = (f / 90) % 2 ? 1 : -1;
    return dirkeys(dx * 2 - dy * sx, dy * 2 + dx * sx);
  }
  if (pd < (1 << 30) && coins < 6)
    return dirkeys(kx - px, ky - py);
  if (nd < 120 * 120)
    return dirkeys(px - nx, py - ny);
  return dirkeys(240 - px, 150 - py);
}
int main(int argc, char **argv) {
  mLogSetDefaultLogger(&logger);
  struct mCore *core = mCoreFind(argv[1]);
  if (!core) {
    fprintf(stderr, "no core\n");
    return 1;
  }
  core->init(core);
  mCoreInitConfig(core, NULL);
  unsigned w, h;
  core->desiredVideoDimensions(core, &w, &h);
  unsigned *buf = calloc(w * h, 4);
  core->setVideoBuffer(core, (color_t *)buf, w);
  if (!mCoreLoadFile(core, argv[1])) {
    fprintf(stderr, "load fail\n");
    return 1;
  }
  core->setAudioBufferSize(core, 4096);
  core->reset(core);
  int keys = 0;
  long total = 0;
  for (int i = 2; i < argc; i++) {
    char *c = argv[i];
    if (c[0] == '+')
      keys |= keybit(c + 1);
    else if (c[0] == '-')
      keys &= ~keybit(c + 1);
    else if (!strncmp(c, "shot:", 5))
      save_png(c + 5, buf, w, h, 2);
    else if (!strcmp(c, "snap")) {
      if (nsnap < 64) {
        snaps[nsnap] = malloc(w * h * 4);
        memcpy(snaps[nsnap++], buf, w * h * 4);
      }
    } else if (!strncmp(c, "grid:", 5)) {
      int cols = 3;
      char fn[512];
      sscanf(c + 5, "%d:%511s", &cols, fn);
      save_grid(fn, w, h, cols);
    } else if (!strncmp(c, "rep:", 4)) {
      int n, k;
      sscanf(c + 4, "%d:%d", &n, &k);
      for (int j = 0; j < n; j++) {
        for (int q = 0; q < k; q++) {
          core->setKeys(core, keys);
          core->runFrame(core);
        }
        if (nsnap < 64) {
          snaps[nsnap] = malloc(w * h * 4);
          memcpy(snaps[nsnap++], buf, w * h * 4);
        }
      }
    } else if (!strncmp(c, "sym:", 4)) {
      char nm[64];
      unsigned a;
      if (sscanf(c + 4, "%63[^=]=%x", nm, &a) == 2) {
        if (!strcmp(nm, "pl"))
          A_pl = a;
        else if (!strcmp(nm, "racs"))
          A_racs = a;
        else if (!strcmp(nm, "picks"))
          A_picks = a;
        else if (!strcmp(nm, "jst"))
          A_jst = a;
        else if (!strcmp(nm, "jx"))
          A_jx = a;
        else if (!strcmp(nm, "jy"))
          A_jy = a;
        else if (!strcmp(nm, "gs"))
          A_gs = a;
        else if (!strcmp(nm, "score"))
          A_score = a;
        else if (!strcmp(nm, "night"))
          A_night = a;
      }
    } else if (!strncmp(c, "bot:", 4)) {
      int n, skill = 1, every = 0;
      sscanf(c + 4, "%d:%d:%d", &n, &skill, &every);
      for (int k = 0; k < n; k++) {
        int kk = bot_step(core, total + k, skill);
        if (kk < 0)
          break;
        core->setKeys(core, kk);
        core->runFrame(core);
        if (every && k % every == 0 && nsnap < 64) {
          snaps[nsnap] = malloc(w * h * 4);
          memcpy(snaps[nsnap++], buf, w * h * 4);
        }
      }
      total += n;
    } else if (!strncmp(c, "wav:", 4)) {
      wav_close();
      wavcore = core;
      wavf = fopen(c + 4, "wb");
      unsigned z[11] = {0};
      fwrite(z, 4, 11, wavf);
      wavn = 0;
      blip_set_rates(core->getAudioChannel(core, 0), core->frequency(core),
                     32768);
      blip_set_rates(core->getAudioChannel(core, 1), core->frequency(core),
                     32768);
      blip_clear(core->getAudioChannel(core, 0));
      blip_clear(core->getAudioChannel(core, 1));
    } else if (!strncmp(c, "peek:", 5)) {
      unsigned a;
      int n = 4;
      sscanf(c + 5, "%x:%d", &a, &n);
      printf("%08x:", a);
      for (int k = 0; k < n; k++)
        printf(" %02x", core->busRead8(core, a + k));
      printf("\n");
    } else if (!strncmp(c, "poke:", 5)) {
      unsigned a, v;
      sscanf(c + 5, "%x:%x", &a, &v);
      core->busWrite8(core, a, v);
    } else {
      int n = atoi(c);
      for (int k = 0; k < n; k++) {
        core->setKeys(core, keys);
        core->runFrame(core);
        wav_pump();
      }
      total += n;
    }
  }
  wav_close();
  fprintf(stderr, "ran %ld frames\n", total);
  return 0;
}
