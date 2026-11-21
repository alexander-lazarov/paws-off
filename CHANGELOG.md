# Changelog

All notable changes to PAWS OFF! are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.0.1] - 2026-11-21

First tagged version: a complete, playable game.

### Added

- Five-night jewel-defence campaign: keep the Duchess's five jewels away from
  the raccoons until dawn; the game ends when all five have left the garden.
- Coin throwing with light homing, missed coins that lure raccoons from the
  ground, and disco balls that make every nearby raccoon dance.
- Strafing (L / R) and pause (START).
- Three kinds of raccoon: regular, fast coin-loving kits, and slow chonks that
  only react to a direct hit or a disco ball. Kits and chonks are
  affine-scaled sprites.
- Raccoon AI with flood-fill path-finding (ARM code in IWRAM), a telegraphed
  pounce, and jewel snatching through the gaps in the hedge.
- Scrolling garden map larger than the screen, with blended shadows and a
  night-to-dawn palette.
- A perk offered by the butler after each night.
- Mode 4 title screen.
- Music and sound effects on the four PSG channels.
- High score saved to SRAM.
- `tools/mkgfx.py`: generates all graphics, the map and the songs from ASCII
  art and note lists, plus PNG previews.
- Headless mGBA test harness (`make harness`) and an auto-player bot
  (`make soak`) used for difficulty tuning and regression checks.

[Unreleased]: https://github.com/alexander-lazarov/paws-off/compare/0.0.1...HEAD
[0.0.1]: https://github.com/alexander-lazarov/paws-off/releases/tag/0.0.1
