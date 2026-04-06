# Contributing to Arduino LCD Game Console

First off — thank you for even considering it. This project started as a personal challenge, and every contribution, idea, or bug report makes it better.

---

## Ways to Contribute

- **Add a new game** — the most welcome contribution
- **Improve an existing game** — better mechanics, difficulty curve, sprites
- **Fix a bug** — open an issue first if you're unsure
- **Improve documentation** — README, wiring diagrams, comments

---

## Before You Start

Read this carefully. The hardware constraints are real and non-negotiable.

### Hardware Limits

| Resource | Available | Notes |
|:---:|:---:|---|
| SRAM | 2 KB | The biggest constraint — watch your globals |
| Flash | 32 KB | ~26 KB used, ~6 KB remaining |
| CGRAM slots | 8 | Shared across the engine — 4 per game max |
| LCD | 16×2 chars | No graphics mode, no pixel access |

### Code Rules (avr-gcc compatibility)

These are hard rules, not style preferences. Breaking them will cause compile errors on Arduino Nano:

```
✅ Flat C-style functions
✅ Global arrays and primitive types
✅ Static local variables inside functions
✅ Enums for state machines

❌ No structs
❌ No reference parameters (bool &x, int &y)
❌ No lambdas or closures
❌ No std:: containers (vector, map, etc.)
❌ No dynamic memory allocation (new, malloc)
```

### No `delay()` in Gameplay

`delay()` freezes the loop. Any `delay()` inside a tick block makes `lastTick` go stale — causing an immediate extra tick on resume, which breaks collision detection and input timing.

**Rule:** After every `delay()` call inside a tick block, reset the timer:
```cpp
delay(80);
lastTick = millis(); // ← always do this
```

---

## Adding a New Game

### 1. Check EEPROM Map First

| Address | Game |
|:---:|---|
| `0–1` | Endless Runner |
| `2–3` | Snake |
| `4–7` | Reserved |
| `8–9` | Reaction Game |
| `10–11` | Whack-a-Mole |
| `12–13` | Memory Game |
| `14–15` | **Your game here** |

Pick the next available 2-byte pair and document it.

### 2. Game Function Template

Every game lives in a single self-contained function. Follow this structure:

```cpp
// ═══════════════════════════════════════════════════
// GAME N — YOUR GAME NAME
// ═══════════════════════════════════════════════════
// Sprites (4 max per game — CGRAM has 8 slots total)
byte mySprite[8] = { ... };

// Game-specific globals (keep minimal)
static int my_score;
static bool my_gameOver;

void runMyGame(){
  // 1. Load sprites
  lcd.createChar(0, mySprite);
  bInit(); bClr();

  // 2. Show tutorial
  // ...
  engineCountdown();

  // 3. Init game state
  my_score = 0;
  my_gameOver = false;
  btnReset();

  // 4. Game loop
  unsigned long lastTick = millis();
  int spd = 300; // ms per tick

  while(!my_gameOver){
    // Read input
    if(firePressed()) { /* ... */ }
    if(leftPressed()) return; // back to menu

    // Tick
    if(millis() - lastTick >= (unsigned long)spd){
      lastTick = millis();

      // Game logic here

      // Render
      bClr();
      // bSp(), bCh() calls
      bFlush();
      flashUpdate();
    }
  }

  // 5. Game over
  engineGameOver(my_score, eeRead(MY_EE_ADDR), MY_EE_ADDR);
}
```

### 3. Register in the Main Menu

In `showMainMenu()`, add your game to the list:

```cpp
const char* GAME_NAMES[6] = {   // ← increment count
  "Endless Runner  ",
  "Snake           ",
  "Reaction Game   ",
  "Whack-a-Mole    ",
  "Memory Game     ",
  "Your Game       "            // ← add here (exactly 16 chars)
};
```

And add the case in the switch block:
```cpp
case 5: runMyGame(); break;
```

### 4. Add a Standalone File

Put a standalone version of your game in the `games/` folder as `your_game_name.ino`. This makes it easier to test and debug independently.

---

## Engine Reference

These functions are available to all games — don't reimplement them.

### Display

```cpp
bClr();              // clear current frame buffer
bCh(col, row, ch);  // write ASCII char to buffer
bSp(col, row, idx); // write custom sprite (CGRAM slot 0-7)
bFlush();            // write changed cells to LCD
bInit();             // reset buffer (force full redraw next flush)
```

### Buttons

```cpp
upPressed()          // true once per press (rising edge, 250ms debounce)
downPressed()
leftPressed()
firePressed()
anyPressed()         // true if any button is held
readAnyButton()      // returns 0-3 or -1
btnReset()           // clear all button states (call before game loop)
```

### LED Flash

```cpp
flashNB(pin, ms)     // non-blocking LED flash
flashUpdate()        // call every loop iteration to expire flash
ledsOff()            // immediately turn off all LEDs
```

### EEPROM

```cpp
eeRead(addr)         // read 2-byte int from addr
eeWrite(addr, val)   // write 2-byte int (only if val > current)
```

### Shared Animations

```cpp
engineCountdown()    // shows 3-2-1 GO! with tones
engineGameOver(score, best, eeAddr)  // full game over screen + new record celebration
```

---

## Pull Request Checklist

Before submitting:

- [ ] Compiles without warnings on Arduino Nano (ATmega328P)
- [ ] No structs, no reference parameters, no lambdas
- [ ] No `delay()` inside tick blocks without `lastTick = millis()` reset after
- [ ] SRAM usage checked — add globals only if truly necessary
- [ ] EEPROM address documented and not overlapping with existing games
- [ ] Game added to main menu with correct 16-char name
- [ ] Standalone `.ino` added to `games/` folder
- [ ] Tested on real hardware or Wokwi simulation
- [ ] README updated if new game changes hardware requirements

---

## Reporting Bugs

Open an issue and include:

- Which game
- What happened vs. what you expected
- Your score / situation when it happened
- Hardware: original Arduino Nano or clone (CH340/CH341)?

---

## Questions?

Open an issue or reach out on LinkedIn — link in the README.

Happy building. 🛠️
