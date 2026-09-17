#pragma once

#include <SDL.h>

struct MacOSMagnifyPoint
{
    int x;
    int y;
};

inline MacOSMagnifyPoint mapMacOSTrackpadContact(float contactX, float contactY,
                                                  float startCentroidX, float startCentroidY,
                                                  int baseX, int baseY,
                                                  int windowWidth, int windowHeight)
{
    return {
        baseX + (int)((contactX - startCentroidX) * windowWidth),
        baseY - (int)((contactY - startCentroidY) * windowHeight)
    };
}

struct MacOSMagnifyEvent
{
    float magnification;
    int x;
    int y;
    int touchCount;
    unsigned int phase;
    bool hasRawContacts;
    int touch1X;
    int touch1Y;
    int touch2X;
    int touch2Y;
};

// Installs a local AppKit event monitor for native trackpad pinch gestures.
// The returned token must be passed to removeMacOSMagnifyMonitor().
void* installMacOSMagnifyMonitor(SDL_Window* window, Uint32 sdlEventType);
void removeMacOSMagnifyMonitor(void* token);
