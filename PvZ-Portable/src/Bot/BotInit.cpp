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
#include <vector>

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

// Definition of the engine-visible flag declared in BotConfig.h.
bool gBotAutoCollectSun = false;

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

		// Baseline greedy policy (placeholder until the Python bot in M5):
		// a fixed target layout — sunflowers in the back two columns for
		// economy, attackers in the middle, a wall-nut up front per row. Each
		// action tick we attempt the whole plan in order; TryPlant validates
		// deck membership / cooldown / sun / placement, so the layout fills in
		// over time as sun and cooldowns allow. Sun is auto-collected.
		struct Placement { SeedType seed; int row; int col; };
		std::vector<Placement> aPlan;
		for (int row = 0; row < 5; ++row)
		{
			aPlan.push_back({ SeedType::SEED_SUNFLOWER,  row, 0 });
			aPlan.push_back({ SeedType::SEED_SUNFLOWER,  row, 1 });
			aPlan.push_back({ SeedType::SEED_PEASHOOTER, row, 2 });
			aPlan.push_back({ SeedType::SEED_REPEATER,   row, 3 });
			aPlan.push_back({ SeedType::SEED_SNOWPEA,    row, 4 });
			aPlan.push_back({ SeedType::SEED_WALLNUT,    row, 7 });
		}

		const long long kMaxTicks = 600LL * 100;   // up to 10 minutes game time
		int aPlanted = 0;
		while (aBoard.TickCount() < kMaxTicks && !aBoard.IsGameOver())
		{
			// Act 10x/second.
			if ((aBoard.TickCount() % 10) == 0)
			{
				for (const Placement& p : aPlan)
				{
					if (aBoard.TryPlant(p.seed, p.row, p.col).accepted)
						aPlanted++;
				}
			}
			aBoard.Tick(1);

			if ((aBoard.TickCount() % 2000) == 0)   // report every 20s
			{
				b = aBoard.GetBoard();
				std::fprintf(stderr,
					"[pvzbot] t=%4llds  sun=%-5d wave=%-3d flags=%d zombies=%-3d plants=%-3d planted=%d\n",
					(long long)(aBoard.TickCount() / 100),
					b->mSunMoney, b->mCurrentWave, b->GetSurvivalFlagsCompleted(),
					b->mZombies.mSize, b->mPlants.mSize, aPlanted);
			}
		}

		std::fprintf(stderr,
			"[pvzbot] done. ticks=%lld (%llds)  flags=%d  gameOver=%d  boardResult=%d\n",
			aBoard.TickCount(), (long long)(aBoard.TickCount() / 100),
			aBoard.GetBoard()->GetSurvivalFlagsCompleted(),
			(int)aBoard.IsGameOver(), (int)app->mBoardResult);
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
