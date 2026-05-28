/*
 * Copyright (C) 2026 PvZ Bot-Engine contributors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Headless Window stub. When PVZ_HEADLESS is defined, this file replaces
 * platform/default/Window.cpp. No SDL window is created and no GL context is
 * acquired.
 *
 * We DO construct a GLInterface object, but we deliberately never call its
 * Init() (nor SexyAppBase::InitGLInterface()). Rationale:
 *
 *   - The image registry (SexyAppBase::AddMemoryImage / GLInterface::
 *     AddGLImage) locks mGLInterface->mCritSect, and a great many code paths
 *     assume mGLInterface is non-null. Leaving it null means death by a
 *     thousand null-dereferences during resource loading.
 *   - The GLInterface *constructor* touches no GL — it only sets fields and a
 *     std::mutex. GLInterface::Init() is what actually creates the GL context,
 *     shaders, and buffers; we skip exactly that.
 *   - Image *bits* decode into RAM fine (MemoryImage::SetBits/CommitBits are
 *     pure CPU). GPU texture upload (TextureData::CheckCreateTextures) is lazy
 *     and only happens at draw time — and the bot-engine never enters a draw
 *     phase.
 *
 * Net effect: the engine boots, loads all its resource bits into RAM, and is
 * ready to tick game logic, without a window, GL context, or GPU.
 *
 * See bot-engine/docs/DESIGN.md §3.3 (headless stubs).
 */

#include "SexyAppBase.h"
#include "graphics/GLInterface.h"

using namespace Sexy;

void SexyAppBase::MakeWindow()
{
	// Mark the app active so the main loop doesn't sit in the minimised /
	// unfocused sleep path waiting for a window event that never comes.
	mActive = true;
	mMinimized = false;

	if (mGLInterface == nullptr)
	{
		// Construct only — do NOT Init(). See file header.
		mGLInterface = new GLInterface(this);
	}
}
