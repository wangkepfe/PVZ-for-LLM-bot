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

	// From now on, sun is auto-collected by the engine (the bot never clicks).
	gBotAutoCollectSun = true;

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

// ---------------------------------------------------------------------------
// Semantic actions
// ---------------------------------------------------------------------------

int AutoBoard::Sun() const
{
	return mApp->mBoard ? mApp->mBoard->mSunMoney : 0;
}

int AutoBoard::FindPacketIndex(SeedType theSeed) const
{
	Board* aBoard = mApp->mBoard;
	if (aBoard == nullptr)
		return -1;
	SeedBank* aBank = aBoard->mSeedBank;
	for (int i = 0; i < aBank->mNumPackets; ++i)
	{
		if (aBank->mSeedPackets[i].mPacketType == theSeed)
			return i;
	}
	return -1;
}

bool AutoBoard::IsCardReady(SeedType theSeed) const
{
	int i = FindPacketIndex(theSeed);
	if (i < 0)
		return false;
	const SeedPacket& p = mApp->mBoard->mSeedBank->mSeedPackets[i];
	return p.mActive && !p.mRefreshing;
}

int AutoBoard::CardCooldownTicksRemaining(SeedType theSeed) const
{
	int i = FindPacketIndex(theSeed);
	if (i < 0)
		return -1;
	const SeedPacket& p = mApp->mBoard->mSeedBank->mSeedPackets[i];
	if (!p.mRefreshing)
		return 0;
	int aRemaining = p.mRefreshTime - p.mRefreshCounter;
	return aRemaining > 0 ? aRemaining : 0;
}

static const char* PlantingReasonStr(PlantingReason r)
{
	switch (r)
	{
	case PlantingReason::PLANTING_OK:             return "ok";
	case PlantingReason::PLANTING_NOT_HERE:       return "cant_plant_there";
	case PlantingReason::PLANTING_ONLY_ON_GRAVES: return "only_on_graves";
	case PlantingReason::PLANTING_ONLY_IN_POOL:   return "only_in_pool";
	case PlantingReason::PLANTING_ONLY_ON_GROUND: return "only_on_ground";
	case PlantingReason::PLANTING_NEEDS_POT:      return "needs_pot";
	case PlantingReason::PLANTING_NOT_ON_ART:     return "not_on_art";
	case PlantingReason::PLANTING_NOT_PASSED_LINE:return "not_passed_line";
	case PlantingReason::PLANTING_NEEDS_UPGRADE:  return "needs_upgrade";
	case PlantingReason::PLANTING_NOT_ON_GRAVE:   return "not_on_grave";
	case PlantingReason::PLANTING_NOT_ON_CRATER:  return "not_on_crater";
	case PlantingReason::PLANTING_NOT_ON_WATER:   return "not_on_water";
	case PlantingReason::PLANTING_NEEDS_GROUND:   return "needs_ground";
	case PlantingReason::PLANTING_NEEDS_SLEEPING: return "needs_sleeping";
	default:                                      return "cant_plant_there";
	}
}

ActionResult AutoBoard::TryPlant(SeedType theSeed, int theRow, int theCol)
{
	Board* aBoard = mApp->mBoard;
	if (aBoard == nullptr)
		return { false, "no_board" };

	const int aCol = theCol;  // grid X
	const int aRow = theRow;  // grid Y

	int aIndex = FindPacketIndex(theSeed);
	if (aIndex < 0)
		return { false, "card_not_in_deck" };

	SeedPacket& aPacket = aBoard->mSeedBank->mSeedPackets[aIndex];
	if (!aPacket.mActive || aPacket.mRefreshing)
		return { false, "card_on_cooldown" };

	int aCost = aBoard->GetCurrentPlantCost(theSeed, SeedType::SEED_NONE);
	if (!aBoard->CanTakeSunMoney(aCost))
		return { false, "not_enough_sun" };

	PlantingReason aReason = aBoard->CanPlantAt(aCol, aRow, theSeed);
	if (aReason != PlantingReason::PLANTING_OK)
		return { false, PlantingReasonStr(aReason) };

	// Commit, mirroring Board::MouseDownWithPlant's CURSOR_TYPE_PLANT_FROM_BANK
	// path: deduct sun, place the plant, start the card cooldown. Deactivate()
	// then WasPlanted() reproduces the pick-up -> plant state transition.
	aBoard->TakeSunMoney(aCost);
	aBoard->AddPlant(aCol, aRow, theSeed);
	aPacket.Deactivate();
	aPacket.WasPlanted();
	return { true, "ok" };
}

ActionResult AutoBoard::TryShovel(int theRow, int theCol)
{
	Board* aBoard = mApp->mBoard;
	if (aBoard == nullptr)
		return { false, "no_board" };
	if (theCol < 0 || theCol >= MAX_GRID_SIZE_X || theRow < 0 || theRow >= MAX_GRID_SIZE_Y)
		return { false, "out_of_bounds" };

	Plant* aPlant = aBoard->GetTopPlantAt(theCol, theRow, PlantPriority::TOPPLANT_ANY);
	if (aPlant == nullptr)
		return { false, "no_plant_to_shovel" };

	aPlant->Die();
	return { true, "ok" };
}

}  // namespace pvzbot

#endif  // PVZ_BOT_BUILD
