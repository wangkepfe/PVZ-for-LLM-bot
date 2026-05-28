/*
 * Copyright (C) 2026 PvZ Bot-Engine contributors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * AutoBoard implementation. See AutoBoard.h and design doc §3.4 / §6.
 */

#include "AutoBoard.h"

#ifdef PVZ_BOT_BUILD

#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/SeedPacket.h"
#include "ConstEnums.h"

namespace pvzbot
{

// The fixed Day Endless+ 8-card bank (design §6.5), in slot order.
static const SeedType kBotCards[8] = {
	SeedType::SEED_SUNFLOWER,
	SeedType::SEED_PEASHOOTER,
	SeedType::SEED_SNOWPEA,
	SeedType::SEED_REPEATER,
	SeedType::SEED_WALLNUT,
	SeedType::SEED_POTATOMINE,
	SeedType::SEED_CHERRYBOMB,
	SeedType::SEED_CHOMPER,
};

AutoBoard::AutoBoard(LawnApp* theApp)
	: mApp(theApp)
	, mTickCount(0)
{
}

Board* AutoBoard::GetBoard() const
{
	return mApp->mBoard;
}

void AutoBoard::Bootstrap()
{
	mTickCount = 0;

	// Day Endless. We piggy-back on the engine's existing Survival-Day-Endless
	// mode for board geometry / wave machinery; the custom Day Endless+ curve
	// (M3) will override the zombie picker later.
	mApp->mGameMode = GameMode::GAMEMODE_SURVIVAL_ENDLESS_STAGE_1;
	mApp->mBoardResult = BoardResult::BOARDRESULT_NONE;

	// Same as LawnApp::NewGame(), minus ShowSeedChooserScreen() and the
	// level-intro cutscene.
	mApp->MakeNewBoard();
	Board* aBoard = mApp->mBoard;
	aBoard->InitLevel();

	SetupSeedBank();

	// Drop straight into play (the engine itself forces SCENE_PLAYING this way
	// when loading a mid-level save, see SaveGame.cpp).
	mApp->mGameScene = GameScenes::SCENE_PLAYING;
	aBoard->StartLevel();
}

void AutoBoard::SetupSeedBank()
{
	Board* aBoard = mApp->mBoard;
	SeedBank* aBank = aBoard->mSeedBank;

	aBank->mNumPackets = 8;
	for (int i = 0; i < 8; ++i)
	{
		SeedPacket* aPacket = &aBank->mSeedPackets[i];
		aPacket->mIndex = i;
		aPacket->mX = aBoard->GetSeedPacketPositionX(i);
		aPacket->mY = 8;
		aPacket->SetPacketType(kBotCards[i]);
	}
}

void AutoBoard::Tick(int theTicks)
{
	for (int i = 0; i < theTicks; ++i)
	{
		if (IsGameOver())
			break;
		// Board is a Widget; Board::Update() is exactly what the
		// WidgetManager invokes each frame, and it calls UpdateGame()
		// (sun spawn, zombie spawn, all game objects) internally.
		mApp->mBoard->Update();
		mTickCount++;
	}
}

bool AutoBoard::IsGameOver() const
{
	if (mApp->mBoard == nullptr)
		return true;
	if (mApp->mGameScene == GameScenes::SCENE_ZOMBIES_WON)
		return true;
	return mApp->mBoardResult != BoardResult::BOARDRESULT_NONE;
}

}  // namespace pvzbot

#endif  // PVZ_BOT_BUILD
