# UI Coordinate Extraction Report
## Smart Cyber Clock v9.1 - ESP32 with ST7735 1.8" Display

**Current Display:** 128×160 pixels (PORTRAIT)  
**Target Display:** 320×240 pixels  
**Scaling:** X×2.0, Y×1.5

---

## Summary of Findings

### File Analyzed
- **Path:** `/Users/cagdasgulay/Desktop/CYBER CLOCK/1.8 ekran/1_8_smart_cyber_clock_v9_1/1_8_smart_cyber_clock_v9_1.ino`
- **Total coordinates extracted:** 200+
- **Total UI screens mapped:** 12 major screens + submenus
- **Game modes covered:** 6 games (Snake, Tetris, Breakout, Flappy Bird, Pong, 2048)

---

## Key Coordinate Constants

### Grid System (Clock/Monitor Screen)
```c
#define GRID_L        4       // Left edge
#define GRID_R        156     // Right edge
#define GRID_TOP      56      // Top section
#define GRID_MID      80      // Middle divider
#define GRID_BOT      104     // Bottom edge
#define GRID_MID_X    80      // Vertical center
#define BAR_Y         110     // Air quality bar Y
#define BAR_W         38      // Bar width (calculated)
#define BAR_H         6       // Bar height
```

### Game Dimensions

#### Snake (Portrait)
- Board: 20×16 cells @ 6px each
- Area: (20-139, 14-109)

#### Tetris (Landscape)
- Board: 10×20 cells @ 6px each
- Right panel starts at X=67

#### Breakout (Landscape)
- Game area: 130×128px
- Right panel: X=132-159

#### Flappy Bird (Landscape)
- Game area: 130×128px
- Bird fixed X: 25
- Pipe width: 12px

#### Pong (Landscape)
- Game area: 130×128px
- Paddles: 3×22 px
- Ball: 3×3 px

#### 2048 (Landscape)
- Grid: 4×4 cells @ 28px each
- Right panel: X=120-159

---

## File Structure

```
Main Modes:
├── MODE_CLOCK       → Monitor/environmental display
├── MODE_MENU        → Main menu (scrolling)
├── MODE_POMODORO    → Timer with circular progress
├── MODE_ALARM       → Time setting + ringing
├── MODE_GAMES_MENU  → Game selection
├── MODE_GAME        → Snake (portrait)
├── MODE_TETRIS      → Tetris (landscape)
├── MODE_BREAKOUT    → Breakout (landscape)
├── MODE_FLAPPY      → Flappy Bird (landscape)
├── MODE_PONG        → Pong (landscape)
├── MODE_2048        → 2048 (landscape)
├── MODE_LED         → Edison LED control
├── MODE_SETTINGS    → Settings menu
├── MODE_HOME        → Smart home (HA control)
└── MODE_SLEEP       → Screen off

Landscape modes use rotation(1) → 160×128
Portrait modes use rotation(0) → 128×160
```

---

## Drawing Functions & Their Positions

### initClockStaticUI()
- Static background grid and labels
- Called once, then dynamic values update on it

### drawClockTime()
- Large time display (HH:MM:SS) @ Y=18, centered
- Called every second

### drawEnvDynamic()
- Sensor values in grid quadrants
- Humidity: left-top, Temperature: right-top
- VOC: left-bottom, CO2: right-bottom + indicator

### drawMenu() / drawGamesMenu()
- Scrolling lists (max 7 items visible)
- Item spacing: 15px (menu) or 16px (games)
- Highlight box: X=6-154, height varies

### drawPomodoroScreen()
- Selection mode: time input (MM:SS format)
- Running mode: circular progress arc @ (80,64), radius 55-44

### drawAlarmScreen()
- Time display HH:MM @ centered Y=30, size=3
- Status display @ Y=84

### drawLedScreen()
- Mode list (6 items) + brightness control

### drawSettingsScreen()
- 5 settings items with editable values

### drawHomeScreen()
- 3 levels: rooms → devices → brightness control
- Item spacing: 14px (rooms) or 16px (devices)

---

## Game Board Layouts

### Snake (Portrait 128×160)
```
[0,0].........................[127,0]
                        SNAKE
[0,14].....GAME BOARD.....[139,109]
                        BORDER

(Score top-left: 0-80, 0-12)
(Title top-right: 80-127)
```

### Tetris/Breakout/Flappy/Pong (Landscape 160×128)
```
[0,0]......................[159,0]
|                          |
| GAME AREA (130×128)     | PANEL
| or GRID (4x4)           | (30×128)
|                          |
[0,127]...................[159,127]

Left border often @ X=0 or dynamic
Right panel @ X=120-159 (2048) or X=132-159 (others)
```

---

## Scaling to 320×240

### Multiply All Coordinates
- **X:** coordinate × 2.0
- **Y:** coordinate × 1.5
- **Width:** width × 2.0
- **Height:** height × 1.5
- **Font size:** multiply by 1.5 (or use Adafruit_GFX scaling)

### Example: Clock Grid (Portrait 128×160 → 320×240)
```
Original:
GRID_L = 4        →  8
GRID_R = 156      →  312
GRID_TOP = 56     →  84
GRID_MID = 80     →  120
GRID_BOT = 104    →  156
GRID_MID_X = 80   →  160

BAR_Y = 110       →  165
BAR_H = 6         →  9
BAR_W = 38        →  76
```

### Example: Tetris in Landscape (160×128 → 320×192)
```
Becomes 320×192 landscape (still fits in 320×240)

TET_X = 1         →  2
TET_Y = 4         →  6
TET_BLOCK = 6     →  12
Board: 10×20 cells → still 10×20 (at 12px/cell)

Right panel X=67  →  134
Title Y=4         →  6
Controls Y=100    →  150
```

---

## Important Notes

### Portrait vs Landscape
- **Portrait games:** Snake only (MODE_GAME)
- **Landscape games:** Tetris, Breakout, Flappy, Pong, 2048
- Rotation is handled by `tft.setRotation(1)` in game init functions
- Always reset to portrait for menus with `tft.setRotation(0)`

### Centered Text
All centered text uses the formula:
```c
x = (width - text_width) / 2
```
- For portrait 128px width: x = (128 - text_width) / 2
- For landscape 160px width: x = (160 - text_width) / 2
- For 320px width: x = (320 - text_width) / 2

### Dynamic Coordinates
Some positions are calculated dynamically:
- Menu item Y = 20 + i × 15 (scrolling offset applied)
- Grid centered positions = (GRID_L + GRID_MID_X) / 2
- Circular progress in Pomodoro uses trigonometry (PI constant, cos/sin)

### Text Sizes
```c
// Adafruit_GFX default font
tft.setTextSize(1)  // 5×7 pixels per character
tft.setTextSize(2)  // 10×14 pixels
tft.setTextSize(3)  // 15×21 pixels (used for clock time)
```

---

## Testing Recommendations

1. **Proportional Scaling:** Ensure X×2.0, Y×1.5 ratio maintained
2. **Game Boundaries:** Verify game play areas don't exceed 320×240
3. **Landscape Transitions:** Test tft.setRotation() calls
4. **Text Rendering:** Check text centering with larger font scales
5. **Touch Boundaries:** Adjust encoder/button response zones if using touch
6. **Performance:** Monitor frame rates in games (especially Pong/Tetris)

---

## Related Files
- **Main coordinate file:** `UI_Coordinate_Mapping_128x160_to_320x240.txt` (548 lines, detailed)
- **Source code:** `/Users/cagdasgulay/Desktop/CYBER CLOCK/1.8 ekran/1_8_smart_cyber_clock_v9_1/1_8_smart_cyber_clock_v9_1.ino`

---

Generated: 2026-04-04  
Total content lines: 548 coordinates + this summary
