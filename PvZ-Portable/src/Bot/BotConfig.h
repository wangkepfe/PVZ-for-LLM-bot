/*
 * Copyright (C) 2026 PvZ Bot-Engine contributors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Central compile-time config for the bot-engine build. Source files inside
 * src/Bot/ should include this; engine sources outside src/Bot/ can `#ifdef
 * PVZ_BOT_BUILD` directly if they need to gate a small change.
 *
 * Macros (defined by CMake when their option is ON):
 *   PVZ_BOT_BUILD     — src/Bot/ is compiled; bot entry points are present.
 *   PVZ_HEADLESS      — platform/headless/ replaces platform/default/.
 *                       No SDL window, no GL context, no audio device.
 *   PVZ_BUILD_PYBIND  — produce a Python module instead of an exe (later M5).
 */

#pragma once

#if defined(PVZ_BUILD_PYBIND) || defined(PVZ_HEADLESS)
#  ifndef PVZ_BOT_BUILD
#    define PVZ_BOT_BUILD
#  endif
#endif

// Forward decl so engine code can call into BotInit without pulling all of
// LawnApp.h. Implemented in BotInit.cpp; only present when PVZ_BOT_BUILD is on.
namespace pvzbot
{
	// Returns true if the bot-engine wants to take over from LawnApp's normal
	// title-screen-and-menu flow. Called from main.cpp right after argument
	// parsing. When it returns true, LawnApp's regular Start() is skipped and
	// the bot entry point owns the process lifetime.
	bool ShouldTakeOverMain(int argc, char** argv);

	// Bot-engine entry point. Constructs an AutoBoard, runs the bot loop until
	// the level ends or a kill switch fires, then returns a process exit code.
	int RunBotMain(int argc, char** argv);
}
