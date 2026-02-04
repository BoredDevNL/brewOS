#include "minesweeper.h"
#include "graphics.h"
#include "wm.h"
#include <stdbool.h>
#include <stddef.h>

Window win_minesweeper;

// Game constants
#define GRID_WIDTH 10
#define GRID_HEIGHT 10
#define MINE_COUNT 10
#define CELL_SIZE 20

// Game state
static int grid[GRID_HEIGHT][GRID_WIDTH];  // -1 = mine, 0-8 = adjacent mine count
static bool revealed[GRID_HEIGHT][GRID_WIDTH];
static bool flagged[GRID_HEIGHT][GRID_WIDTH];
static bool game_over = false;
static bool game_won = false;
static int revealed_count = 0;

// Helper: Random number generator (simple LCG)
static uint32_t random_seed = 12345;
static uint32_t random_next(void) {
    random_seed = random_seed * 1103515245 + 12345;
    return (random_seed / 65536) % 32768;
}

static void init_game(void) {
    // Clear arrays
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            grid[y][x] = 0;
            revealed[y][x] = false;
            flagged[y][x] = false;
        }
    }
    
    // Place mines randomly
    int mines_placed = 0;
    while (mines_placed < MINE_COUNT) {
        int x = random_next() % GRID_WIDTH;
        int y = random_next() % GRID_HEIGHT;
        
        if (grid[y][x] != -1) {
            grid[y][x] = -1;
            mines_placed++;
        }
    }
    
    // Calculate adjacent mine counts
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            if (grid[y][x] != -1) {
                int count = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int ny = y + dy;
                        int nx = x + dx;
                        if (ny >= 0 && ny < GRID_HEIGHT && nx >= 0 && nx < GRID_WIDTH) {
                            if (grid[ny][nx] == -1) count++;
                        }
                    }
                }
                grid[y][x] = count;
            }
        }
    }
    
    game_over = false;
    game_won = false;
    revealed_count = 0;
}

static void reveal_cell(int x, int y);

// Flood fill for empty cells
static void flood_fill(int x, int y) {
    if (x < 0 || x >= GRID_WIDTH || y < 0 || y >= GRID_HEIGHT) return;
    if (revealed[y][x] || flagged[y][x]) return;
    if (grid[y][x] == -1) return;
    
    revealed[y][x] = true;
    revealed_count++;
    
    // If cell is empty, reveal adjacent cells
    if (grid[y][x] == 0) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                flood_fill(x + dx, y + dy);
            }
        }
    }
}

static void reveal_cell(int x, int y) {
    if (x < 0 || x >= GRID_WIDTH || y < 0 || y >= GRID_HEIGHT) return;
    if (revealed[y][x] || flagged[y][x]) return;
    
    if (grid[y][x] == -1) {
        // Hit a mine - game over
        game_over = true;
        // Reveal all mines
        for (int yy = 0; yy < GRID_HEIGHT; yy++) {
            for (int xx = 0; xx < GRID_WIDTH; xx++) {
                if (grid[yy][xx] == -1) {
                    revealed[yy][xx] = true;
                }
            }
        }
    } else if (grid[y][x] == 0) {
        // Empty cell - flood fill
        flood_fill(x, y);
    } else {
        // Numbered cell
        revealed[y][x] = true;
        revealed_count++;
    }
    
    // Check win condition
    if (revealed_count == (GRID_WIDTH * GRID_HEIGHT - MINE_COUNT)) {
        game_won = true;
    }
}

static void flag_cell(int x, int y) {
    if (x < 0 || x >= GRID_WIDTH || y < 0 || y >= GRID_HEIGHT) return;
    if (revealed[y][x]) return;
    
    flagged[y][x] = !flagged[y][x];
}

static void minesweeper_right_click(Window *win, int x, int y) {
    // x and y are relative to window content (0,0 is top-left of window)
    int grid_start_x = 10;
    int grid_start_y = 50;
    
    // Check grid cells
    if (x >= grid_start_x && x < grid_start_x + GRID_WIDTH * CELL_SIZE &&
        y >= grid_start_y && y < grid_start_y + GRID_HEIGHT * CELL_SIZE) {
        
        if (game_over || game_won) return;
        
        int cell_x = (x - grid_start_x) / CELL_SIZE;
        int cell_y = (y - grid_start_y) / CELL_SIZE;
        
        flag_cell(cell_x, cell_y);
        wm_paint();
    }
}

static void minesweeper_paint(Window *win) {
    // Background
    draw_rect(win->x + 4, win->y + 24, win->w - 8, win->h - 28, COLOR_LTGRAY);
    
    // Game status
    if (game_over) {
        draw_string(win->x + 10, win->y + 30, "Game Over!", COLOR_RED);
    } else if (game_won) {
        draw_string(win->x + 10, win->y + 30, "You Won!", COLOR_BLUE);
    } else {
        draw_string(win->x + 10, win->y + 30, "", COLOR_BLACK);
    }
    
    // Draw grid
    int grid_start_x = win->x + 10;
    int grid_start_y = win->y + 50;
    
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            int px = grid_start_x + x * CELL_SIZE;
            int py = grid_start_y + y * CELL_SIZE;
            
            if (revealed[y][x]) {
                // Revealed cell - sunken
                draw_bevel_rect(px, py, CELL_SIZE, CELL_SIZE, true);
                
                if (grid[y][x] == -1) {
                    // Mine
                    draw_string(px + 8, py + 6, "*", COLOR_RED);
                } else if (grid[y][x] > 0) {
                    // Number
                    char num[2] = { '0' + grid[y][x], 0 };
                    draw_string(px + 8, py + 6, num, COLOR_BLACK);
                }
                // 0 = empty, nothing to draw
            } else {
                // Unrevealed cell - raised
                draw_bevel_rect(px, py, CELL_SIZE, CELL_SIZE, false);
                
                if (flagged[y][x]) {
                    draw_string(px + 7, py + 6, "F", COLOR_RED);
                }
            }
        }
    }
    
    // Draw new game button
    int btn_y = grid_start_y + GRID_HEIGHT * CELL_SIZE + 10;
    draw_button(grid_start_x, btn_y, 90, 24, "New Game", false);
}

static void minesweeper_click(Window *win, int x, int y) {
    // x and y are relative to window content (0,0 is top-left of window)
    int grid_start_x = 10;
    int grid_start_y = 50;
    int btn_y = grid_start_y + GRID_HEIGHT * CELL_SIZE + 10;
    
    // Check "New Game" button
    if (x >= grid_start_x && x < grid_start_x + 90 &&
        y >= btn_y && y < btn_y + 24) {
        init_game();
        wm_paint();
        return;
    }
    
    // Check grid cells
    if (x >= grid_start_x && x < grid_start_x + GRID_WIDTH * CELL_SIZE &&
        y >= grid_start_y && y < grid_start_y + GRID_HEIGHT * CELL_SIZE) {
        
        if (game_over || game_won) return;
        
        int cell_x = (x - grid_start_x) / CELL_SIZE;
        int cell_y = (y - grid_start_y) / CELL_SIZE;
        
        reveal_cell(cell_x, cell_y);
        
        wm_paint();
    }
}

void minesweeper_init(void) {
    win_minesweeper.title = "Minesweeper";
    win_minesweeper.x = 250;
    win_minesweeper.y = 100;
    win_minesweeper.w = 240;
    win_minesweeper.h = 340;
    win_minesweeper.visible = false;
    win_minesweeper.focused = false;
    win_minesweeper.z_index = 0;
    win_minesweeper.paint = minesweeper_paint;
    win_minesweeper.handle_click = minesweeper_click;
    win_minesweeper.handle_right_click = minesweeper_right_click;
    
    // Initialize game
    init_game();
}
