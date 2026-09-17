#pragma once

#include <SDL.h>

struct MacOSMagnifyPoint
{
    int x;
    int y;
};

struct MacOSMagnifyContacts
{
    float touch1X;
    float touch1Y;
    float touch2X;
    float touch2Y;
};

inline MacOSMagnifyContacts invertMacOSPinchScale(float touch1X, float touch1Y,
                                                   float touch2X, float touch2Y,
                                                   float startDistanceSquared)
{
    const float centerX = (touch1X + touch2X) * 0.5f;
    const float centerY = (touch1Y + touch2Y) * 0.5f;
    const float deltaX = touch2X - touch1X;
    const float deltaY = touch2Y - touch1Y;
    const float currentDistanceSquared = deltaX * deltaX + deltaY * deltaY;

    // Windows' remote touch stack interprets the macOS raw-contact scale in
    // the opposite direction. Reflect only the distance ratio around the live
    // centroid; translation and contact orientation remain unchanged.
    const float factor = startDistanceSquared > 0.000001f && currentDistanceSquared > 0.000001f ?
                             startDistanceSquared / currentDistanceSquared : 1.0f;
    return {
        centerX + (touch1X - centerX) * factor,
        centerY + (touch1Y - centerY) * factor,
        centerX + (touch2X - centerX) * factor,
        centerY + (touch2Y - centerY) * factor
    };
}

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

inline short macOSMagnificationToWheelDelta(float magnification, float& remainder)
{
    const float scaledDelta = magnification * 1200.0f + remainder;
    const short wireValue = (short)SDL_clamp((int)scaledDelta, -120, 120);
    remainder = scaledDelta - wireValue;
    return wireValue;
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
    bool isRawFrame;
};

// Installs a local AppKit event monitor for native trackpad pinch gestures.
// The returned token must be passed to removeMacOSMagnifyMonitor().
void* installMacOSMagnifyMonitor(SDL_Window* window, Uint32 sdlEventType);
void removeMacOSMagnifyMonitor(void* token);
