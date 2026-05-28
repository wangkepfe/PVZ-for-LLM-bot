/*
 * Copyright (C) 2026 PvZ Bot-Engine contributors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * AutoBoard — bot-controlled level lifecycle. Bypasses the GameSelector,
 * SeedChooserScreen, and level-intro cutscene, builds a Day Endless board
 * pre-loaded with the fixed 8-card bank, and exposes single-tick advancement.
 *
 * See bot-engine/docs/DESIGN.md §3.4 / §6.
 */

#pragma once

#include "BotConfig.h"

#ifdef PVZ_BOT_BUILD

#include "../ConstEnums.h"   // SeedType

class LawnApp;
class Board;

namespace pvzbot
{

// Result of a semantic action. `reason` is a stable short string ("ok",
// "not_enough_sun", "card_on_cooldown", "occupied", ...). See design §4.2.
struct ActionResult
{
	bool			accepted;
	const char*		reason;
};

class AutoBoard
{
public:
	explicit AutoBoard(LawnApp* theApp);

	// Start a fresh Day Endless level with the fixed 8-card bank and drop
	// straight into SCENE_PLAYING (no chooser, no intro cutscene).
	void			Bootstrap();

	// Advance the simulation by `theTicks` game ticks (10 ms each), driving
	// Board::Update() exactly as the WidgetManager would each frame.
	void			Tick(int theTicks = 1);

	// True once the level has ended (zombies won, or board result set).
	bool			IsGameOver() const;

	// ---- Semantic actions (instant-effect, validated). row 0-4, col 0-8. ----
	// plant: validates deck membership, cooldown, sun cost, and placement;
	// on success deducts sun, places the plant, and starts the card cooldown.
	ActionResult	TryPlant(SeedType theSeed, int theRow, int theCol);
	// shovel: removes the top plant at (row,col).
	ActionResult	TryShovel(int theRow, int theCol);

	// ---- Lightweight queries (full Observation comes in M4) ----
	int				Sun() const;
	int				FindPacketIndex(SeedType theSeed) const;  // -1 if absent
	bool			IsCardReady(SeedType theSeed) const;
	int				CardCooldownTicksRemaining(SeedType theSeed) const;

	LawnApp*		App() const { return mApp; }
	Board*			GetBoard() const;
	long long		TickCount() const { return mTickCount; }

private:
	void			SetupSeedBank();   // install the fixed 8 cards

	LawnApp*		mApp;
	long long		mTickCount;
};

}  // namespace pvzbot

#endif  // PVZ_BOT_BUILD
