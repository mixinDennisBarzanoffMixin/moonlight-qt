#include "input.h"

#include <Limelight.h>
#include "SDL_compat.h"
#include "streaming/streamutils.h"

static bool isZoomDiagnosticLoggingEnabled()
{
    static const bool enabled = qEnvironmentVariableIntValue("MOONLIGHT_ZOOM_DIAGNOSTICS") != 0;
    return enabled;
}

static bool isCadScrollFilterEnabled()
{
    static const bool enabled = qEnvironmentVariableIntValue("MOONLIGHT_CAD_SCROLL_FILTER") != 0;
    return enabled;
}

static Uint32 cadScrollIntervalMs()
{
    static const Uint32 interval = []() {
        const int configured = qEnvironmentVariableIntValue("MOONLIGHT_CAD_SCROLL_INTERVAL_MS");
        return (Uint32) qBound(8, configured == 0 ? 40 : configured, 250);
    }();
    return interval;
}

void SdlInputHandler::handleMouseButtonEvent(SDL_MouseButtonEvent* event)
{
    int button;

    if (event->which == SDL_TOUCH_MOUSEID) {
        // Ignore synthetic mouse events
        return;
    }
    else if (!isCaptureActive()) {
        if (event->button == SDL_BUTTON_LEFT && event->state == SDL_RELEASED &&
                isMouseInVideoRegion(event->x, event->y)) {
            // Capture the mouse again if clicked when unbound.
            // We start capture on left button released instead of
            // pressed to avoid sending an errant mouse button released
            // event to the host when clicking into our window (since
            // the pressed event was consumed by this code).
            setCaptureActive(true);
        }

        // Not capturing
        return;
    }
    else if (m_AbsoluteMouseMode && !isMouseInVideoRegion(event->x, event->y) && event->state == SDL_PRESSED) {
        // Ignore button presses outside the video region, but allow button releases
        return;
    }

    switch (event->button)
    {
        case SDL_BUTTON_LEFT:
            button = BUTTON_LEFT;
            break;
        case SDL_BUTTON_MIDDLE:
            button = BUTTON_MIDDLE;
            break;
        case SDL_BUTTON_RIGHT:
            button = BUTTON_RIGHT;
            break;
        case SDL_BUTTON_X1:
            button = BUTTON_X1;
            break;
        case SDL_BUTTON_X2:
            button = BUTTON_X2;
            break;
        default:
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Unhandled button event: %d",
                        event->button);
            return;
    }

    if (m_SwapMouseButtons) {
        if (button == BUTTON_RIGHT)
            button = BUTTON_LEFT;
        else if (button == BUTTON_LEFT)
            button = BUTTON_RIGHT;
    }

    LiSendMouseButtonEvent(event->state == SDL_PRESSED ?
                               BUTTON_ACTION_PRESS :
                               BUTTON_ACTION_RELEASE,
                           button);
}

void SdlInputHandler::handleMouseMotionEvent(SDL_MouseMotionEvent* event)
{
    if (!isCaptureActive()) {
        // Not capturing
        return;
    }
    else if (event->which == SDL_TOUCH_MOUSEID) {
        // Ignore synthetic mouse events
        return;
    }

    // Batch all pending mouse motion events to save CPU time
    Sint32 x = event->x, y = event->y, xrel = event->xrel, yrel = event->yrel;
    SDL_Event nextEvent;
    while (SDL_PeepEvents(&nextEvent, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION) > 0) {
        event = &nextEvent.motion;

        // Ignore synthetic mouse events
        if (event->which != SDL_TOUCH_MOUSEID) {
            x = event->x;
            y = event->y;
            xrel += event->xrel;
            yrel += event->yrel;
        }
    }

    // We should not reference the original event anymore
    event = nullptr;

    if (m_AbsoluteMouseMode) {
        int windowWidth, windowHeight;
        SDL_GetWindowSize(m_Window, &windowWidth, &windowHeight);

        SDL_Rect src, dst;
        bool mouseInVideoRegion;

        src.x = src.y = 0;
        src.w = m_StreamWidth;
        src.h = m_StreamHeight;

        dst.x = dst.y = 0;
        dst.w = windowWidth;
        dst.h = windowHeight;

        // Use the stream and window sizes to determine the video region
        StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

        mouseInVideoRegion = isMouseInVideoRegion(x, y, windowWidth, windowHeight);

        // Clamp motion to the video region
        x = qMin(qMax(x - dst.x, 0), dst.w);
        y = qMin(qMax(y - dst.y, 0), dst.h);

        if (isZoomDiagnosticLoggingEnabled()) {
            static Uint32 lastMotionLog = 0;
            const Uint32 now = SDL_GetTicks();
            if (now - lastMotionLog >= 250) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "ZoomDiag motion: stream=%dx%d window=%dx%d video=(%d,%d %dx%d) mapped=(%d,%d) inVideo=%d",
                            m_StreamWidth, m_StreamHeight,
                            windowWidth, windowHeight,
                            dst.x, dst.y, dst.w, dst.h,
                            x, y, mouseInVideoRegion);
                lastMotionLog = now;
            }
        }

        // Send the mouse position update if one of the following is true:
        // a) it is in the video region now
        // b) it just left the video region (to ensure the mouse is clamped to the video boundary)
        // c) a mouse button is still down from before the cursor left the video region (to allow smooth dragging)
        Uint32 buttonState = SDL_GetMouseState(nullptr, nullptr);
        if (buttonState == 0) {
            if (m_PendingMouseButtonsAllUpOnVideoRegionLeave) {
                if (m_NeedsManualCaptureOnLeave) {
                    // Stop capturing the mouse now
                    SDL_CaptureMouse(SDL_FALSE);
                }
                m_PendingMouseButtonsAllUpOnVideoRegionLeave = false;
            }
        }
        if (mouseInVideoRegion || m_MouseWasInVideoRegion || m_PendingMouseButtonsAllUpOnVideoRegionLeave) {
            LiSendMousePositionEvent((short)x, (short)y, dst.w, dst.h);
        }

        // Adjust the cursor visibility if applicable
        if (mouseInVideoRegion ^ m_MouseWasInVideoRegion) {
            SDL_ShowCursor((mouseInVideoRegion && m_MouseCursorCapturedVisibilityState == SDL_DISABLE) ? SDL_DISABLE : SDL_ENABLE);
            if (!mouseInVideoRegion && buttonState != 0) {
                // If we still have a button pressed on leave, wait for that to come up
                // before we stop sending mouse position events.
                m_PendingMouseButtonsAllUpOnVideoRegionLeave = true;
            }
        }

        m_MouseWasInVideoRegion = mouseInVideoRegion;
    }
    else {
        LiSendMouseMoveEvent(xrel, yrel);
    }
}

void SdlInputHandler::handleMouseWheelEvent(SDL_MouseWheelEvent* event)
{
    unsigned long long diagnosticSequence = 0;
    Uint32 diagnosticElapsedMs = 0;

    if (isZoomDiagnosticLoggingEnabled()) {
        static unsigned long long wheelSequence = 0;
        static Uint32 lastWheelTimestamp = 0;
        const Uint32 now = SDL_GetTicks();

        diagnosticSequence = ++wheelSequence;
        diagnosticElapsedMs = lastWheelTimestamp == 0 ? 0 : now - lastWheelTimestamp;
        lastWheelTimestamp = now;
    }

    if (!isCaptureActive()) {
        if (isZoomDiagnosticLoggingEnabled()) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "ZoomDiag wheel-drop: seq=%llu elapsedMs=%u reason=capture-inactive",
                        diagnosticSequence, diagnosticElapsedMs);
        }

        // Not capturing
        return;
    }
    else if (event->which == SDL_TOUCH_MOUSEID) {
        if (isZoomDiagnosticLoggingEnabled()) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "ZoomDiag wheel-drop: seq=%llu elapsedMs=%u reason=synthetic-touch-mouse",
                        diagnosticSequence, diagnosticElapsedMs);
        }

        // Ignore synthetic mouse events
        return;
    }

    if (isZoomDiagnosticLoggingEnabled()) {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
#if SDL_VERSION_ATLEAST(2, 0, 18)
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "ZoomDiag wheel-raw: seq=%llu elapsedMs=%u integer=(%d,%d) precise=(%.6f,%.6f) direction=%u mouse=(%d,%d) modifiers=0x%x absolute=%d reverse=%d focused=%d",
                    diagnosticSequence, diagnosticElapsedMs,
                    event->x, event->y,
                    event->preciseX, event->preciseY,
                    event->direction, mouseX, mouseY,
                    SDL_GetModState(), m_AbsoluteMouseMode, m_ReverseScrollDirection,
                    (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_INPUT_FOCUS) != 0);
#else
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "ZoomDiag wheel-raw: seq=%llu elapsedMs=%u integer=(%d,%d) direction=%u mouse=(%d,%d) modifiers=0x%x absolute=%d reverse=%d focused=%d",
                    diagnosticSequence, diagnosticElapsedMs,
                    event->x, event->y,
                    event->direction, mouseX, mouseY,
                    SDL_GetModState(), m_AbsoluteMouseMode, m_ReverseScrollDirection,
                    (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_INPUT_FOCUS) != 0);
#endif
    }

    if (m_AbsoluteMouseMode) {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        if (!isMouseInVideoRegion(mouseX, mouseY)) {
            if (isZoomDiagnosticLoggingEnabled()) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "ZoomDiag wheel-drop: seq=%llu elapsedMs=%u reason=outside-video mouse=(%d,%d)",
                            diagnosticSequence, diagnosticElapsedMs, mouseX, mouseY);
            }

            // Ignore scroll events outside the video region
            return;
        }
    }

#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (event->preciseY != 0.0f) {
        const float rawValue = event->preciseY;
        bool sendValue = true;

        // Invert the scroll direction if needed
        if (m_ReverseScrollDirection) {
            event->preciseY = -event->preciseY;
        }

        const float directedValue = event->preciseY;

#ifdef Q_OS_DARWIN
        if (isCadScrollFilterEnabled()) {
            static float pendingValue = 0.0f;
            static Uint32 lastInputTimestamp = 0;
            static Uint32 lastSendTimestamp = 0;
            const Uint32 now = SDL_GetTicks();

            // Don't carry an unfinished fraction into the next gesture.
            if (lastInputTimestamp != 0 && now - lastInputTimestamp > 120) {
                pendingValue = 0.0f;
                lastSendTimestamp = 0;
            }
            lastInputTimestamp = now;
            pendingValue += event->preciseY;

            if (lastSendTimestamp != 0 && now - lastSendTimestamp < cadScrollIntervalMs()) {
                sendValue = false;
                if (isZoomDiagnosticLoggingEnabled()) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "ZoomDiag wheel-filter: seq=%llu axis=y action=coalesce pending=%.6f waitMs=%u",
                                diagnosticSequence, pendingValue,
                                cadScrollIntervalMs() - (now - lastSendTimestamp));
                }
            }
            else {
                event->preciseY = SDL_clamp(pendingValue, -1.0f, 1.0f);
                pendingValue = 0.0f;
                lastSendTimestamp = now;
            }
        }
        else {
            // HACK: Clamp the scroll values on macOS to prevent OS scroll acceleration
            // from generating wild scroll deltas when scrolling quickly.
            event->preciseY = SDL_clamp(event->preciseY, -1.0f, 1.0f);
        }
#endif

        if (sendValue) {
            const short wireValue = (short)(event->preciseY * 120); // WHEEL_DELTA
            LiSendHighResScrollEvent(wireValue);

            if (isZoomDiagnosticLoggingEnabled()) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "ZoomDiag wheel-send: seq=%llu axis=y raw=%.6f directed=%.6f clamped=%.6f wire=%d clampedByClient=%d quantizedToZero=%d cadFilter=%d",
                            diagnosticSequence, rawValue, directedValue, event->preciseY, wireValue,
                            directedValue != event->preciseY, wireValue == 0,
                            isCadScrollFilterEnabled());
            }
        }
    }

    if (event->preciseX != 0.0f) {
        const float rawValue = event->preciseX;
        bool sendValue = true;

        // Invert the scroll direction if needed
        if (m_ReverseScrollDirection) {
            event->preciseX = -event->preciseX;
        }

        const float directedValue = event->preciseX;

#ifdef Q_OS_DARWIN
        if (isCadScrollFilterEnabled()) {
            static float pendingValue = 0.0f;
            static Uint32 lastInputTimestamp = 0;
            static Uint32 lastSendTimestamp = 0;
            const Uint32 now = SDL_GetTicks();

            // Keep horizontal two-finger scrolling available while applying the
            // same event-rate guard used for vertical scrolling. Previously the
            // CAD filter discarded this axis entirely, which made side-to-side
            // navigation impossible in KiCad and other wide canvases.
            if (lastInputTimestamp != 0 && now - lastInputTimestamp > 120) {
                pendingValue = 0.0f;
                lastSendTimestamp = 0;
            }
            lastInputTimestamp = now;
            pendingValue += event->preciseX;

            if (lastSendTimestamp != 0 && now - lastSendTimestamp < cadScrollIntervalMs()) {
                sendValue = false;
                if (isZoomDiagnosticLoggingEnabled()) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "ZoomDiag wheel-filter: seq=%llu axis=x action=coalesce pending=%.6f waitMs=%u",
                                diagnosticSequence, pendingValue,
                                cadScrollIntervalMs() - (now - lastSendTimestamp));
                }
            }
            else {
                event->preciseX = SDL_clamp(pendingValue, -1.0f, 1.0f);
                pendingValue = 0.0f;
                lastSendTimestamp = now;
            }
        }
        else {
            // HACK: Clamp the scroll values on macOS to prevent OS scroll acceleration
            // from generating wild scroll deltas when scrolling quickly.
            event->preciseX = SDL_clamp(event->preciseX, -1.0f, 1.0f);
        }
#endif

        if (sendValue) {
            const short wireValue = (short)(event->preciseX * 120); // WHEEL_DELTA
            LiSendHighResHScrollEvent(wireValue);

            if (isZoomDiagnosticLoggingEnabled()) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "ZoomDiag wheel-send: seq=%llu axis=x raw=%.6f directed=%.6f clamped=%.6f wire=%d clampedByClient=%d quantizedToZero=%d cadFilter=%d",
                            diagnosticSequence, rawValue, directedValue, event->preciseX, wireValue,
                            directedValue != event->preciseX, wireValue == 0,
                            isCadScrollFilterEnabled());
            }
        }
    }
#else
    if (event->y != 0) {
        // Invert the scroll direction if needed
        if (m_ReverseScrollDirection) {
            event->y = -event->y;
        }

#ifdef Q_OS_DARWIN
        // See comment above
        event->y = SDL_clamp(event->y, -1, 1);
#endif

        LiSendScrollEvent((signed char)event->y);
    }

    if (event->x != 0) {
        // Invert the scroll direction if needed
        if (m_ReverseScrollDirection) {
            event->x = -event->x;
        }

#ifdef Q_OS_DARWIN
        // See comment above
        event->x = SDL_clamp(event->x, -1, 1);
#endif

        LiSendHScrollEvent((signed char)event->x);
    }
#endif
}

void SdlInputHandler::handleMagnifyGesture(float magnification, int x, int y, int touchCount, unsigned int phase)
{
    constexpr unsigned int GestureStateBegan = 1;
    constexpr unsigned int GestureStateEnded = 3;
    constexpr unsigned int GestureStateCancelled = 4;
    constexpr unsigned int GestureStateFailed = 5;
    constexpr uint32_t MagnifyTouch1 = 0x4D4C0001;
    constexpr uint32_t MagnifyTouch2 = 0x4D4C0002;

    if (!isCaptureActive()) {
        if (m_MagnifyTouchActive) {
            LiSendTouchEvent(LI_TOUCH_EVENT_CANCEL_ALL, 0, 0.0f, 0.0f, 0.0f,
                             0.0f, 0.0f, LI_ROT_UNKNOWN);
            m_MagnifyTouchActive = false;
        }
        return;
    }

    const bool hostSupportsTouch = (LiGetHostFeatureFlags() & LI_FF_PEN_TOUCH_EVENTS) != 0;
    if (hostSupportsTouch) {
        int windowWidth, windowHeight;
        SDL_GetWindowSize(m_Window, &windowWidth, &windowHeight);

        SDL_Rect src = { 0, 0, m_StreamWidth, m_StreamHeight };
        SDL_Rect dst = { 0, 0, windowWidth, windowHeight };
        StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

        m_MagnifyTouchCenterX = qBound(0.0f, (x - dst.x) / (float)dst.w, 1.0f);
        m_MagnifyTouchCenterY = qBound(0.0f, (y - dst.y) / (float)dst.h, 1.0f);

        const bool ending = phase == GestureStateEnded ||
                            phase == GestureStateCancelled ||
                            phase == GestureStateFailed;
        if (!m_MagnifyTouchActive && ending) {
            return;
        }

        if (!m_MagnifyTouchActive || phase == GestureStateBegan) {
            m_MagnifyTouchRadius = 0.035f;
            m_MagnifyTouchActive = true;

            LiSendTouchEvent(LI_TOUCH_EVENT_DOWN, MagnifyTouch1,
                             qBound(0.0f, m_MagnifyTouchCenterX - m_MagnifyTouchRadius, 1.0f),
                             m_MagnifyTouchCenterY, 0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
            LiSendTouchEvent(LI_TOUCH_EVENT_DOWN, MagnifyTouch2,
                             qBound(0.0f, m_MagnifyTouchCenterX + m_MagnifyTouchRadius, 1.0f),
                             m_MagnifyTouchCenterY, 0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
        }

        // The recognizer supplies incremental magnification because the bridge
        // resets its value after every callback. Move the contacts apart or
        // together for scale while their shared center carries X/Y translation.
        m_MagnifyTouchRadius = qBound(0.005f,
                                      m_MagnifyTouchRadius * (1.0f + magnification),
                                      0.25f);

        const float touch1X = qBound(0.0f, m_MagnifyTouchCenterX - m_MagnifyTouchRadius, 1.0f);
        const float touch2X = qBound(0.0f, m_MagnifyTouchCenterX + m_MagnifyTouchRadius, 1.0f);
        const uint8_t touchEventType = (phase == GestureStateCancelled || phase == GestureStateFailed) ?
                                           LI_TOUCH_EVENT_CANCEL :
                                           ending ? LI_TOUCH_EVENT_UP : LI_TOUCH_EVENT_MOVE;

        LiSendTouchEvent(touchEventType, MagnifyTouch1, touch1X, m_MagnifyTouchCenterY,
                         0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
        LiSendTouchEvent(touchEventType, MagnifyTouch2, touch2X, m_MagnifyTouchCenterY,
                         0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);

        if (isZoomDiagnosticLoggingEnabled()) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "ZoomDiag magnify-touch: magnification=%.6f center=(%.6f,%.6f) radius=%.6f contacts=(%.6f,%.6f) phase=%u eventType=%u touches=%d",
                        magnification,
                        m_MagnifyTouchCenterX, m_MagnifyTouchCenterY,
                        m_MagnifyTouchRadius, touch1X, touch2X,
                        phase, touchEventType, touchCount);
        }

        if (ending) {
            m_MagnifyTouchActive = false;
        }
        return;
    }

    if (magnification == 0.0f) {
        return;
    }

    bool focalPointSent = false;
    if (m_AbsoluteMouseMode) {
        int windowWidth, windowHeight;
        SDL_GetWindowSize(m_Window, &windowWidth, &windowHeight);

        SDL_Rect src = { 0, 0, m_StreamWidth, m_StreamHeight };
        SDL_Rect dst = { 0, 0, windowWidth, windowHeight };
        StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

        if (isMouseInVideoRegion(x, y, windowWidth, windowHeight)) {
            const int mappedX = qMin(qMax(x - dst.x, 0), dst.w);
            const int mappedY = qMin(qMax(y - dst.y, 0), dst.h);
            LiSendMousePositionEvent((short)mappedX, (short)mappedY, dst.w, dst.h);
            focalPointSent = true;
        }
    }

    // NSEvent magnification is a fractional scale delta. A cumulative 0.1
    // magnification maps to one Windows WHEEL_DELTA notch. Preserve fractional
    // wire deltas between events so slow pinches are never lost to rounding.
    const float scaledDelta = magnification * 1200.0f + m_MagnifyWheelRemainder;
    const short wireValue = (short)SDL_clamp((int)scaledDelta, -120, 120);
    m_MagnifyWheelRemainder = scaledDelta - wireValue;

    if (wireValue == 0) {
        return;
    }

    constexpr short VK_LCONTROL = 0xA2;
    const short wireCtrlCode = (short)(0x8000 | VK_LCONTROL);
    const bool ctrlAlreadyDown = m_KeysDown.contains(VK_LCONTROL) || m_KeysDown.contains(0xA3);

    if (!ctrlAlreadyDown) {
        LiSendKeyboardEvent2(wireCtrlCode,
                            KEY_ACTION_DOWN,
                            MODIFIER_CTRL,
                            0);
    }

    LiSendHighResScrollEvent(wireValue);

    if (!ctrlAlreadyDown) {
        LiSendKeyboardEvent2(wireCtrlCode,
                            KEY_ACTION_UP,
                            0,
                            0);
    }

    if (isZoomDiagnosticLoggingEnabled()) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "ZoomDiag magnify-wheel-fallback: magnification=%.6f wire=%d remainder=%.6f focal=(%d,%d) focalSent=%d touches=%d phase=%u ctrlAlreadyDown=%d",
                    magnification, wireValue, m_MagnifyWheelRemainder,
                    x, y, focalPointSent, touchCount, phase, ctrlAlreadyDown);
    }
}

bool SdlInputHandler::isMouseInVideoRegion(int mouseX, int mouseY, int windowWidth, int windowHeight)
{
    SDL_Rect src, dst;

    if (windowWidth < 0 || windowHeight < 0) {
        SDL_GetWindowSize(m_Window, &windowWidth, &windowHeight);
    }

    src.x = src.y = 0;
    src.w = m_StreamWidth;
    src.h = m_StreamHeight;

    dst.x = dst.y = 0;
    dst.w = windowWidth;
    dst.h = windowHeight;

    // Use the stream and window sizes to determine the video region
    StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

    return (mouseX >= dst.x && mouseX <= dst.x + dst.w) &&
           (mouseY >= dst.y && mouseY <= dst.y + dst.h);
}

void SdlInputHandler::updatePointerRegionLock()
{
    // Pointer region lock is irrelevant in relative mouse mode
    if (SDL_GetRelativeMouseMode()) {
        return;
    }

    // Our pointer lock behavior tracks with the fullscreen mode unless the user has
    // toggled it themselves using the keyboard shortcut. If that's the case, they
    // have full control over it and we don't touch it anymore.
    if (!m_PointerRegionLockToggledByUser) {
        // Lock the pointer in true full-screen mode or in any fullscreen mode when only a single monitor is present
        Uint32 fullscreenFlags = SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
        m_PointerRegionLockActive = (fullscreenFlags == SDL_WINDOW_FULLSCREEN) ||
                                    (fullscreenFlags != 0 && SDL_GetNumVideoDisplays() == 1);
    }

    // If region lock is enabled, grab the cursor so it can't accidentally leave our window.
    if (isCaptureActive() && m_PointerRegionLockActive) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        SDL_Rect src, dst;

        src.x = src.y = 0;
        src.w = m_StreamWidth;
        src.h = m_StreamHeight;

        dst.x = dst.y = 0;
        SDL_GetWindowSize(m_Window, &dst.w, &dst.h);

        // Use the stream and window sizes to determine the video region
        StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

        // SDL 2.0.18 lets us lock the cursor to a specific region
        SDL_SetWindowMouseRect(m_Window, &dst);
#elif SDL_VERSION_ATLEAST(2, 0, 15)
        // SDL 2.0.15 only lets us lock the cursor to the whole window
        SDL_SetWindowMouseGrab(m_Window, SDL_TRUE);
#else
        SDL_SetWindowGrab(m_Window, SDL_TRUE);
#endif
    }
    else {
        // Allow the cursor to leave the bounds of our video region or window
#if SDL_VERSION_ATLEAST(2, 0, 18)
        SDL_SetWindowMouseRect(m_Window, nullptr);
#elif SDL_VERSION_ATLEAST(2, 0, 15)
        SDL_SetWindowMouseGrab(m_Window, SDL_FALSE);
#else
        SDL_SetWindowGrab(m_Window, SDL_FALSE);
#endif
    }
}
