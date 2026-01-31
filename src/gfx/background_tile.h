#ifndef background_tile_include_file
#define background_tile_include_file

#ifdef __cplusplus
extern "C" {
#endif

#define background_tile_width 16
#define background_tile_height 16
#define background_tile_size 258
#define background_tile ((gfx_sprite_t*)background_tile_data)
extern unsigned char background_tile_data[258];

#ifdef __cplusplus
}
#endif

#endif
