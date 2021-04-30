PIO VGA Pico Demo
-------------------

This is the code described in my blogs:
https://gregchadwick.co.uk/blog/playing-with-the-pico-pt5/ and
https://gregchadwick.co.uk/blog/playing-with-the-pico-pt6/. Read that for full
details.

The C source files present are:

* `pio_vga_first.c` and `video_first.pio` - Testing video signal timing using
  a PIO logic analyzer, doesn't generate an actual video signal
* `pio_vga_second.c` and `video_second.pio` - Generates a 320x240 video test
  pattern
* `pio_vga_third.c` - Generates a moving RGB test pattern
* `pio_vga_fourth.c` - Draws animated character sprites on the test pattern in
  `pio_vga_third.c`
* `pio_vga_fifth.c` - Draws animated, moving characters over a tilemap
  background and 'bounces' around scrolling the tilemap with the sprites.

Additional files are:

* `sprite_data.h` - Header containing sprite data used in `pio_vga_fourth.c` and
  `pio_vga_fifth.c`
* `test_tilemap.h` and `village_tileset.h` - Headers containing tilemap and
  tileset data used in `pio_vga_fifth.c`
* `char_sheet.png` - Graphics used to generate `sprite_data.h`
* `make_sprite_data.py` - Python3 program (requires Pillow) to generate
  `sprite_data.h` from `char_sheet.png`
* `test_map.csv` - CSV containing the test map. Each line gives the tile indexes
  for a particular row.
* `make_tilemap_data.py` - Python3 program (requires Pillow) to generate
  `test_tilemap.h` and `village_tileset.h` from `test_map.csv` and
  `village_tiles.png`. Produces a map preview `map_render.png`

Sprite art is by Charles Gabriel from 
https://opengameart.org/content/twelve-16x18-rpg-sprites-plus-base, licensed
under CC-BY 3.0 (https://creativecommons.org/licenses/by/3.0/)

Tilemap art is by LimeZu from https://limezu.itch.io/serenevillagerevamped,
licensed under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/)
