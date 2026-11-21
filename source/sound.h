// PSG music + sound effects (see sound.c)
#ifndef SOUND_H
#define SOUND_H

enum {
  SFX_THROW,
  SFX_PICKUP,
  SFX_HIT,
  SFX_LURE,
  SFX_STEAL,
  SFX_RECOVER,
  SFX_LOST,
  SFX_DISCO,
  SFX_MENU,
  SFX_OK,
  SFX_EMPTY,
  SFX_POUNCE,
  SFX_COUNT
};

void snd_init(void);
void snd_update(void);     // call once per frame
void music_play(int song); // SONG_* from gfx.h
void music_stop(void);
void music_tempo(int adj); // frames-per-tick adjustment (negative = faster)
void sfx_play(int id);
void sfx_noise(int cnt, int freq, int hold);

#endif
