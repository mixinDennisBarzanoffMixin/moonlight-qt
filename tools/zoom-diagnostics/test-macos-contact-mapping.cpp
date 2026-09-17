#include "../../app/streaming/input/macos_magnify.h"

#include <cassert>
#include <cmath>

static float separation(MacOSMagnifyPoint first, MacOSMagnifyPoint second)
{
    return std::hypot((float)(second.x - first.x), (float)(second.y - first.y));
}

int main()
{
    const float startDistanceSquared = 0.05f;
    const MacOSMagnifyPoint initial1 = mapMacOSTrackpadContact(
        0.40f, 0.45f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint initial2 = mapMacOSTrackpadContact(
        0.60f, 0.55f, 0.50f, 0.50f, 500, 400, 1000, 800);
    assert(std::abs(initial1.x - 400) <= 1 && std::abs(initial1.y - 440) <= 1);
    assert(std::abs(initial2.x - 600) <= 1 && std::abs(initial2.y - 360) <= 1);

    // Both axes must translate while the contact separation stays unchanged.
    const MacOSMagnifyContacts translated = invertMacOSPinchScale(
        0.50f, 0.55f, 0.70f, 0.65f, startDistanceSquared);
    const MacOSMagnifyPoint translated1 = mapMacOSTrackpadContact(
        translated.touch1X, translated.touch1Y, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint translated2 = mapMacOSTrackpadContact(
        translated.touch2X, translated.touch2Y, 0.50f, 0.50f, 500, 400, 1000, 800);
    assert(std::abs((translated1.x - initial1.x) - 100) <= 1);
    assert(std::abs((translated1.y - initial1.y) + 80) <= 1);
    assert(std::abs((translated2.x - initial2.x) - 100) <= 1);
    assert(std::abs((translated2.y - initial2.y) + 80) <= 1);
    assert(std::abs(separation(initial1, initial2) -
                    separation(translated1, translated2)) < 2.0f);

    const MacOSMagnifyContacts nativeStart = invertMacOSPinchScale(
        0.40f, 0.50f, 0.60f, 0.50f, 0.04f);
    const MacOSMagnifyContacts macPinchOut = invertMacOSPinchScale(
        0.35f, 0.50f, 0.65f, 0.50f, 0.04f);
    const MacOSMagnifyContacts macPinchIn = invertMacOSPinchScale(
        0.45f, 0.50f, 0.55f, 0.50f, 0.04f);
    const float nativeStartSeparation = nativeStart.touch2X - nativeStart.touch1X;
    const float pinchOutWireSeparation = macPinchOut.touch2X - macPinchOut.touch1X;
    const float pinchInWireSeparation = macPinchIn.touch2X - macPinchIn.touch1X;

    // The Sunshine/Windows touch path observed in live acceptance interprets
    // decreasing wire separation as zoom-in and increasing separation as
    // zoom-out. The macOS gestures must therefore be reflected around start.
    assert(pinchOutWireSeparation < nativeStartSeparation); // pinch-out -> zoom-in
    assert(pinchInWireSeparation > nativeStartSeparation); // pinch-in -> zoom-out

    // Reversing direction must change the very next output frame.
    const MacOSMagnifyContacts reversedOut = invertMacOSPinchScale(
        0.43f, 0.50f, 0.57f, 0.50f, 0.04f);
    assert(reversedOut.touch2X - reversedOut.touch1X < pinchInWireSeparation);

    // Resolve mode uses positive Alt+wheel for macOS pinch-out (zoom-in) and
    // negative Alt+wheel for pinch-in (zoom-out).
    float wheelRemainder = 0.0f;
    assert(macOSMagnificationToWheelDelta(0.1f, wheelRemainder) > 0);
    wheelRemainder = 0.0f;
    assert(macOSMagnificationToWheelDelta(-0.1f, wheelRemainder) < 0);

    return 0;
}
