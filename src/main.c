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
#define COL_GOLD 229

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
} game_data_t;

// --- Globals ---
game_data_t g_data;
Reel reels[NUM_REELS];
gfx_sprite_t *symbol_sprites[NUM_SYMBOLS];
gfx_sprite_t *symbol_sprites[NUM_SYMBOLS];
gfx_sprite_t *bg_sprite;
gfx_sprite_t *bg_tile_sprite;
// Buffer for pre-scaled 32x32 tile (width + height + 32*32 bytes = 2 + 1024 =
// 1026)
uint8_t bg_tile_scaled_data[2 + 32 * 32];
gfx_sprite_t *bg_tile_scaled = (gfx_sprite_t *)bg_tile_scaled_data;

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
void shuffle_reels(void);
void load_assets(void);
void pre_scale_bg_tile(void);
bool load_save(void);
void save_game(void);
void draw_ui(unsigned int current_bet, int win_amount);
void draw_reels(void);
void draw_reels(void);
// void draw_scaled_sprite(gfx_sprite_t *sprite, int x, int y);
void logic_spin_reels(void);
void logic_spin_reels(void);
int check_win(void);
void draw_text_centered(const char *str, int y, uint8_t fg, uint8_t bg);
unsigned int get_bet_input(void);

// --- Main ---
int main(void) {
  unsigned int current_bet = 5;

  init_gfx();
  shuffle_reels();
  load_assets();
  pre_scale_bg_tile();

  if (!load_save()) {
    g_data.money = STARTING_MONEY;
  }

  // Pity money check at startup
  if (g_data.money == 0)
    g_data.money = 10;

  for (int i = 0; i < NUM_REELS; i++) {
    reels[i].y_offset = 0;
    reels[i].is_spinning = false;
    reels[i].speed = 0;
    reels[i].target_index = rand() % NUM_SYMBOLS; // Random initial display
  }

  while (true) {
    current_bet = get_bet_input();
    if (current_bet == 0)
      break;

    g_data.money -= current_bet;

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
      gfx_SwapDraw();
    }

    int multiplier = check_win();
    unsigned int payout = current_bet * multiplier;
    g_data.money += payout;

    // Pity money check after losing spin
    if (g_data.money == 0)
      g_data.money = 10;

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
      for (int f = 0; f < 12; f++) {
        draw_ui(current_bet, (int)payout);
        draw_reels();

        // Draw UI box background
        gfx_SetColor(COL_BLACK);
        gfx_FillRectangle(box_x, box_y, box_w, box_h);
        gfx_SetColor(COL_GOLD);
        gfx_Rectangle(box_x, box_y, box_w, box_h);
        gfx_Rectangle(box_x + 2, box_y + 2, box_w - 4, box_h - 4);

        // Draw text with flashing colors
        gfx_SetTextScale(3, 3);
        gfx_SetTextBGColor(COL_BLACK);
        gfx_SetTextTransparentColor(COL_BLACK);
        if (f % 2 == 0)
          gfx_SetTextFGColor(COL_GOLD);
        else
          gfx_SetTextFGColor(COL_WHITE);
        gfx_PrintStringXY(buf, text_x, text_y);

        gfx_SwapDraw();
        delay(150);
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
  gfx_SetTextFGColor(COL_BLACK);
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
  // Red horizontal line removed
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

    draw_ui(bet, 0);
    draw_reels();

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