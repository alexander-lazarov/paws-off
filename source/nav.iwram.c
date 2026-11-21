// PAWS OFF! -- path-finding flood fill.
//
// This file is compiled as ARM code and lives in IWRAM (that is what the
// .iwram.c suffix does in the devkitARM build rules): the same loop running
// as Thumb code straight from the cartridge was eating over half a frame.

#include <string.h>
#include <tonc.h>
#include "gfx.h"

//! Breadth-first flood fill over the 16px cell grid.
/*! \param dist	Receives the number of steps from each cell to the nearest seed
   (255: unreachable).
        \param blocked	Non-zero for cells that can't be walked through.
*/
void bfs(u8 *dist, const u8 *blocked, const u16 *seeds, int nseeds) {
  static u16 queue[MAP_W * MAP_H];
  int head = 0, tail = 0, i;

  memset32(dist, 0xFFFFFFFF, MAP_W * MAP_H / 4);
  for (i = 0; i < nseeds; i++) {
    dist[seeds[i]] = 0;
    queue[tail++] = seeds[i];
  }
  while (head < tail) {
    int c = queue[head++];
    int d = dist[c] + 1;
    int cx = c % MAP_W;
    if (d >= 254)
      continue;
    if (cx > 0 && dist[c - 1] == 255 && !blocked[c - 1]) {
      dist[c - 1] = d;
      queue[tail++] = c - 1;
    }
    if (cx < MAP_W - 1 && dist[c + 1] == 255 && !blocked[c + 1]) {
      dist[c + 1] = d;
      queue[tail++] = c + 1;
    }
    if (c >= MAP_W && dist[c - MAP_W] == 255 && !blocked[c - MAP_W]) {
      dist[c - MAP_W] = d;
      queue[tail++] = c - MAP_W;
    }
    if (c < MAP_W * (MAP_H - 1) && dist[c + MAP_W] == 255 &&
        !blocked[c + MAP_W]) {
      dist[c + MAP_W] = d;
      queue[tail++] = c + MAP_W;
    }
  }
}
