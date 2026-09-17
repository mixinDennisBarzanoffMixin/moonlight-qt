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

    // A pinch direction reversal must affect the next raw frame immediately.
    const MacOSMagnifyPoint close1 = mapMacOSTrackpadContact(
        0.45f, 0.50f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint close2 = mapMacOSTrackpadContact(
        0.55f, 0.50f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint reversed1 = mapMacOSTrackpadContact(
        0.42f, 0.50f, 0.50f, 0.50f, 500, 400, 1000, 800);
    const MacOSMagnifyPoint reversed2 = mapMacOSTrackpadContact(
        0.58f, 0.50f, 0.50f, 0.50f, 500, 400, 1000, 800);
    assert(separation(reversed1, reversed2) > separation(close1, close2));

    return 0;
}
