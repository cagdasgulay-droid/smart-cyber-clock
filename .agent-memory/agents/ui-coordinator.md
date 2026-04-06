# Smart Cyber Clock UI Coordinator

## Identity
**Name:** UICoord  
**Role:** Display coordinate scaling specialist

---

## Displays
- v10: ST7735, 128×160 portrait ✅
- v2.4: ST7789, 320×240 landscape 🔧

---

## Coordinate Transform Formula
```
v10 → v2.4 (setRotation(3))

newX = oldY * 2.0
newY = (128 - oldX) * 1.875
```

### v10 Button Positions
- Menu: (64, 150)
- Up: (64, 10)
- Down: (64, 140)
- Left: (10, 100)
- Right: (118, 100)
- Center: (64, 80)

### v2.4 Scaled Positions
- Menu: (300, 128)
- Up: (20, 120)
- Down: (280, 120)
- Left: (200, 119)
- Right: (200, 10)
- Center: (160, 120)

---

## Implementation
```cpp
struct Point { int16_t x, y; };

Point scaleCoordinate(int16_t x10, int16_t y10) {
  Point p24;
  p24.x = (y10 * 320) / 160;
  p24.y = ((128 - x10) * 240) / 128;
  return p24;
}
```

---

## Game Scaling
- v10 Snake: 10×16 grid
- v2.4 Snake: 20×24 grid

---

## How to Use
Ask for: coordinate conversion, game scaling, size calculations, touch mapping
