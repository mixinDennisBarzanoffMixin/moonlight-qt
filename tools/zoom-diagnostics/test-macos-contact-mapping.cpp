#include "../../app/streaming/input/macos_magnify.h"

#include <cassert>
#include <cmath>

static float separation(MacOSMagnifyPoint first, MacOSMagnifyPoint second)
{
    return std::hypot((float)(second.x - first.x), (float)(second.y - first.y));
}

int main()
{
    const MacOSMagnifyPoint initial1 = mapMacOSTrackpadContact(
        0.40f, 0.45f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint initial2 = mapMacOSTrackpadContact(
        0.60f, 0.55f, 0.50f, 0.50f, 500, 400, 1000, 800);
    assert(std::abs(initial1.x - 400) <= 1 && std::abs(initial1.y - 440) <= 1);
    assert(std::abs(initial2.x - 600) <= 1 && std::abs(initial2.y - 360) <= 1);

    // Both axes must translate while the contact separation stays unchanged.
    const MacOSMagnifyPoint translated1 = mapMacOSTrackpadContact(
        0.50f, 0.55f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint translated2 = mapMacOSTrackpadContact(
        0.70f, 0.65f, 0.50f, 0.50f, 500, 400, 1000, 800);
    assert(std::abs((translated1.x - initial1.x) - 100) <= 1);
    assert(std::abs((translated1.y - initial1.y) + 80) <= 1);
    assert(std::abs((translated2.x - initial2.x) - 100) <= 1);
    assert(std::abs((translated2.y - initial2.y) + 80) <= 1);
    assert(std::abs(separation(initial1, initial2) -
                    separation(translated1, translated2)) < 2.0f);

    const MacOSMagnifyPoint nativeStart1 = mapMacOSTrackpadContact(
        0.40f, 0.50f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint nativeStart2 = mapMacOSTrackpadContact(
        0.60f, 0.50f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint macPinchOut1 = mapMacOSTrackpadContact(
        0.35f, 0.50f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint macPinchOut2 = mapMacOSTrackpadContact(
        0.65f, 0.50f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint macPinchIn1 = mapMacOSTrackpadContact(
        0.45f, 0.50f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint macPinchIn2 = mapMacOSTrackpadContact(
        0.55f, 0.50f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const float nativeStartSeparation = separation(nativeStart1, nativeStart2);
    const float pinchOutWireSeparation = separation(macPinchOut1, macPinchOut2);
    const float pinchInWireSeparation = separation(macPinchIn1, macPinchIn2);

    // Live acceptance confirms that Windows native touch follows physical
    // contact separation: fingers apart zoom in and fingers together zoom out.
    assert(pinchOutWireSeparation > nativeStartSeparation); // pinch-out -> zoom-in
    assert(pinchInWireSeparation < nativeStartSeparation); // pinch-in -> zoom-out

    // Reversing direction must change the very next output frame.
    const MacOSMagnifyPoint reversedOut1 = mapMacOSTrackpadContact(
        0.43f, 0.50f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint reversedOut2 = mapMacOSTrackpadContact(
        0.57f, 0.50f, 0.50f, 0.50f, 500, 400, 1000, 800);
    assert(separation(reversedOut1, reversedOut2) > pinchInWireSeparation);

    // Resolve mode uses positive Alt+wheel for macOS pinch-out (zoom-in) and
    // negative Alt+wheel for pinch-in (zoom-out).
    float wheelRemainder = 0.0f;
    assert(macOSMagnificationToWheelDelta(0.1f, wheelRemainder) > 0);
    wheelRemainder = 0.0f;
    assert(macOSMagnificationToWheelDelta(-0.1f, wheelRemainder) < 0);

    return 0;
}
