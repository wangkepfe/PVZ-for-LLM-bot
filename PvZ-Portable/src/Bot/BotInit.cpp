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

namespace
{

void LogFlag(const char* name, bool value)
{
	std::fprintf(stderr, "    %-24s = %s\n", name, value ? "true" : "false");
}

}  // namespace

int RunBotMain(int argc, char** argv)
{
	std::fprintf(stderr, "[pvzbot] M1 spike: bringing up LawnApp under headless stubs.\n");
#ifdef PVZ_HEADLESS
	std::fprintf(stderr, "[pvzbot] PVZ_HEADLESS=1 (no window, no GL context)\n");
#endif

	// Hand main.cpp's pre-Init setup work to us: in main.cpp these are run
	// before our dispatch, so here we just need to construct the app.
	::LawnApp* app = new ::LawnApp();
	app->SetArgs(argc, argv);

	std::fprintf(stderr, "[pvzbot] LawnApp constructed. Calling Init()...\n");

	int initStage = 0;
	const char* initStageName = "construction";
	try
	{
		initStage = 1; initStageName = "LawnApp::Init";
		app->Init();
		initStage = 2; initStageName = "post-Init";
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr,
			"[pvzbot] EXCEPTION during %s: %s\n", initStageName, e.what());
		return 2;
	}
	catch (...)
	{
		std::fprintf(stderr,
			"[pvzbot] EXCEPTION (non-std) during %s\n", initStageName);
		return 3;
	}

	std::fprintf(stderr, "[pvzbot] Init() returned. App state:\n");
	LogFlag("mShutdown",       app->mShutdown);
	LogFlag("mLoadingFailed",  app->mLoadingFailed);
	LogFlag("mCloseRequest",   app->mCloseRequest);
	LogFlag("mLoaded",         app->mLoaded);
	std::fprintf(stderr, "    %-24s = %p\n", "app->mBoard",   (void*)app->mBoard);
	std::fprintf(stderr, "    %-24s = %p\n", "app->mWindow",  app->mWindow);
	std::fprintf(stderr, "    %-24s = %p\n", "app->mContext", app->mContext);
	std::fprintf(stderr, "    %-24s = %p\n", "app->mGLInterface", (void*)app->mGLInterface);

	// Don't call Start() yet — Start() drops into SexyAppBase's main event
	// loop, which we don't want until M2's AutoBoard takes the wheel.
	std::fprintf(stderr, "[pvzbot] M1 spike complete; not calling Start(). Exiting.\n");

	// Don't try to Shutdown either: that touches subsystems we likely failed
	// to initialise. Leaking the LawnApp on a one-shot exit is fine.
	return 0;
}

}  // namespace pvzbot

#endif  // PVZ_BOT_BUILD
