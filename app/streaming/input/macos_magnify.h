#pragma once

#include <SDL.h>

struct MacOSMagnifyEvent
{
    float magnification;
    int x;
    int y;
    int touchCount;
    unsigned int phase;
};

// Installs a local AppKit event monitor for native trackpad pinch gestures.
// The returned token must be passed to removeMacOSMagnifyMonitor().
void* installMacOSMagnifyMonitor(SDL_Window* window, Uint32 sdlEventType);
void removeMacOSMagnifyMonitor(void* token);
