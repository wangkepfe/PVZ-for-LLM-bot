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

class LawnApp;
class Board;

namespace pvzbot
{

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
