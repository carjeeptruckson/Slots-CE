#include <fileioc.h>
#include <graphx.h>
#include <keypadc.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tice.h>

#include "gfx/gfx.h"

// --- Config ---
#define APPVAR_NAME "SLOTSDAT"
#define STARTING_MONEY 100
#define NUM_REELS 3
#define NUM_SYMBOLS 6
#define SYMBOL_H 40
#define REEL_X_START 96
#define REEL_SPACING 47
#define REEL_Y_START 37
#define REEL_WINDOW_H 110

#define COL_TRANSPARENT 0
#define COL_WHITE 1
#define COL_BLACK 2
#define COL_BLUE 5
#define COL_GREEN 142
#define COL_RED 170
#define COL_GOLD 233

const unsigned int bets[] = {5, 10, 25, 50, 100};
#define NUM_BETS (sizeof(bets) / sizeof(bets[0]))

typedef enum {
  SYM_SEVEN = 0,
  SYM_DIAMOND,
  SYM_BELL,
  SYM_BAR,
  SYM_LEMON,
  SYM_CHERRY
} symbol_type_t;

typedef struct {
  float y_offset;
  float speed;
  bool is_spinning;
  int target_index;
} Reel;

typedef struct {
  unsigned int money;
  unsigned long total_spins;
  unsigned long total_wins;
  unsigned long lifetime_earnings; // Total money won
  unsigned int biggest_win;
} game_data_t;

// --- Particles ---
#define MAX_PARTICLES 50
typedef struct {
  float x, y;
  float vx, vy;
  uint8_t color;
  int life;
  bool active;
} Particle;

Particle particles[MAX_PARTICLES];

// --- Globals ---
game_data_t g_data;
Reel reels[NUM_REELS];
gfx_sprite_t *symbol_sprites[NUM_SYMBOLS];
gfx_sprite_t *symbol_sprites[NUM_SYMBOLS];
gfx_sprite_t *bg_sprite;
gfx_sprite_t *bg_tile_sprite;
gfx_sprite_t *stat_button_sprite;
// Buffer for pre-scaled 32x32 tile (width + height + 32*32 bytes = 2 + 1024 =
// 1026)
uint8_t bg_tile_scaled_data[2 + 32 * 32];
gfx_sprite_t *bg_tile_scaled = (gfx_sprite_t *)bg_tile_scaled_data;

// Lights
#define MAX_LIGHTS 60
typedef struct {
  int x;
  int y;
} Point;
Point light_positions[MAX_LIGHTS];
int num_lights = 0;
int light_timer = 0;
int light_state = 0; // 0=Chase, 1=Flash
int light_frame_index = 0;

// Pre-scale helper
void pre_scale_bg_tile(void) {
  if (!bg_tile_sprite)
    return;

  bg_tile_scaled->width = 32;
  bg_tile_scaled->height = 32;

  uint8_t *src = bg_tile_sprite->data;
  uint8_t *dst = bg_tile_scaled->data;

  for (int r = 0; r < 16; r++) {
    for (int c = 0; c < 16; c++) {
      uint8_t color = src[r * 16 + c];
      // 2x2 block in dst
      int dst_r = r * 2;
      int dst_c = c * 2;

      dst[dst_r * 32 + dst_c] = color;
      dst[dst_r * 32 + dst_c + 1] = color;
      dst[(dst_r + 1) * 32 + dst_c] = color;
      dst[(dst_r + 1) * 32 + dst_c + 1] = color;
    }
  }
}

// 151x29 rect at 84,5.
void init_lights(void) {
  num_lights = 0;
  int start_x = 84;
  int start_y = 5;
  int w = 151;
  int h = 29;
  int pitch = 8;

  // Top Row: x from 84 to 84+151 (exclusive)
  for (int x = start_x; x < start_x + w; x += pitch) {
    if (num_lights < MAX_LIGHTS) {
      light_positions[num_lights].x = x;
      light_positions[num_lights].y = start_y;
      num_lights++;
    }
  }

  // Right Col: 3 lights centered vertically
  // Center Y = 5 + 29/2 = 19.
  // Offsets: -8, 0, +8 -> 11, 19, 27.
  // Right X = 84 + 151 - 4 = 231.
  int right_x = start_x + w - 4;
  if (num_lights < MAX_LIGHTS) {
    light_positions[num_lights++] = (Point){right_x, 11};
  }
  if (num_lights < MAX_LIGHTS) {
    light_positions[num_lights++] = (Point){right_x, 19};
  }
  if (num_lights < MAX_LIGHTS) {
    light_positions[num_lights++] = (Point){right_x, 27};
  }

  // Bottom Row: x from right to left
  for (int x = start_x + w - 4 - pitch; x >= start_x; x -= pitch) {
    if (num_lights < MAX_LIGHTS) {
      light_positions[num_lights].x = x;
      light_positions[num_lights].y = start_y + h - 4;
      num_lights++;
    }
  }

  // Left Col: 3 lights centered vertically
  // Y: 27, 19, 11 (bottom up to keep loop order consistent-ish)
  if (num_lights < MAX_LIGHTS) {
    light_positions[num_lights++] = (Point){start_x, 27};
  }
  if (num_lights < MAX_LIGHTS) {
    light_positions[num_lights++] = (Point){start_x, 19};
  }
  if (num_lights < MAX_LIGHTS) {
    light_positions[num_lights++] = (Point){start_x, 11};
  }
}

void draw_lights(void) {
  if (num_lights == 0)
    return;

  // Calculate gap for 3 chains
  int chain_gap = num_lights / 3;
  if (chain_gap == 0)
    chain_gap = 1;

  for (int i = 0; i < num_lights; i++) {
    bool lit = false;
    if (light_state == 0) { // Chase
      // Check against 3 chains
      // We want i to match frame, frame + gap, frame + 2*gap
      // Relative index in the cycle
      int k = light_frame_index;

      // Check each chain head and trail (head, -1, -2)
      for (int c = 0; c < 3; c++) {
        int head = (k + c * chain_gap) % num_lights;
        if (i == head || i == (head + num_lights - 1) % num_lights ||
            i == (head + num_lights - 2) % num_lights) {
          lit = true;
          break;
        }
      }
    } else { // Flash
      // Lit if frame is even (faster toggle)
      if ((light_frame_index / 2) % 2 == 0) {
        lit = true;
      }
    }

    if (lit) {
      gfx_TransparentSprite(light_lit, light_positions[i].x,
                            light_positions[i].y);
    } else {
      gfx_TransparentSprite(light_unlit, light_positions[i].x,
                            light_positions[i].y);
    }
  }
}

void update_lights(void) {
  light_timer++;
  if (light_state == 0) {  // Chase
    if (light_timer > 0) { // Speed
      light_timer = 0;
      light_frame_index++;
    }
  } else { // Flash
    if (light_timer > 2) {
      light_timer = 0;
      light_frame_index++;
    }
  }
}

// Helper to draw 16x16 sprite scaled to 32x32 with transparency
// void draw_scaled_sprite(gfx_sprite_t *sprite, int x, int y) { ... } REMOVED

// Initial set for shuffling
const symbol_type_t initial_reel_strip[NUM_SYMBOLS] = {
    SYM_SEVEN, SYM_DIAMOND, SYM_BELL, SYM_BAR, SYM_LEMON, SYM_CHERRY};

symbol_type_t reel_strips[NUM_REELS][NUM_SYMBOLS];

void shuffle_reels(void) {
  // Use a simple local LCG to ensure randomness works regardless of global rand
  // state
  unsigned long seed = rtc_Time(); // Seeding
  if (seed == 0)
    seed = 0xDEADBEEF; // Fallback

  for (int r = 0; r < NUM_REELS; r++) {
    // 1. Copy initial with an offset to guarantee difference even if shuffle
    // fails
    for (int s = 0; s < NUM_SYMBOLS; s++) {
      int idx = (s + r) % NUM_SYMBOLS; // Offset by reel index
      reel_strips[r][s] = initial_reel_strip[idx];
    }

    // 2. Fisher-Yates Shuffle with custom Random
    for (int i = NUM_SYMBOLS - 1; i > 0; i--) {
      // Custom rand()
      seed = (1103515245 * seed + 12345) & 0x7FFFFFFF;

      int j = seed % (i + 1);
      symbol_type_t temp = reel_strips[r][i];
      reel_strips[r][i] = reel_strips[r][j];
      reel_strips[r][j] = temp;
    }
  }
}

// --- Prototypes ---
void init_gfx(void);
void init_lights(void);
void update_lights(void);
void draw_lights(void);
void shuffle_reels(void);
void load_assets(void);
void pre_scale_bg_tile(void);
bool load_save(void);
void save_game(void);
void draw_ui(unsigned int current_bet, int win_amount);
void draw_stats(void); // New
void draw_reels(void);
void init_particles(void);
void spawn_confetti(void);
void update_particles(void);
void draw_particles(void);
// void draw_scaled_sprite(gfx_sprite_t *sprite, int x, int y);
void logic_spin_reels(void);
int check_win(void);
void draw_stippled_bar(int x, int y, int width, int height);
void draw_text_centered(const char *str, int y, uint8_t fg, uint8_t bg);
unsigned int get_bet_input(void);

// --- Main ---
int main(void) {
  unsigned int current_bet = 5;

  init_gfx();
  init_lights();    // Init
  init_particles(); // Init particles
  shuffle_reels();
  load_assets();
  pre_scale_bg_tile();

  if (!load_save()) {
    g_data.money = STARTING_MONEY;
  }

  // Pity money check at startup
  if (g_data.money == 0)
    g_data.money = 10;

  // Initialize stats if they look garbage (basic sanity check or just trust 0
  // init from load failure) (If load_save fails, implicit 0 init by
  // static/startup code? No, g_data is global so 0 init. If partial read, we
  // might have issues. Let's explicit init if needed, but existing logic
  // handles money.)

  for (int i = 0; i < NUM_REELS; i++) {
    reels[i].y_offset = 0;
    reels[i].is_spinning = false;
    reels[i].speed = 0;
    reels[i].target_index = rand() % NUM_SYMBOLS; // Random initial display
  }

  while (true) {
    // Reset to Chase mode when waiting for input

    current_bet = get_bet_input();
    if (current_bet == 0)
      break;

    g_data.money -= current_bet;
    g_data.total_spins++; // Stat update

    light_state = 0; // Chase during spin

    // Start Spin - begin from current displayed position
    for (int i = 0; i < NUM_REELS; i++) {
      reels[i].y_offset =
          reels[i].target_index * SYMBOL_H; // Start from previous result
      reels[i].is_spinning = true;
      reels[i].speed = 12.0 + (i * 2.0); // Fast spin, smooth start
      reels[i].target_index = rand() % NUM_SYMBOLS;
    }

    bool all_stopped = false;
    int stop_timer = 0;
    int reels_stopped_count = 0;

    while (!all_stopped) {
      stop_timer++;

      update_lights(); // Update animation

      // Rapid stop timing
      if (stop_timer > 30 && reels_stopped_count == 0) {
        reels[0].is_spinning = false;
        reels_stopped_count++;
      }
      if (stop_timer > 50 && reels_stopped_count == 1) {
        reels[1].is_spinning = false;
        reels_stopped_count++;
      }
      if (stop_timer > 70 && reels_stopped_count == 2) {
        reels[2].is_spinning = false;
        reels_stopped_count++;
      }

      logic_spin_reels();

      if (reels_stopped_count == 3) {
        bool settling = false;
        for (int i = 0; i < NUM_REELS; i++)
          if (reels[i].speed > 0)
            settling = true;
        if (!settling)
          all_stopped = true;
      }

      draw_ui(current_bet, 0);
      draw_reels();
      draw_lights(); // Draw lights on top
      gfx_SwapDraw();
    }

    int multiplier = check_win();
    unsigned int payout = current_bet * multiplier;
    g_data.money += payout;

    if (payout > 0) {
      light_state = 1; // Flash on win

      // Stats Update
      g_data.total_wins++;
      g_data.lifetime_earnings += payout;
      if (payout > g_data.biggest_win) {
        g_data.biggest_win = payout;
      }

      spawn_confetti(); // Trigger FX
    }

    // Pity money check after losing spin - show bankrupt popup
    if (g_data.money == 0) {
      g_data.money = 10;

      // Show bankrupt popup
      gfx_SetColor(COL_BLACK);
      gfx_FillRectangle(60, 90, 200, 60);
      gfx_SetColor(COL_RED);
      gfx_Rectangle(60, 90, 200, 60);
      gfx_Rectangle(61, 91, 198, 58);
      draw_text_centered("BANKRUPT!", 100, COL_GOLD, COL_BLACK);
      draw_text_centered("Here's $10 on the house.", 120, COL_WHITE, COL_BLACK);
      gfx_SwapDraw();
      delay(1500); // Show for 1.5 seconds
    }

    save_game();

    if (payout > 0) {
      char buf[32];
      sprintf(buf, "WIN: $%u!", payout);

      // Calculate box dimensions for centered win message
      gfx_SetTextScale(3, 3); // Larger text
      int text_w = gfx_GetStringWidth(buf);
      int text_h = 24; // Approximate height at 3x scale
      int box_w = text_w + 40;
      int box_h = text_h + 30;
      int box_x = (320 - box_w) / 2;
      int box_y = (240 - box_h) / 2;
      int text_x = (320 - text_w) / 2;
      int text_y = box_y + 15;

      // Display win message briefly (flash effect)
      for (int f = 0; f < 6; f++) {
        // Break up the delay to animate lights smoothly
        for (int d = 0; d < 4; d++) {
          update_lights();

          draw_ui(current_bet, (int)payout);
          draw_reels();
          // Draw UI box background
          gfx_SetColor(COL_BLACK);
          gfx_FillRectangle(box_x, box_y, box_w, box_h);
          gfx_SetColor(COL_GOLD);
          gfx_Rectangle(box_x, box_y, box_w, box_h);
          gfx_Rectangle(box_x + 2, box_y + 2, box_w - 4, box_h - 4);

          // Text
          gfx_SetTextScale(3, 3);
          gfx_SetTextBGColor(COL_BLACK);
          gfx_SetTextTransparentColor(COL_BLACK);
          if (f % 2 == 0)
            gfx_SetTextFGColor(COL_GOLD);
          else
            gfx_SetTextFGColor(COL_WHITE);
          gfx_PrintStringXY(buf, text_x, text_y);

          draw_lights(); // Lights on top

          update_particles();
          draw_particles();

          gfx_SwapDraw();
          delay(10);
        }
      }

      // Reset text scale and colors
      gfx_SetTextScale(1, 1);
      gfx_SetTextFGColor(COL_BLACK);
      gfx_SetTextBGColor(COL_TRANSPARENT);
      gfx_SetTextTransparentColor(COL_TRANSPARENT);
    } else {
      delay(400);
    }
  }

  gfx_End();
  return 0;
}

void logic_spin_reels(void) {
  int total_h = NUM_SYMBOLS * SYMBOL_H;
  for (int i = 0; i < NUM_REELS; i++) {
    if (reels[i].is_spinning) {
      reels[i].y_offset += reels[i].speed;
    } else {
      float target_y = reels[i].target_index * SYMBOL_H;
      float current_mod = fmod(reels[i].y_offset, total_h);
      float dist = target_y - current_mod;
      if (dist < 0)
        dist += total_h;

      if (dist < 2.0) {
        reels[i].speed = 0;
        reels[i].y_offset = target_y;
      } else {
        float step = dist * 0.35; // Snappier landing
        // Fix: Cap the stopping speed to the max spin speed so it doesn't
        // "speed up" when stopping from a large distance.
        if (reels[i].speed > 0 && step > reels[i].speed) {
          step = reels[i].speed;
        }
        if (step < 2.0)
          step = 2.0;
        reels[i].y_offset += step;
      }
    }
  }
}

int check_win(void) {
  symbol_type_t r1 = reel_strips[0][reels[0].target_index];
  symbol_type_t r2 = reel_strips[1][reels[1].target_index];
  symbol_type_t r3 = reel_strips[2][reels[2].target_index];

  // 1. THREE OF A KIND
  if (r1 == r2 && r2 == r3) {
    if (r1 == SYM_SEVEN)
      return 50;
    if (r1 == SYM_DIAMOND)
      return 25;
    return 10;
  }

  // 2. ANY TWO OF A KIND (Pays money back or small profit)
  if (r1 == r2 || r2 == r3 || r1 == r3) {
    // Find which symbol is paired
    symbol_type_t pair_sym = (r1 == r2) ? r1 : r2;

    if (pair_sym == SYM_SEVEN)
      return 3; // Profit
    if (pair_sym == SYM_DIAMOND)
      return 2; // Small profit
    return 1;   // Any other pair gives money back
  }

  return 0;
}

void draw_ui(unsigned int current_bet, int win_amount) {
  (void)win_amount; // Fix warning
  // Draw tiled background
  if (bg_tile_sprite) {
    // Fill screen with tiles scaled 2x (32x32) using pre-rendered sprite
    for (int y = 0; y < 240; y += 32) {
      for (int x = 0; x < 320; x += 32) {
        gfx_Sprite(bg_tile_scaled, x, y);
      }
    }
  } else {
    gfx_FillScreen(COL_BLUE); // Fallback
  }

  if (bg_sprite) {
    // Center the 160x240 slot machine background
    // (320 - 160) / 2 = 80
    gfx_TransparentSprite(bg_sprite, 80, 0);
  }

  char buffer[40];
  gfx_SetTextScale(2, 2);
  gfx_SetTextFGColor(COL_WHITE);
  gfx_SetTextBGColor(COL_TRANSPARENT);
  gfx_SetTextTransparentColor(COL_TRANSPARENT);

  // Top Right: Cash
  // Line 1: Label
  const char *cash_label = "Cash:";
  int cash_label_w = gfx_GetStringWidth(cash_label);
  gfx_PrintStringXY(cash_label, 320 - cash_label_w - 5, 5);

  // Line 2: Value
  sprintf(buffer, "$%u", g_data.money);
  int cash_val_w = gfx_GetStringWidth(buffer);
  gfx_PrintStringXY(buffer, 320 - cash_val_w - 5, 25);

  // Top Left: Bet
  if (current_bet > 0) {
    // Line 1: Label
    gfx_PrintStringXY("Bet:", 5, 5);
    // Line 2: Value
    sprintf(buffer, "$%u", current_bet);
    gfx_PrintStringXY(buffer, 5, 25);
  }

  gfx_SetTextScale(1, 1);
  // Reset text colors to defaults
  gfx_SetTextFGColor(COL_BLACK);
  gfx_SetTextBGColor(COL_TRANSPARENT);
  gfx_SetTextTransparentColor(COL_TRANSPARENT);

  // Stat Button
  if (stat_button_sprite) {
    // Right margin: 240 to 320. Width 80.
    // Center of margin: 240 + 40 = 280.
    // Sprite width: 48.
    // X = 280 - 24 = 256.
    // Y: Bottom right. H = 32. Let's put it 5px from bottom.
    // Y = 240 - 32 - 5 = 203.
    gfx_TransparentSprite(stat_button_sprite, 256, 203);
  }
}

void draw_reels(void) {
  // Clipping region for the slot window: 88, 37, 143, 110
  gfx_SetClipRegion(88, REEL_Y_START, 88 + 143, REEL_Y_START + REEL_WINDOW_H);
  int total_h = NUM_SYMBOLS * SYMBOL_H;

  for (int r = 0; r < NUM_REELS; r++) {
    int x_pos = REEL_X_START + (r * REEL_SPACING);
    float shift = fmod(reels[r].y_offset, total_h);

    for (int repeat = -1; repeat <= 1; repeat++) {
      int base_y = REEL_Y_START - (int)shift + (repeat * total_h) +
                   (REEL_WINDOW_H / 2) - (SYMBOL_H / 2);
      for (int s = 0; s < NUM_SYMBOLS; s++) {
        int draw_y = base_y + (s * SYMBOL_H);
        if (draw_y > REEL_Y_START - 32 &&
            draw_y < REEL_Y_START + REEL_WINDOW_H) {
          gfx_TransparentSprite(symbol_sprites[reel_strips[r][s]], x_pos,
                                draw_y);
        }
      }
    }
  }

  gfx_SetClipRegion(0, 0, 320, 240);

  // Draw overlays (stippled black for semi-transparency)
  // X range: 88 to 88+143 = 231
  int ox = 88;
  int ow = 143;

  // Top:
  // 1px Solid Black line at REEL_Y_START (37)
  gfx_SetColor(COL_BLACK);
  gfx_HorizLine(ox, REEL_Y_START, ow);

  // 4px Stippled (38-41)
  draw_stippled_bar(ox, REEL_Y_START + 1, ow, 4);

  // 2px gap (42-43)

  // 2px Stippled (44-45)
  draw_stippled_bar(ox, REEL_Y_START + 7, ow, 2);

  // 1px gap (46)

  // 2px Stippled (47-48)
  draw_stippled_bar(ox, REEL_Y_START + 10, ow, 2);

  // Bottom: Mirrored logic
  // Bottom edge Y = 37 + 110 = 147.
  int bot_y = REEL_Y_START + REEL_WINDOW_H; // 147

  // 1px Solid Black line at very bottom (146) - wait, window is y_start to
  // y_start+h. if height is 110, pixels are 0..109 relative. 37..146 inclusive.
  // So line 147 is outside. Line 146 is the last line.
  gfx_HorizLine(ox, bot_y - 1, ow);

  // 4px Stippled above it (142-145)
  draw_stippled_bar(ox, bot_y - 5, ow, 4);

  // 2px gap (140-141)

  // 2px Stippled (138-139)
  draw_stippled_bar(ox, bot_y - 9, ow, 2);

  // 1px gap (137)

  // 2px Stippled (135-136)
  draw_stippled_bar(ox, bot_y - 12, ow, 2);
}

void draw_stippled_bar(int x, int y, int width, int height) {
  uint8_t *buffer = (uint8_t *)gfx_vbuffer;
  for (int r = 0; r < height; r++) {
    int draw_y = y + r;
    if (draw_y < 0 || draw_y >= 240)
      continue;
    for (int c = 0; c < width; c++) {
      int draw_x = x + c;
      if (draw_x < 0 || draw_x >= 320)
        continue;

      if ((draw_y + draw_x) % 2 == 0) { // Checkerboard
        buffer[draw_y * 320 + draw_x] = COL_BLACK;
      }
    }
  }
}

unsigned int get_bet_input(void) {
  static unsigned int bet = 5;

  // Ensure bet is valid relative to money
  if (g_data.money > 0 && bet > g_data.money) {
    bet = g_data.money;
  }
  if (bet == 0 && g_data.money > 0)
    bet = 5;

  // Main Loop
  while (1) {
    // Pity Check
    if (g_data.money == 0) {
      g_data.money = 10;
      bet = 5; // Reset bet
      draw_ui(0, 0);
      // Simple popup
      gfx_SetColor(COL_BLACK);
      gfx_FillRectangle(60, 90, 200, 60);
      gfx_SetColor(COL_WHITE);
      gfx_Rectangle(60, 90, 200, 60);
      draw_text_centered("BANKRUPT!", 100, COL_GOLD, COL_BLACK);
      draw_text_centered("Here's $10 on the house.", 120, COL_WHITE, COL_BLACK);
      gfx_SwapDraw();
      while (!os_GetCSC())
        ;
    }

    update_lights(); // Update lights while waiting

    draw_ui(bet, 0);
    draw_reels();
    draw_lights(); // Draw lights

    // Persist confetti
    update_particles();
    draw_particles();

    // Control text removed (in background now)

    gfx_SwapDraw();

    kb_Scan();

    // UP: Inc by 5
    if (kb_Data[7] & kb_Up) {
      if (bet + 5 <= g_data.money) {
        bet += 5;
      } else {
        bet = g_data.money; // Cap at max
      }
      delay(100); // Debounce
    }

    // DOWN: Dec by 5
    if (kb_Data[7] & kb_Down) {
      if (bet >= 5) { // Assuming 5 is min increment
        if (bet <= 5)
          bet = 5; // Floor
        else
          bet -= 5;
      }
      delay(100);
    }

    if (kb_Data[6] & kb_Enter) {
      // Final check
      if (bet > g_data.money)
        bet = g_data.money;
      if (bet < 1 && g_data.money > 0)
        bet = 1;
      return bet;
    }

    if (kb_Data[6] & kb_Clear)
      return 0;

    // MODE: Stats (kb_Stat is in kb_Data[4] per keypadc docs)
    if ((kb_Data[1] & kb_Mode) || (kb_Data[4] & kb_Stat)) {
      while ((kb_Data[1] & kb_Mode) || (kb_Data[4] & kb_Stat))
        kb_Scan(); // Wait for release

      while (1) {
        draw_stats();
        gfx_SwapDraw();
        kb_Scan();
        // Exit on Mode, Clear, or Stat
        if ((kb_Data[1] & kb_Mode) || (kb_Data[6] & kb_Clear) ||
            (kb_Data[4] & kb_Stat)) {
          while ((kb_Data[1] & kb_Mode) || (kb_Data[6] & kb_Clear) ||
                 (kb_Data[4] & kb_Stat))
            kb_Scan(); // Wait release
          break;
        }
      }
    }
  }
}

void draw_text_centered(const char *str, int y, uint8_t fg, uint8_t bg) {
  gfx_SetTextFGColor(fg);
  gfx_SetTextBGColor(bg);
  gfx_SetTextTransparentColor(bg == COL_BLACK ? COL_BLACK : 0);
  gfx_PrintStringXY(str, (320 - gfx_GetStringWidth(str)) / 2, y);
}

void init_gfx(void) {
  gfx_Begin();
  gfx_SetPalette(sprite_palette, sizeof_sprite_palette, 0);
  gfx_SetDrawBuffer();
  srand(rtc_Time());
}

void load_assets(void) {
  bg_sprite = slot_machine_bg;
  bg_tile_sprite = background_tile;
  symbol_sprites[SYM_SEVEN] = sym_seven;
  symbol_sprites[SYM_CHERRY] = sym_cherry;
  symbol_sprites[SYM_BELL] = sym_bell;
  symbol_sprites[SYM_BAR] = sym_bar;
  symbol_sprites[SYM_LEMON] = sym_lemon;
  symbol_sprites[SYM_DIAMOND] = sym_diamond;
  stat_button_sprite = stat_button;
}

bool load_save(void) {
  ti_var_t slot = ti_Open(APPVAR_NAME, "r");
  if (!slot)
    return false;
  ti_Read(&g_data, sizeof(game_data_t), 1, slot);
  ti_Close(slot);
  return true;
}

void save_game(void) {
  ti_var_t slot = ti_Open(APPVAR_NAME, "w");
  if (slot) {
    ti_Write(&g_data, sizeof(game_data_t), 1, slot);
    ti_Close(slot);
  }
}

// --- Particles ---
void init_particles(void) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    particles[i].active = false;
  }
}

void spawn_confetti(void) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    particles[i].x = (float)(rand() % 320);
    int rand_offset = rand() % 100; // 0 to 99
    particles[i].y =
        (float)(-50 - rand_offset); // -50 to -149 (fully above screen)
    particles[i].vx = ((float)(rand() % 100) - 50.0f) / 20.0f; // Random drift
    particles[i].vy =
        ((float)(rand() % 100) + 100.0f) / 40.0f; // Fall speed 2.5 to 5
    particles[i].life = 100 + (rand() % 100);
    // Random color from palette (simplification: use indices near predefined
    // ones)
    int c = rand() % 6;
    switch (c) {
    case 0:
      particles[i].color = COL_RED;
      break;
    case 1:
      particles[i].color = COL_GREEN;
      break;
    case 2:
      particles[i].color = COL_BLUE;
      break;
    case 3:
      particles[i].color = COL_GOLD;
      break;
    case 4:
      particles[i].color = COL_WHITE;
      break;
    case 5:
      particles[i].color =
          24; // Pinkish/Purple often near 24 in standard palette or just random
    }
    particles[i].active = true;
  }
}

void update_particles(void) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!particles[i].active)
      continue;

    particles[i].x += particles[i].vx;
    particles[i].y += particles[i].vy;
    particles[i].life--;

    // Gravity/Drag
    particles[i].vy += 0.05; // Gravity
    particles[i].vx *= 0.99; // Air resistance

    if (particles[i].life <= 0 || particles[i].y > 240) {
      particles[i].active = false;
    }
  }
}

void draw_particles(void) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!particles[i].active)
      continue;
    // Only draw particles that are on-screen (y >= 0 and y < 240)
    int py = (int)particles[i].y;
    int px = (int)particles[i].x;
    if (py < 0 || py >= 239 || px < 0 || px >= 319)
      continue;
    gfx_SetColor(particles[i].color);
    gfx_SetPixel(px, py);
    gfx_SetPixel(px + 1, py);
    gfx_SetPixel(px, py + 1);
    gfx_SetPixel(px + 1, py + 1);
  }
}

// --- Stats UI ---
void draw_stats(void) {
  // Simple full screen overlay
  gfx_SetColor(COL_BLUE);
  gfx_FillScreen(COL_BLUE);

  if (bg_tile_sprite) {
    for (int y = 0; y < 240; y += 32) {
      for (int x = 0; x < 320; x += 32) {
        gfx_Sprite(bg_tile_scaled, x, y);
      }
    }
  }

  // Box
  gfx_SetColor(COL_BLACK);
  gfx_FillRectangle(40, 30, 240, 180);
  gfx_SetColor(COL_WHITE);
  gfx_Rectangle(40, 30, 240, 180);

  // Header
  gfx_SetTextScale(2, 2);
  draw_text_centered("STATISTICS", 45, COL_GOLD, COL_BLACK);

  gfx_SetTextScale(1, 1);
  char buf[64];

  int x = 50;
  int y = 80;
  int dy = 15;

  sprintf(buf, "Total Spins: %lu", g_data.total_spins);
  gfx_PrintStringXY(buf, x, y);
  y += dy;

  sprintf(buf, "Total Wins: %lu", g_data.total_wins);
  gfx_PrintStringXY(buf, x, y);
  y += dy;

  sprintf(buf, "Win Rate: %lu%%",
          g_data.total_spins > 0
              ? (g_data.total_wins * 100 / g_data.total_spins)
              : 0);
  gfx_PrintStringXY(buf, x, y);
  y += dy;

  y += 5; // Gap

  sprintf(buf, "Lifetime Earned: $%lu", g_data.lifetime_earnings);
  gfx_PrintStringXY(buf, x, y);
  y += dy;

  sprintf(buf, "Biggest Win: $%u", g_data.biggest_win);
  gfx_PrintStringXY(buf, x, y);
  y += dy;

  // Footer
  draw_text_centered("Press [STAT] to Return", 190, COL_WHITE, COL_BLACK);
}