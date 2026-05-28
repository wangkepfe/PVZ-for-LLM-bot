/*
 * Copyright (C) 2026 PvZ Bot-Engine contributors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Bot-engine entry-point dispatch. The detailed work (AutoBoard, semantic
 * actions, observation, event log) lives in later M2-M5 files; this M1
 * spike does just enough to characterise what fails when we try to bring
 * the engine up without a main.pak.
 */

#include "BotConfig.h"

#include <cstdio>
#include <cstring>
#include <exception>

#ifdef PVZ_BOT_BUILD

#include "LawnApp.h"
#include "Resources.h"
#include "Sexy.TodLib/TodStringFile.h"
#include "Lawn/Board.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/System/ProfileMgr.h"
#include "Lawn/System/Music.h"
#include "AutoBoard.h"

extern bool gIsPartnerBuild;

namespace pvzbot
{

bool ShouldTakeOverMain(int argc, char** argv)
{
#ifdef PVZ_HEADLESS
	(void)argc; (void)argv;
	return true;
#else
	for (int i = 1; i < argc; ++i)
	{
		if (argv[i] && std::strcmp(argv[i], "--autobot") == 0)
		{
			return true;
		}
	}
	return false;
#endif
}

int RunBotMain(int argc, char** argv)
{
	std::fprintf(stderr, "[pvzbot] M2: headless Day Endless tick test.\n");
#ifdef PVZ_HEADLESS
	std::fprintf(stderr, "[pvzbot] PVZ_HEADLESS=1 (no window, no GL context)\n");
#endif

	::LawnApp* app = new ::LawnApp();
	// The engine relies on the global gLawnApp (GameObject's constructor reads
	// gLawnApp->mBoard, etc.). main.cpp sets this on the normal path; we must
	// too since we took over before that line ran.
	gLawnApp = app;
	app->SetArgs(argc, argv);

	try
	{
		app->Init();
		if (app->mShutdown || app->mLoadingFailed)
		{
			std::fprintf(stderr, "[pvzbot] Init failed (shutdown=%d loadingFailed=%d). "
				"Did you pass -resdir=<path to main.pak folder>?\n",
				app->mShutdown, app->mLoadingFailed);
			return 1;
		}

		// Headless has no audio device. Music::PlayMusic() early-outs on this
		// flag (the music path otherwise crashes inside SDL_mixer). Sound
		// effects route through SDLSoundManager which no-ops when the mixer
		// isn't initialised.
		if (app->mMusic != nullptr)
			app->mMusic->mMusicDisabled = true;

		// A player profile is normally created via the NewUserDialog on first
		// run; we skipped that UI. Board::GetLevelRandSeed() and others deref
		// mPlayerInfo, so ensure one exists.
		if (app->mPlayerInfo == nullptr)
		{
			app->mPlayerInfo = app->mProfileMgr->GetAnyProfile();
			if (app->mPlayerInfo == nullptr)
				app->mPlayerInfo = app->mProfileMgr->AddProfile("Bot");
			std::fprintf(stderr, "[pvzbot] player profile = %p\n", (void*)app->mPlayerInfo);
		}

		// Start() would drop into the SDL main loop + loader screen. Instead we
		// run the resource-loading pass synchronously ourselves, then take the
		// board over directly.
		std::fprintf(stderr, "[pvzbot] loading resources (synchronous LoadingThreadProc)...\n");
		app->LoadingThreadProc();
		if (app->mShutdown || app->mLoadingFailed)
		{
			std::fprintf(stderr, "[pvzbot] resource load failed.\n");
			return 1;
		}

		std::fprintf(stderr, "[pvzbot] bootstrapping Day Endless board...\n");
		AutoBoard aBoard(app);
		aBoard.Bootstrap();

		Board* b = aBoard.GetBoard();
		std::fprintf(stderr,
			"[pvzbot] board up. sun=%d  packets=%d  scene=%d\n",
			b->mSunMoney, b->mSeedBank->mNumPackets, (int)app->mGameScene);

		// DEBUG (pre-action-API): plant peashooters in column 2 of every row
		// plus a couple of sunflowers, to exercise the combat code paths
		// (plant update, projectile spawn/flight, zombie damage/death) headless.
		// AddPlant is the low-level create; it doesn't charge sun.
		for (int row = 0; row < 5; ++row)
			b->AddPlant(2, row, SeedType::SEED_PEASHOOTER);
		b->AddPlant(0, 1, SeedType::SEED_SUNFLOWER);
		b->AddPlant(0, 3, SeedType::SEED_SUNFLOWER);
		std::fprintf(stderr, "[pvzbot] planted debug peashooters+sunflowers. plants=%d\n",
			b->mPlants.mSize);

		// Tick up to ~300 seconds of game time (~2 flags), reporting every 20s
		// or until the level ends. Track the zombie high-water mark so we can
		// see deaths (count dropping) vs. pure accumulation.
		const int kSecondsToRun = 300;
		int aMaxZombies = 0;
		for (int sec = 0; sec < kSecondsToRun && !aBoard.IsGameOver(); ++sec)
		{
			aBoard.Tick(100);
			b = aBoard.GetBoard();
			if (b->mZombies.mSize > aMaxZombies)
				aMaxZombies = b->mZombies.mSize;
			if ((sec % 20) == 19)
			{
				std::fprintf(stderr,
					"[pvzbot] t=%4llds  sun=%-5d wave=%-3d zombies=%-3d (max %d) plants=%-3d projectiles=%-3d\n",
					(long long)(aBoard.TickCount() / 100),
					b->mSunMoney, b->mCurrentWave,
					b->mZombies.mSize, aMaxZombies, b->mPlants.mSize, b->mProjectiles.mSize);
			}
		}

		std::fprintf(stderr,
			"[pvzbot] done. ticks=%lld  gameOver=%d  boardResult=%d  scene=%d\n",
			aBoard.TickCount(), (int)aBoard.IsGameOver(),
			(int)app->mBoardResult, (int)app->mGameScene);
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "[pvzbot] EXCEPTION: %s\n", e.what());
		return 2;
	}
	catch (...)
	{
		std::fprintf(stderr, "[pvzbot] EXCEPTION (non-std)\n");
		return 3;
	}

	return 0;
}

}  // namespace pvzbot

#endif  // PVZ_BOT_BUILD
