# PvZ Bot-Engine — Design Document

**Status:** Draft v1.0 — 2026-05-27
**Goal:** Convert PvZ-Portable into a bot-only platform that supports (a) a high-throughput **headless simulation** for training/iterating bot scripts, and (b) a **real-time, watchable** game window driven by the same bot script. Both modes share one engine and one Python API.

---

## 0. Scope summary

| Item | Decision |
| :--- | :--- |
| Game mode | Day Endless (extends `GAMEMODE_SURVIVAL_ENDLESS_STAGE_1`). |
| Lawn | Basic Day lawn (9 cols × 5 rows, no pool/fog/roof). |
| Card set | 8 cards: Peashooter, Sunflower, Cherry Bomb, Wall-nut, Potato Mine, Snow Pea, Chomper, Repeater. |
| Bot language | Python 3.11+. |
| Headless transport | pybind11 module (`pvzbot`), in-process. |
| Real-time transport | Same Python API; engine runs in a sibling process and the bindings call it via a localhost socket. |
| Action API | High-level semantic (`plant`, `use_shovel`, `noop`). No pixel coords. **Sun is auto-collected by the engine — the bot never clicks sun.** |
| Time model | **Bot-driven**: `engine.step()` advances one 10 ms game tick. Real-time mode paces calls to wall-clock; headless runs unpaced. |
| Difficulty | Custom "Day Endless+" curve we author (faster waves, earlier heavy zombies). High ramp — competent bots fail in single-digit flag waves. |
| Zombie set | Day-appropriate subset, **excluding Pogo** (no counter plant in our 8-card deck). See §6.2. |
| Event log | JSONL per run with lifecycle events, bot actions, and engine diagnostics. **No live stream** — bot polls state via API; the log is for post-hoc analysis. |
| UI strip | Skip menus; keep in-game HUD so the run is watchable. Cheat keys, tutorial, store, almanac, Zen Garden, save dialogs all bypassed. |
| Determinism | Best-effort (not a hard requirement). `--seed=<N>` is wired through, but stray `rand()` / `time()` calls in legacy paths are tolerated. No replay guarantee. |
| Reanim refactor | All gameplay timings currently gated by reanimation frame events are refactored to tick-counter-driven logic. See §3.8 and M1.5. |
| Time-warp | Real-time mode supports 1×/2×/4×/8× pacing. |
| Repo layout | Sibling project `./bot-engine/` alongside `./PvZ-Portable/`. Minimal patches to PvZ-Portable. |

---

## 1. Architecture overview

```
                        ┌─────────────────────────┐
                        │       bot script        │   (your code)
                        │       (Python)          │
                        └────────────┬────────────┘
                                     │ pvzbot.* API
                                     │ (semantic actions, state queries)
                       ┌─────────────┴─────────────┐
                       │   pvzbot Python module    │   (pybind11 binding)
                       │   ──────────────────────  │
                       │  Headless mode: calls     │
                       │  engine directly in-proc. │
                       │  Real-time mode: speaks   │
                       │  to engine over socket.   │
                       └─────────────┬─────────────┘
                                     │
            ┌────────────────────────┴────────────────────────┐
            │                                                 │
            ▼ in-process                                      ▼ IPC (TCP/UDS)
┌─────────────────────────┐                       ┌──────────────────────────┐
│ pvz-engine (headless)   │                       │ pvz-engine (real-time)   │
│ ─────────────────────── │                       │ ──────────────────────── │
│ HEADLESS=ON build of    │                       │ HEADLESS=OFF build with  │
│ PvZ-Portable engine.    │                       │ SDL+OpenGL window.       │
│ No SDL, no GL, no audio.│                       │ Bot-driven through same  │
│ Tick-on-demand.         │                       │ AutoBoard hook.          │
└─────────────────────────┘                       └──────────────────────────┘
```

**One engine, two builds.** All gameplay logic — `Board::UpdateGame`, `Zombie::Update`, `Plant::Update`, `Coin`, `Projectile`, wave scheduling, RNG — is the same C++ code in both builds. The only difference is what's stubbed out and how the outer loop is driven.

---

## 2. Repo layout

```
E:\GameDev\PVZ\
├── PvZ-Portable\           # upstream engine. We add patches here.
│   ├── src\Lawn\           # game logic (unchanged in behavior, exposed for binding).
│   ├── src\Bot\            # NEW: AutoBoard, EventLog, SemanticAction, IPC server.
│   └── CMakeLists.txt      # new options: BOT_BUILD, HEADLESS, BUILD_PYBIND.
└── bot-engine\             # NEW sibling project.
    ├── docs\               # design doc, decisions, runbook.
    ├── python\
    │   └── pvzbot\         # pybind11 module sources + pure-Python facade.
    ├── scripts\            # bot strategies (baseline_bot.py, etc.).
    ├── runs\               # JSONL run logs (.gitignored).
    ├── CMakeLists.txt      # builds Python module against PvZ-Portable engine.
    └── README.md
```

The bot-engine project links the PvZ-Portable engine sources directly via `add_subdirectory(../PvZ-Portable)` so we always build against the patched engine.

---

## 3. Engine patches (PvZ-Portable side)

These are the only changes inside `PvZ-Portable/`. Everything else is in `./bot-engine/`.

### 3.1 New CMake options

| Option | Default | Effect |
| :--- | :--- | :--- |
| `BOT_BUILD` | `OFF` | Master switch. When ON, compile the `src/Bot/` directory and the `pvzbot_engine` static library target. |
| `HEADLESS` | `OFF` | When ON, replace SDL2 / OpenGL / SDLMixer dependencies with stubs (see §3.3). Implies `BOT_BUILD=ON`. |
| `BUILD_PYBIND` | `OFF` | When ON, build the `pvzbot` Python module. Implies `BOT_BUILD=ON`. |
| `BOT_RT_IPC` | `OFF` | When ON, compile the real-time IPC server into the desktop build. Implies `BOT_BUILD=ON`. |

A given build is one of:
- `cmake -DHEADLESS=ON -DBUILD_PYBIND=ON` → headless `pvzbot` module.
- `cmake -DBOT_RT_IPC=ON` → real-time desktop game with an IPC server listening for the bot.
- `cmake` (no flags) → unmodified upstream build, still works.

### 3.2 `src/Bot/` files (new)

| File | Responsibility |
| :--- | :--- |
| `AutoBoard.{h,cpp}` | The bot-controlled board lifecycle. Bypasses GameSelector/SeedChooser, builds a Board pre-loaded with the 8-card Day Endless+ setup, exposes single-step ticking. |
| `SemanticAction.{h,cpp}` | Validates and applies bot actions against a Board: `Plant`, `CollectSun`, `UseShovel`, `Noop`. Calls existing `Board::AddPlant`, `Coin::Collect`, etc. so all logic is shared. |
| `Observation.{h,cpp}` | Builds the observation struct returned to the bot (board state, sun, suns-on-screen, zombies, plants, wave/flag info, packet cooldowns). |
| `EventLog.{h,cpp}` | JSONL writer. Hooks into Board/Zombie/Plant/Coin lifecycle to emit lifecycle events; receives action results from `SemanticAction`; writes to `runs/<timestamp>.jsonl`. |
| `DayEndlessPlus.{h,cpp}` | Wave & zombie picker overrides for the custom Day Endless+ curve (§6). |
| `IpcServer.{h,cpp}` | Localhost socket server for real-time mode. Speaks the same wire format as the in-process pybind11 binding. |
| `HeadlessStubs.{h,cpp}` | When `HEADLESS=ON`, provide no-op SDL/GL/audio adapters so the engine compiles and runs without rendering. |
| `PythonBindings.cpp` | pybind11 entry point, only compiled when `BUILD_PYBIND=ON`. |

### 3.3 Headless stubs

The engine touches SDL/GL in a few places. We do **not** rip those out — we route them through a thin adapter:

- SDL window/renderer/event pump → no-op stub returning success.
- OpenGL draw calls → no-op (the engine's `Graphics::Draw*` family). Existing code already separates `Update` from `Draw`; we only stub the drawing side.
- SDL Mixer (audio) → no-op.
- File I/O for `main.pak` / `properties/` → still required *at the moment*, see §8 open questions; headless may need a minimal data path or a "no-assets" mode.
- Reanimation loading → headless skips compiled reanim caches; combat logic uses bounding boxes from `Plant.cpp` / `Zombie.cpp` constants, not reanim frames.

The headless build target is `pvzbot_engine_static` — a static library with `main()` replaced by a `RunSession(seed, config) -> result` entry point.

### 3.4 Skip-the-menus hook

`LawnApp::Start()` currently goes into the title screen. We add an early branch:

```cpp
if (mBotMode) {              // set from --autobot or BOT_BUILD constructor
    mGameMode = GAMEMODE_DAY_ENDLESS_PLUS;
    AutoBoard::Bootstrap(this);   // builds board, skips seed chooser, starts level.
    return;
}
```

`GAMEMODE_DAY_ENDLESS_PLUS` is a new enum value appended at the end of `GameMode` (no renumbering of existing values — preserves save compatibility). It piggy-backs on `GAMEMODE_SURVIVAL_ENDLESS_STAGE_1` for things the engine asks "is this Day Endless?", but routes through `DayEndlessPlus` for wave composition.

### 3.5 Bypassed subsystems (still compiled, never invoked)

These stay in the codebase but `AutoBoard` never enters them:

- `GameSelector`, `SeedChooserScreen`, `AwardScreen`, `CreditScreen`, `ChallengeScreen`, `TitleScreen`.
- `AlmanacDialog`, `StoreScreen`, `ZenGarden`, `CheatDialog`, `NewOptionsDialog`.
- All `TypingCheck` cheat handlers (`mKonamiCheck`, etc.) — `mTodCheatKeys = false` and we never call `DoTypingCheck`.
- Cutscenes (level intro, "Final Wave", award) — `CutScene` is constructed but `StartLevelIntro()` skipped; the engine fires straight into `SCENE_PLAYING`.
- Pause/Save dialogs — disabled.

The in-game HUD (sun counter, seed bank visualization, progress meter, zombies, plants, projectiles) stays so the human can watch.

### 3.6 RNG seeding (best-effort, not deterministic)

Per scope, full determinism is **not** a requirement — we prefer simplicity. We pass `--seed=<N>` through to `mAppRandSeed` and `mBoardRandSeed`, which covers the engine's `MTRand`-based picks (most of wave composition, zombie type weighting, sun spawn jitter). We do **not** commit to byte-identical reruns: stray `rand()` / `std::rand()` / `time(nullptr)` calls in legacy paths are tolerated. The seed is recorded in the event log header for human-readable bisection ("did the engine just have an unlucky wave?") rather than mechanical replay. A future PR can tighten this if it becomes useful.

### 3.7 Sun auto-collection

The bot **does not** click sun. Sun collection is a no-decision — the engine handles it. Concretely:

- When a sky sun, a Sunflower-dropped sun, or a zombie-dropped sun would normally become collectable, the engine calls `Board::AddSunMoney(value)` immediately and emits a `sun_collected` event with `by="engine"`. The bot never sees an uncollected sun.
- For the real-time viewer, the sun coin sprite still spawns and a brief `+25` / `+50` flying-text effect plays toward the counter so the human watcher can see Sunflowers producing. The coin is removed in the same tick it appears — no fall, no click hitbox, no fly-to-bank animation.
- The action API has no `collect_sun`. Sun is never a reason `act()` rejects.
- Sun expiration is moot — no sun is ever left uncollected.

Implementation: short-circuit `Coin::Update()` at the top for sun coins:

```cpp
if (IsSun() && !mDead) {
    mBoard->AddSunMoney(GetSunValue());
    EventLog::Emit({type: "sun_collected", id: this, value: GetSunValue(), by: "engine"});
    mBoard->SpawnFlyingTextEffect(mPosX, mPosY, GetSunValue());  // viewer-only; no-op in headless
    mDead = true;
    return;
}
```

The `Observation.sun` field still reports current bank balance — the bot reads it to decide affordability.

### 3.8 Reanim refactor (tick-driven gameplay)

In vanilla, some gameplay timings are driven by **reanimation frame events** — the reanim system fires named events at specific animation frames, and gameplay code listens for them. We must refactor these to be pure tick-counter-driven, because:

1. **Headless has no reanim playback**, so frame events never fire — gameplay would stall.
2. **Time-warp** (1×/2×/4×/8×) changes anim playback speed if it's tied to render rate; gameplay must remain on the fixed 100 Hz tick.
3. **Real-time bot reaction** needs gameplay timing decoupled from anim-cache load state on slower machines.

Audit targets (full list pinned in M1.5):

- Pole Vaulter — vault trigger frame.
- Newspaper Zombie — paper-rip transition frame.
- Gargantuar — smash impact frame; Imp-throw release frame.
- Zombie eat-bite damage frame (per type).
- Plant attack-anim → projectile spawn frames (Peashooter, Repeater, Snow Pea).
- Chomper — chew-cycle gate.
- Cherry Bomb / Potato Mine — detonation frame.

Refactor pattern: for every gameplay-relevant reanim frame event, introduce an explicit per-entity tick counter (e.g., `mVaultCountdown`, `mNewspaperRipPending`, `mSmashCountdown`, `mChewLockoutTicks`) that fires at the same game-time offset the vanilla anim event would have. The reanim system, when running (real-time mode), becomes pure visualization driven off `mState` + tick counters — never a gameplay trigger.

Validation: a side-by-side test harness runs a fixed bot input against (a) the vanilla unmodified engine and (b) the refactored engine, both at 1× real-time, and diffs the event timeline. Acceptable drift is sub-tick. See M1.5.

---

## 4. Bot Python API

The Python module is `pvzbot`. The bot script imports it, opens a session, then loops:

```python
import pvzbot
from pvzbot import Seed, Card  # enums

sess = pvzbot.Session(
    mode="headless",       # or "realtime"
    seed=12345,
    log_dir="runs/",
    bot_name="my_first_bot",
    # Real-time-only options (ignored in headless):
    speed=1.0,             # 1.0 / 2.0 / 4.0 / 8.0
    engine_host="127.0.0.1", engine_port=7777,
)

while not sess.done:
    obs = sess.observe()               # one observation snapshot (sun is already collected for you)
    for action in my_policy(obs):      # your bot decides — zero or more actions this tick
        sess.act(action)               # returns AcceptResult
    sess.step()                        # advance 1 game tick (10ms game time)

print(sess.result)   # FinalResult: flags_completed, survived_ticks, zombies_killed, etc.
```

### 4.1 Observation (read-only snapshot)

```python
@dataclass(frozen=True)
class Observation:
    tick: int                          # game tick count since level start
    game_time_ms: int                  # tick * 10
    sun: int                           # current sun in bank
    flags_completed: int               # how many flag waves cleared
    current_wave: int                  # 0-indexed total wave
    is_huge_wave: bool                 # current wave is a flag/huge wave
    final_wave: bool                   # last wave in the current flag cycle
    zombie_health_to_next_wave: int    # health remaining before next spawn trigger

    plants: tuple[PlantState, ...]
    zombies: tuple[ZombieState, ...]
    projectiles: tuple[ProjectileState, ...]
    lawn_mowers: tuple[LawnMowerState, ...]

    cards: tuple[CardState, ...]       # 8 entries, in slot order
    shovel_ready: bool                 # always True; the shovel has no cooldown in vanilla

    # Sun is auto-collected by the engine — no SunState list. Use `sun` for current bank,
    # or the event log / `sun_gained_this_tick` if you need finer accounting.
    sun_gained_this_tick: int          # sum of all auto-collected sun this tick

@dataclass(frozen=True)
class PlantState:
    id: int; seed: Seed; row: int; col: int
    hp: int; max_hp: int
    state: PlantStateEnum              # idle / shooting / sleeping / eaten / ...

@dataclass(frozen=True)
class ZombieState:
    id: int; type: Zombie; row: int
    x: float                            # pixel x; bot can also use col_float = x/80
    hp: int; max_hp: int
    has_armor: bool; armor_hp: int      # cone / bucket / screen-door
    is_eating: bool; chilled: bool; frozen: bool
    speed: float                        # current pixels/tick

@dataclass(frozen=True)
class CardState:
    seed: Seed
    cost: int
    cooldown_ms_remaining: int         # 0 == ready
    can_afford: bool
    ready: bool                        # cost met AND cooldown == 0
```

`obs` is a value-type snapshot, not a view — safe to keep across ticks for diffs.

### 4.2 Actions (semantic, instant-effect)

```python
class Action:                          # tagged union
    @staticmethod
    def plant(seed: Seed, row: int, col: int) -> 'Action': ...
    @staticmethod
    def use_shovel(row: int, col: int) -> 'Action': ...
    @staticmethod
    def noop() -> 'Action': ...
```

There is no `collect_sun` — sun is auto-collected by the engine (§3.7).

`sess.act(action)` returns:

```python
@dataclass(frozen=True)
class AcceptResult:
    accepted: bool
    reason: str        # "ok", "not_enough_sun", "card_on_cooldown",
                       # "occupied", "invalid_terrain", "card_not_in_deck",
                       # "out_of_bounds", "no_plant_to_shovel", ...
```

**Multiple actions per tick are allowed** (e.g. plant 2 plants and shovel 1 in the same tick). Each `act()` is processed in order against the current Board state; subsequent actions see the effects of earlier ones immediately.

**"Instant" semantics** (the user's "simplified clicking"):
- `plant` → engine validates cost/cooldown/placement, calls `Board::AddPlant` directly (no `CursorObject` involvement), deducts sun, starts the seed packet cooldown. Plant appears on the board immediately and fires on its normal in-game schedule.
- `use_shovel` → engine validates and removes the plant immediately; no shovel-cursor animation.

Validation rules match the human game exactly (so a bot can't stack two plants on one tile, can't plant Snow Pea on a Lily Pad slot — there are none on Day lawn anyway). Failure returns `accepted=False` with a reason; the engine **does not** auto-deduct sun on rejected plants.

### 4.3 Tick API

```python
sess.step()                            # advance exactly 1 tick (10 ms game time)
sess.step(n=10)                        # advance 10 ticks; no actions during these
sess.step_until(predicate)             # advance until predicate(obs) is True or game ends
                                       # (capped by sess.config.max_step_until_ticks)
```

In **headless** mode, `step()` returns as fast as the CPU can run the game logic.
In **real-time** mode, `step()` blocks until the wall-clock has caught up to the next tick boundary (modulated by `speed`).

### 4.4 Session lifecycle

```python
sess.observe()       # snapshot current state (cheap; produced from Board fields).
sess.act(action)     # apply 1 action immediately; can be called many times per tick.
sess.step(n=1)       # advance time.
sess.done            # True once the level ended (won/lost/quit).
sess.result          # FinalResult: BoardResult, flags_completed, survived_ticks,
                     #              zombies_killed_by_type, plants_planted_by_type,
                     #              total_sun_collected, etc.
sess.close()         # writes the log footer, closes IPC if any. Context-manager-safe.
```

### 4.5 Convenience helpers (pure Python on top)

To keep the bot script ergonomic:

```python
pvzbot.helpers.nearest_zombie(obs, row)           # → ZombieState | None
pvzbot.helpers.zombies_in_row(obs, row)           # → list[ZombieState] sorted by x desc
pvzbot.helpers.empty_squares(obs)                 # → list[(row, col)]
pvzbot.helpers.can_plant(obs, seed, row, col)     # → (bool, reason)
pvzbot.helpers.iter_ready_cards(obs)              # → Iterator[CardState]
```

These don't touch the engine; they're convenience reducers over the observation. Sun helpers are unnecessary because sun is auto-collected.

---

## 5. Real-time bridge & viewer

### 5.1 Architecture

The real-time game is the **exact same engine** with `SDL` + `OpenGL` enabled and `IpcServer` listening on a localhost socket.

```
$ ./bot-engine/scripts/baseline_bot.py --seed=42 --mode=realtime --speed=2
```

The bot script process:
1. Spawns the `pvz-portable --autobot --ipc-port=7777 --seed=42` child process.
2. Waits for the engine's "ready" handshake on the socket.
3. Runs the same `Session`/`observe()`/`act()`/`step()` loop. The Python module's real-time backend marshals each call as a JSON line over the socket.

The engine's main thread runs the SDL event pump and renders the game window normally. A second thread reads commands from the socket and applies them at safe points in the game loop:

- `observe` is read-only and can fire any time.
- `act` is queued and executed at the start of the next tick (so the bot can't tear into the middle of an `Update()` call).
- `step` in real-time mode is **advisory**, not authoritative — the engine ticks itself on a wall clock. `step()` returns when the engine has advanced the requested number of ticks past the moment the call arrived.

### 5.2 Time-warp

`speed=N` multiplies the engine's tick rate from 100 Hz to `100×N` Hz. The renderer still draws at the display refresh; the game just simulates faster. Implemented by adjusting `SexyAppBase::mUpdateMultiplier` (the framework already has this knob; see §1.2).

### 5.3 Watching

You launch:
```
> python bot-engine/scripts/baseline_bot.py --mode=realtime --speed=2 --seed=42
```

A normal PvZ window opens, shows the Day Endless+ lawn with the 8-card bank, sun counter ticking, and the bot starts playing visibly. Plants appear instantly (no cursor / no placement animation). Sun coins disappear into the counter on collection (no fly-to-bank animation — they just snap and add).

### 5.4 IPC wire format

JSON-Lines over TCP (configurable port; default 7777, loopback only). Same shape as the in-process API:

```
{"cmd":"observe","id":17}
{"cmd":"act","id":18,"action":{"type":"plant","seed":"PEASHOOTER","row":2,"col":3}}
{"cmd":"step","id":19,"n":1}
```

Responses use the same `id` for matching. Engine pushes log records over a second stream (or just to disk; see §7).

---

## 6. Day Endless+ mode

### 6.1 Lawn

- 9 columns × 5 rows.
- All ground (no pool, no roof slopes).
- 5 lawn mowers (one per row), original behavior.
- No graves, no fog, no flowerpots, no Zen Garden.

### 6.2 Allowed zombie set

| Zombie | Allowed | Notes |
| :--- | :--- | :--- |
| Regular | ✅ | |
| Flag | ✅ | Marks flag waves. |
| Conehead | ✅ | |
| Pole Vaulter | ✅ | |
| Buckethead | ✅ | |
| Newspaper | ✅ | |
| Screen-Door | ✅ | |
| Football | ✅ | |
| Dancing | ✅ | Brings backup dancers. |
| Backup Dancer | ✅ | (spawned alongside Dancing). |
| Pogo | ❌ | **Excluded** — the 8-card deck has no Tall-nut / Magnet-shroom / Spikeweed counter. Pogo bypasses every plant we own except via Cherry Bomb / Potato Mine sniping, which is an unfair matchup. |
| Gargantuar | ✅ | Counter: Cherry Bomb. |
| Imp | ✅ | (thrown by Gargantuar; not from waves directly until late.) Counter: Chomper, Wall-nut, Potato Mine. |
| Ducky Tube | ❌ | Pool-only. |
| Snorkel | ❌ | Pool-only. |
| Dolphin Rider | ❌ | Pool-only. |
| Balloon | ❌ | Fog-only. |
| Digger | ❌ | Fog-only. |
| Jack-in-the-box | ❌ | Fog-only by reference. |
| Zomboni | ❌ | Roof-themed in vanilla; off-theme for Day. |
| Bobsled | ❌ | Requires Zomboni. |
| Catapult | ❌ | Roof-only. |
| Ladder | ❌ | Roof-only. |
| Bungee | ❌ | Roof-only in vanilla; flag-wave drop only when enabled. |
| Yeti | ❌ | Special spawn, off-theme. |

Enforced in `DayEndlessPlus::PickZombieType()` by overriding `Board::PickZombieType()`'s allow-list.

### 6.3 Wave & difficulty curve

The vanilla Survival Endless curve (in `Board::InitZombieWavesForLevel` / `PickZombieType`) ramps `aZombiePoints` (per-wave point budget) by flag, but it's tuned around Pool zombies and stretches over many flags. We override:

- **Wave interval**: starts at 350 ticks (3.5 s) between intra-flag waves, decays to 200 ticks (2 s) by flag 8. (Vanilla: ~600 ticks.)
- **Wave-points budget**: starts at 4 (flag 1 wave 1), grows quadratically. Formula in §6.4. Flag waves (every 10th wave) get a 2.5× multiplier.
- **Zombie unlocks** (earliest flag a type may appear):
    - flag 1: Regular, Flag, Conehead.
    - flag 2: Pole Vaulter, Buckethead.
    - flag 3: Newspaper, Screen-Door.
    - flag 4: Football.
    - flag 5: Dancing (+ Backup Dancer).
    - flag 6: Gargantuar (Imps via Gargantuar throw).
- **Gargantuar density** ramps fast after flag 6 — by flag 9 a flag wave has 3+ Gargantuars on a 5-row lawn.
- **Special spawn rule**: no zombie type is ever "spawned-only" (we don't introduce out-of-set types via cutscenes).

Target outcome: a *competent* bot using all 8 cards with optimal sun management is expected to fail somewhere between flag 5 and flag 12. A "spam Peashooter + Sunflower" baseline should die at flag 3. This calibration will be empirical — we test with the baseline bot and adjust constants.

### 6.4 Wave-points formula

```
points(flag, wave_in_flag) = round( 4 + (flag - 1) * 6 + flag^2 * 0.8 + wave_in_flag * 1.5 )
flag_wave_bonus            = 2.5x
huge_wave_zombie_min       = 5      # min zombies in a flag wave
```

Plus the existing engine logic for `mZombieHealthToNextWave` (drives spawn pacing). We hand-pick zombie type weights per flag rather than the engine's default "weighted random across allowed types" so we can guarantee variety.

### 6.5 Seed bank

Fixed 8-card bank, no chooser screen, no upgrades:

| Slot | Seed | Cost | Cooldown |
| :--- | :--- | :--- | :--- |
| 1 | Sunflower | 50 | 7.5 s |
| 2 | Peashooter | 100 | 7.5 s |
| 3 | Snow Pea | 175 | 7.5 s |
| 4 | Repeater | 200 | 7.5 s |
| 5 | Wall-nut | 50 | 30 s |
| 6 | Potato Mine | 25 | 30 s |
| 7 | Cherry Bomb | 150 | 50 s |
| 8 | Chomper | 150 | 7.5 s |

(Values come from the engine's existing `Plant::GetCost` / `Plant::GetRefreshTime`. We do not change them — we only fix the slot set.)

### 6.6 Starting state

- 50 sun. (Same as the engine's `INITIAL_SUN_AMOUNT` for chooser-skipping levels.)
- All 8 cards on cooldown=0 ready to use.
- No tutorial.
- Mowers armed.

### 6.7 Win/lose conditions

- **Lose**: any zombie reaches the house (existing `Board::ZombiesWon`). Endless = no "win" — but the level ends and the bot's score is `flags_completed`.
- The session never "wins"; `result.outcome` is always `BOARDRESULT_LOST` (or `QUIT` if the bot calls `sess.quit()`).
- The interesting metric is `flags_completed` (survival depth) plus secondary metrics (sun efficiency, zombies killed by type).

---

## 7. Event log

### 7.1 Location & format

- One file per session: `bot-engine/runs/<UTC-timestamp>_<bot-name>_seed<N>.jsonl`.
- One JSON object per line, UTF-8, newline-terminated.
- First line is a `session_start` header with version, seed, bot name, mode, full config.
- Last line is a `session_end` footer with final result and aggregate stats.

### 7.2 Event taxonomy

**Lifecycle events** (emitted from C++ engine hooks):

| Event | Fields |
| :--- | :--- |
| `wave_started` | wave, flag, is_huge, points_budget, zombie_composition |
| `flag_raised` | flag (now completed), tick |
| `zombie_spawned` | id, type, row, hp |
| `zombie_killed` | id, type, row, x, killed_by ("plant_id"/"projectile"/"mower"/"instakill") |
| `zombie_ate_plant` | zombie_id, plant_id, plant_seed, row, col |
| `plant_planted` | plant_id, seed, row, col, cost_paid, by ("bot"/"engine") |
| `plant_died` | plant_id, seed, row, col, cause ("eaten"/"shoveled"/"exploded"/"timed_out") |
| `sun_appeared` | id, value, x, y, source ("sky"/"sunflower"/"zombie"/"start") |
| `sun_collected` | id, value, by ("engine" — always; bot doesn't collect sun) |
| `projectile_fired` | id, type, from_plant_id, row, x |
| `mower_triggered` | row |
| `game_over` | result, flags_completed, survived_ticks |

**Bot-action events**:

| Event | Fields |
| :--- | :--- |
| `action_requested` | tick, action, accepted, reason |

**Engine-diagnostic events** (rarer, debug-grade):

| Event | Fields |
| :--- | :--- |
| `rng_seeded` | seed (header only) |
| `wave_pick` | wave, picked_zombies (full type list), points_used, points_budget |
| `spawn_tick_decision` | wave, mZombieHealthToNextWave, will_advance |

Per-tick state snapshots are **not** written by default (would balloon the log) but are toggleable: `Session(snapshot_every_n_ticks=100)` writes a `state_snapshot` event with the full observation.

### 7.3 No live event stream

Per decision, the engine writes the log to disk only. The bot script gets state via `sess.observe()`, which returns the same data the engine has internally. Bots that want event-driven logic compute deltas between consecutive observations (helpers provided).

### 7.4 Post-run analysis

A small Python helper ships with `pvzbot`:

```python
from pvzbot.analysis import RunLog
log = RunLog("runs/2026-05-27T15-12-04_baseline_seed42.jsonl")
log.summary()                                 # quick stats
log.timeline_dataframe()                      # pandas DataFrame of events
log.zombies_killed_by_type()
log.sun_curve()                               # sun over time
log.plant_lifespans()
```

---

## 8. Open questions / investigation items

These are things I want to confirm before writing the code, *not* things we'll guess at. Each is small enough to be answered with a short audit of the existing engine.

1. **Can headless run without `main.pak`?** ★ Spike 1 completed 2026-05-27. **Findings:**
    - With `HEADLESS=ON` + the bot entry point taking over `main()`, the engine compiles and links cleanly using `platform/headless/` no-op stubs.
    - `SexyAppBase::Init()` requires exactly **one** patch (already applied): `#ifndef PVZ_HEADLESS`-gate the `if (mGLInterface == nullptr) { FATAL... }` block at `SexyAppBase.cpp:3416`. Without that gate, init aborts after `MakeWindow()` because the headless platform stub never creates a GL context.
    - With that gate in place, the engine progresses into `LawnApp::Init()` and reaches line 1274: `mResourceManager->ParseResourcesFile("properties/resources.xml")`. The file is missing → `ResourceManager::Fail` → `LawnApp::ShowResourceError(true)` → `DoExit(0)` → process exits.
    - `properties/resources.xml` is the manifest of every image / sound / font / reanim the engine will load via `main.pak`. It ships only as part of the licensed assets — no copy exists in the open-source repo.
    - **Conclusion:** the engine cannot bootstrap past `LawnApp::Init` without the licensed pak. Two viable paths from here:
        - **Path A (BYO pak):** User points us at their legally owned `main.pak` + `properties/` (the same files needed by the vanilla game). Headless build loads them just like vanilla, then runs the game logic without rendering/audio output.
        - **Path B (Spike 2, pak-free):** Bypass `LawnApp::Init` entirely. Bot constructs a `Board` directly, sets the minimum required fields, and ticks `Board::UpdateGame`. Requires gating every `IMAGE_*` / `SOUND_*` / `REANIM_*` dereference in gameplay code and authoring a small `numeric_tables.json` for plant/zombie HP, damage, cost, cooldown. Estimate: 3-5 days of code surgery. Pays off in CI cleanliness (no licensed assets ever needed) and avoids any "you must own the game" friction.

    **RESOLVED 2026-05-27 — Path A chosen and working.** The user supplied a legal *Plants vs. Zombies GOTY Edition 1.2.0.1096* (Steam) copy. The headless engine now boots `LawnApp::Init()` to completion (`mShutdown=false`, `mLoadingFailed=false`, exit 0; it compiles all reanims and loads all image bits into RAM). The working recipe:
      1. Headless platform stubs (`platform/headless/{Window,Input}.cpp`) — no SDL window, no event pump.
      2. **Headless `MakeWindow()` constructs a `GLInterface` but never calls `Init()`.** The constructor is GL-free and gives a valid `mCritSect`, so the image registry (`AddMemoryImage`/`AddGLImage`, which lock `mGLInterface->mCritSect`) works. GPU texture upload is lazy (draw-time only) and headless never draws. Net result: **zero patches to engine-core `.cpp` files** — the earlier `SexyAppBase::Init` FATAL-gate was reverted once the interface became non-null.
      3. Point at the licensed assets with `-resdir="<game folder>"`; `SexyAppBase::Init` mounts `main.pak`, and `properties/resources.xml` resolves from inside the pak.
    - **Path B** remains a possible future cleanup (no licensed assets in CI) but is not needed for the project to proceed.
2. **Gargantuar Imp throw on a Day lawn**: the existing engine code throws Imps even in Day modes. Confirm trajectories and landings work correctly when there's no pool (they should). Verify in M2 testing.
3. **Real-time mode bot reaction time**: at 1× the bot has 10 ms per tick of bot logic. Python can comfortably do this in-process, but the **real-time IPC** round-trip (TCP loopback JSON) needs to stay under ~5 ms p99 or we'll desync. Pre-validate in M6 with a no-op echo benchmark.
4. **Sun visual in real-time**: confirmed plan is "spawn coin sprite, immediately credit, play `+25` flying-text, kill coin same tick." Verify this reads as legible to a human watching at 2×–4× time-warp. Tweak text duration if it strobes.
5. **(downgraded) RNG audit**: nice-to-have, not required. If we ever want byte-identical replay, a half-day audit of stray `rand()` calls will deliver it. Tracking as a future enhancement, not blocking.

---

## 9. Milestones

### M0 — Survey & decision lock (done)
- Read engine. Pick decisions. Write this doc.

### M1 — Engine seams + headless build (2-3 days)
- Add `BOT_BUILD`, `HEADLESS`, `BUILD_PYBIND` CMake options.
- Stub SDL/GL/audio for headless.
- Resolve §8 Q1 (Spike 1 + Spike 2): decide pak-needed vs. pak-free headless boot.
- Build the headless engine binary that runs a hard-coded session (no bot yet, just ticks).
- Verify it runs the existing Day Endless mode without crashing for ≥1 flag worth of game time.

### M1.5 — Reanim refactor (3-5 days)
- Audit `Zombie.cpp`, `Plant.cpp`, `Reanimation*.cpp`, and any `ListenerCallback` / `mTrackShowing` references for reanim-frame-event-driven gameplay.
- Pin the full list of gameplay-relevant reanim events (Pole Vaulter, Newspaper, Gargantuar, Imp, eat-bite, Peashooter/Repeater/Snow Pea shoots, Chomper chew lockout, Cherry Bomb / Potato Mine detonation, …).
- For each: introduce a tick counter on the entity, fire gameplay at the same game-time offset, and demote the reanim event to a viewer-only trigger.
- Side-by-side test harness vs. vanilla engine at 1× speed; confirm sub-tick drift on a recorded input.

### M2 — AutoBoard + semantic actions + sun auto-collect (2-3 days)
- Implement `AutoBoard::Bootstrap` to skip menus and start Day Endless+ with the 8-card bank.
- Implement `SemanticAction::Plant / UseShovel / Noop`.
- Wire to existing `Board::AddPlant`, etc. (no `CursorObject` involvement).
- Implement sun auto-collection (§3.7): short-circuit `Coin::Update()` for sun coins; spawn flying-text in real-time viewer.
- Manual test from C++ test harness: tick + plant, observe state, verify sun ticks up automatically.

### M3 — Day Endless+ curve (1-2 days)
- Implement `DayEndlessPlus` wave & zombie picker (no Pogo).
- Empirically tune the wave-points formula against a hand-played run from C++ test driver.

### M4 — Observation + event log (1-2 days)
- Implement `Observation` struct and `EventLog` JSONL writer.
- Hook engine lifecycle points to emit events.

### M5 — Python binding (1-2 days)
- pybind11 module `pvzbot`.
- Pure-Python `Session` facade with observe/act/step.
- Baseline bot script: "plant Sunflower in back two columns, then Peashooter, then Snow Pea, then Repeater. Cherry Bomb on huge waves. Wall-nut at column 8."
- Run end-to-end in headless. Save log. Run log analyzer.

### M6 — Real-time IPC + viewer (2-3 days)
- IPC server in engine. Same wire format as Python module.
- Real-time backend in Python session.
- Time-warp toggle (1× / 2× / 4× / 8×).
- End-to-end: `python baseline_bot.py --mode=realtime` opens a game window and plays visibly with sun auto-crediting to the counter.

### M7 — Polish + tuning (ongoing)
- Calibrate difficulty so the baseline bot fails at flag ~3 and the "good" bot fails at flag ~10.
- Run-log analysis helpers (`pvzbot.analysis.RunLog`).
- A second bot script demonstrating a different strategy (e.g., "all-in mines + Cherry Bomb economy").

Total estimate: **~3-4 weeks** of focused work — M1.5 (reanim refactor) is the new long pole.

---

## 10. Non-goals (explicit)

- Night/Pool/Fog/Roof modes.
- Plant upgrades (Twin Sunflower, Gatling Pea, etc.).
- Almanac unlocks, Zen Garden, Crazy Dave's shop, achievements.
- Mobile/console/WASM targets (only Windows/Linux/macOS for the bot work).
- Saving mid-level state to `.v4` files (run logs are the source of truth).
- **Deterministic / byte-identical replay.** Best-effort RNG seeding only; no action-recording / replay mode.
- **Bot sun management.** Sun is auto-collected; the bot has no `collect_sun` action and no incentive to think about sun beyond reading the current bank balance.
- Pogo Zombie in the benchmark (no counter in our deck).
- Network multiplayer.
- Modding API beyond the bot script itself.
- Hot-reload of the bot script (process restart is fine).
- A GUI for the bot — bots are Python files.

---

## 11. Risks

| Risk | Likelihood | Impact | Mitigation |
| :--- | :--- | :--- | :--- |
| Headless build can't run without licensed `main.pak` | Medium | High | §8 Q1 spike in M1. Fallback: extract numeric tables at first run from the user's own pak; cache locally. |
| Reanim refactor (M1.5) uncovers more gameplay-gating events than expected | Medium | High | Triage by zombie/plant type and time-box. Tick-driven gates can be added incrementally — only the 8 cards + Day zombie set must be complete before M2. |
| Difficulty calibration takes many iterations | High | Low | Expose curve constants as JSON config; tune without recompile. |
| pybind11 GIL contention in real-time bridge | Low | Medium | Bot is the only Python caller; engine thread does work; release GIL around `step()`. |
| Engine assumes mouse/keyboard input in places we missed | Medium | Low | First real-time integration run will surface these; fix as found. |
| Sun auto-collect reads as "engine cheated" because plants pay sun cost while a visible coin still flashes | Low | Low | Tunable flying-text duration; we can even hide the coin sprite entirely if the visual is jarring. |

---

## 12. Decisions not yet made (for explicit follow-up)

- Whether to expose plant-internal cooldown counters (e.g., Peashooter shoot timer) in the observation. Default: **no** — too low-level; bots should infer from projectile events. Reconsider after the first baseline bot.
- Sun-coin visual in real-time viewer: keep the brief coin sprite + flying-text effect, **or** skip the coin entirely and only show the flying-text. Default: **keep brief coin** for visual continuity with vanilla. Easy toggle.
- Run log compression (gzip on close). Default: **no** — keeps `tail -f` and `jq` working trivially; add later if size becomes an issue.

---

## 13. Definition of done

We are done when **all** of these hold:

1. `cmake --build` with `-DHEADLESS=ON -DBUILD_PYBIND=ON` produces a working `pvzbot` Python module.
2. `python bot-engine/scripts/baseline_bot.py --seed=1` runs a Day Endless+ session in headless mode, writes a JSONL log, and exits cleanly with `flags_completed >= 1`.
3. Sun is **auto-collected** throughout the run — no `sun_appeared` event is ever followed by a `sun_collected` event with anything other than `by="engine"`, and no bot action mentions sun.
4. `python bot-engine/scripts/baseline_bot.py --mode=realtime --speed=2 --seed=1` opens the PvZ-Portable game window, plays Day Endless+ visibly with the same 8-card bank, the bot's plants appear instantly when it acts, and sun snaps into the counter without any click.
5. **No Pogo Zombie** ever appears in the run log.
6. The vanilla `pvz-portable` build (no bot flags) still works as upstream — we don't break the original game.
7. A second bot script (e.g., naive "all sunflowers") demonstrably reaches a different `flags_completed` from the baseline, proving the framework can differentiate strategies.
8. The reanim refactor side-by-side test (M1.5) shows sub-tick drift versus the vanilla engine on a fixed input.
