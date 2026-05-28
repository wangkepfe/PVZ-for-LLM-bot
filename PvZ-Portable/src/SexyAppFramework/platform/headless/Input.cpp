/*
 * Copyright (C) 2026 PvZ Bot-Engine contributors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Headless Input stub. Replaces platform/default/Input.cpp when PVZ_HEADLESS
 * is defined. No SDL event pump, no text input — bot inputs arrive through
 * the Bot API, not as SDL events.
 *
 * See bot-engine/docs/DESIGN.md §3.3.
 */

#include <string>
#include "SexyAppBase.h"

using namespace Sexy;

void SexyAppBase::InitInput()
{
	// No SDL_INIT_EVENTS — we deliberately avoid touching SDL in headless.
}

bool SexyAppBase::StartTextInput(std::string& /*theInput*/)
{
	return false;
}

void SexyAppBase::StopTextInput()
{
}

bool SexyAppBase::ProcessDeferredMessages(bool /*singleMessage*/)
{
	// No pending OS events ever in headless mode.
	return false;
}
