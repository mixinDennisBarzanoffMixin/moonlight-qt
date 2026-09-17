#include "macos_magnify.h"

#include <AppKit/AppKit.h>
#include <CoreFoundation/CoreFoundation.h>
#include <SDL_syswm.h>
#include <dlfcn.h>

struct MTPoint
{
    float x;
    float y;
};

struct MTVector
{
    MTPoint position;
    MTPoint velocity;
};

struct MTTouch
{
    int32_t frame;
    double timestamp;
    int32_t pathIndex;
    int32_t state;
    int32_t fingerId;
    int32_t handId;
    MTVector normalized;
    float zTotal;
    int32_t field9;
    float angle;
    float majorAxis;
    float minorAxis;
    MTVector absolute;
    int32_t field14;
    int32_t field15;
    float zDensity;
};

typedef const void* MTDeviceRef;
typedef void (*MTFrameCallback)(MTDeviceRef, MTTouch[], size_t, double, size_t, void*);
typedef CFArrayRef (*MTDeviceCreateListFn)(void);
typedef void (*MTRegisterCallbackFn)(MTDeviceRef, MTFrameCallback, void*);
typedef void (*MTUnregisterCallbackFn)(MTDeviceRef, MTFrameCallback);
typedef int32_t (*MTDeviceStartFn)(MTDeviceRef, int);
typedef int32_t (*MTDeviceStopFn)(MTDeviceRef);

@interface MoonlightMagnifyBridge : NSObject
{
    SDL_Window* _sdlWindow;
    NSView* _contentView;
    NSMagnificationGestureRecognizer* _recognizer;
    Uint32 _sdlEventType;

    void* _multitouchFramework;
    CFArrayRef _multitouchDevices;
    MTUnregisterCallbackFn _unregisterCallback;
    MTDeviceStopFn _stopDevice;

    BOOL _hasRawCentroid;
    float _rawCentroidX;
    float _rawCentroidY;
    int _rawTouchCount;
    int32_t _rawContactPath1;
    int32_t _rawContactPath2;
    float _rawContact1X;
    float _rawContact1Y;
    float _rawContact2X;
    float _rawContact2Y;

    BOOL _rawPinchActive;
    float _rawStartX;
    float _rawStartY;
    int _gestureBaseX;
    int _gestureBaseY;
    int _gestureWindowWidth;
    int _gestureWindowHeight;
}

- (id)initWithSDLWindow:(SDL_Window*)window
            contentView:(NSView*)contentView
              eventType:(Uint32)eventType;
- (void)handleMagnify:(NSMagnificationGestureRecognizer*)recognizer;
- (void)handleRawTouches:(MTTouch*)touches count:(size_t)count;
- (void)detach;

@end

static void rawTouchCallback(MTDeviceRef,
                             MTTouch touches[],
                             size_t count,
                             double,
                             size_t,
                             void* refcon)
{
    MoonlightMagnifyBridge* bridge = (MoonlightMagnifyBridge*)refcon;
    [bridge handleRawTouches:touches count:count];
}

@implementation MoonlightMagnifyBridge

- (void)pushMagnification:(float)magnification
                        x:(int)x
                        y:(int)y
               touchCount:(int)touchCount
                    phase:(unsigned int)phase
           rawContact1X:(int)touch1X
           rawContact1Y:(int)touch1Y
           rawContact2X:(int)touch2X
           rawContact2Y:(int)touch2Y
          hasRawContacts:(BOOL)hasRawContacts
{
    MacOSMagnifyEvent* magnify = new MacOSMagnifyEvent {
        magnification,
        x,
        y,
        touchCount,
        phase,
        hasRawContacts == YES,
        touch1X,
        touch1Y,
        touch2X,
        touch2Y
    };

    SDL_Event event = {};
    event.type = _sdlEventType;
    event.user.timestamp = SDL_GetTicks();
    event.user.windowID = SDL_GetWindowID(_sdlWindow);
    event.user.data1 = magnify;

    if (SDL_PushEvent(&event) <= 0) {
        delete magnify;
    }
}

- (id)initWithSDLWindow:(SDL_Window*)window
            contentView:(NSView*)contentView
              eventType:(Uint32)eventType
{
    self = [super init];
    if (self == nil) {
        return nil;
    }

    _sdlWindow = window;
    _contentView = contentView;
    _sdlEventType = eventType;

    _recognizer = [[NSMagnificationGestureRecognizer alloc]
        initWithTarget:self
                action:@selector(handleMagnify:)];
    _recognizer.delaysMagnificationEvents = NO;
    [_contentView addGestureRecognizer:_recognizer];

    _multitouchFramework = dlopen(
        "/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport",
        RTLD_NOW | RTLD_LOCAL);
    if (_multitouchFramework == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to load raw macOS trackpad contacts: %s",
                    dlerror());
        return self;
    }

    MTDeviceCreateListFn createDeviceList = (MTDeviceCreateListFn)dlsym(_multitouchFramework,
                                                                        "MTDeviceCreateList");
    MTRegisterCallbackFn registerCallback = (MTRegisterCallbackFn)dlsym(_multitouchFramework,
                                                                        "MTRegisterContactFrameCallbackWithRefcon");
    _unregisterCallback = (MTUnregisterCallbackFn)dlsym(_multitouchFramework,
                                                        "MTUnregisterContactFrameCallback");
    MTDeviceStartFn startDevice = (MTDeviceStartFn)dlsym(_multitouchFramework, "MTDeviceStart");
    _stopDevice = (MTDeviceStopFn)dlsym(_multitouchFramework, "MTDeviceStop");

    if (createDeviceList == nullptr || registerCallback == nullptr ||
            _unregisterCallback == nullptr || startDevice == nullptr || _stopDevice == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Raw macOS trackpad contact functions are unavailable");
        dlclose(_multitouchFramework);
        _multitouchFramework = nullptr;
        return self;
    }

    _multitouchDevices = createDeviceList();
    if (_multitouchDevices == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "No macOS multitouch devices were found");
        return self;
    }

    const CFIndex deviceCount = CFArrayGetCount(_multitouchDevices);
    for (CFIndex i = 0; i < deviceCount; i++) {
        MTDeviceRef device = CFArrayGetValueAtIndex(_multitouchDevices, i);
        registerCallback(device, rawTouchCallback, self);
        startDevice(device, 0);
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Started raw macOS trackpad centroid capture on %ld device(s)",
                (long)deviceCount);
    return self;
}

- (void)handleRawTouches:(MTTouch*)touches count:(size_t)count
{
    float centroidX = 0.0f;
    float centroidY = 0.0f;
    int activeCount = 0;
    const MTTouch* firstContact = nullptr;
    const MTTouch* secondContact = nullptr;

    for (size_t i = 0; i < count; i++) {
        // MakeTouch and Touching are the stable contact states. Including
        // BreakTouch prevents the centroid from jumping just before gesture end.
        if (touches[i].state >= 3 && touches[i].state <= 5) {
            centroidX += touches[i].normalized.position.x;
            centroidY += touches[i].normalized.position.y;
            activeCount++;

            if (firstContact == nullptr || touches[i].pathIndex < firstContact->pathIndex) {
                secondContact = firstContact;
                firstContact = &touches[i];
            }
            else if (secondContact == nullptr || touches[i].pathIndex < secondContact->pathIndex) {
                secondContact = &touches[i];
            }
        }
    }

    if (activeCount < 2) {
        return;
    }

    centroidX /= activeCount;
    centroidY /= activeCount;

    BOOL pushRawMove = NO;
    int touch1X = 0;
    int touch1Y = 0;
    int touch2X = 0;
    int touch2Y = 0;
    int focalX = 0;
    int focalY = 0;

    @synchronized(self) {
        _hasRawCentroid = YES;
        _rawCentroidX = centroidX;
        _rawCentroidY = centroidY;
        _rawTouchCount = activeCount;

        if (!_rawPinchActive) {
            _rawContactPath1 = firstContact->pathIndex;
            _rawContactPath2 = secondContact->pathIndex;
        }

        const MTTouch* tracked1 = nullptr;
        const MTTouch* tracked2 = nullptr;
        for (size_t i = 0; i < count; i++) {
            if (touches[i].state < 3 || touches[i].state > 5) {
                continue;
            }
            if (touches[i].pathIndex == _rawContactPath1) {
                tracked1 = &touches[i];
            }
            else if (touches[i].pathIndex == _rawContactPath2) {
                tracked2 = &touches[i];
            }
        }

        if (tracked1 != nullptr && tracked2 != nullptr) {
            _rawContact1X = tracked1->normalized.position.x;
            _rawContact1Y = tracked1->normalized.position.y;
            _rawContact2X = tracked2->normalized.position.x;
            _rawContact2Y = tracked2->normalized.position.y;

            if (_rawPinchActive) {
                const MacOSMagnifyPoint touch1 = mapMacOSTrackpadContact(
                    _rawContact1X, _rawContact1Y, _rawStartX, _rawStartY,
                    _gestureBaseX, _gestureBaseY, _gestureWindowWidth, _gestureWindowHeight);
                const MacOSMagnifyPoint touch2 = mapMacOSTrackpadContact(
                    _rawContact2X, _rawContact2Y, _rawStartX, _rawStartY,
                    _gestureBaseX, _gestureBaseY, _gestureWindowWidth, _gestureWindowHeight);
                touch1X = touch1.x;
                touch1Y = touch1.y;
                touch2X = touch2.x;
                touch2Y = touch2.y;
                focalX = (touch1X + touch2X) / 2;
                focalY = (touch1Y + touch2Y) / 2;
                pushRawMove = YES;
            }
        }
    }

    // AppKit only invokes the magnification recognizer while scale changes.
    // Queue raw contact frames too so two-finger translation continues even
    // when the distance between the contacts is momentarily unchanged.
    if (pushRawMove) {
        [self pushMagnification:0.0f
                             x:focalX
                             y:focalY
                    touchCount:activeCount
                         phase:NSGestureRecognizerStateChanged
                  rawContact1X:touch1X
                  rawContact1Y:touch1Y
                  rawContact2X:touch2X
                  rawContact2Y:touch2Y
                 hasRawContacts:YES];
    }
}

- (void)handleMagnify:(NSMagnificationGestureRecognizer*)recognizer
{
    NSRect bounds = _contentView.bounds;
    if (NSWidth(bounds) <= 0.0 || NSHeight(bounds) <= 0.0) {
        return;
    }

    NSPoint point = [recognizer locationInView:_contentView];
    if (!_contentView.isFlipped) {
        point.y = NSHeight(bounds) - point.y;
    }

    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(_sdlWindow, &windowWidth, &windowHeight);

    int focalX = (int)(point.x * windowWidth / NSWidth(bounds));
    int focalY = (int)(point.y * windowHeight / NSHeight(bounds));
    int touchCount = 2;
    const unsigned int phase = (unsigned int)recognizer.state;
    const BOOL ending = phase == NSGestureRecognizerStateEnded ||
                        phase == NSGestureRecognizerStateCancelled ||
                        phase == NSGestureRecognizerStateFailed;
    BOOL activateRawAfterPush = NO;
    BOOL useRawContacts = NO;
    int touch1X = 0;
    int touch1Y = 0;
    int touch2X = 0;
    int touch2Y = 0;

    @synchronized(self) {
        if (phase == NSGestureRecognizerStateBegan) {
            _gestureBaseX = focalX;
            _gestureBaseY = focalY;
            _gestureWindowWidth = windowWidth;
            _gestureWindowHeight = windowHeight;
            if (_hasRawCentroid) {
                _rawStartX = _rawCentroidX;
                _rawStartY = _rawCentroidY;
                activateRawAfterPush = YES;
            }
        }

        if (activateRawAfterPush || _rawPinchActive) {
            const MacOSMagnifyPoint touch1 = mapMacOSTrackpadContact(
                _rawContact1X, _rawContact1Y, _rawStartX, _rawStartY,
                _gestureBaseX, _gestureBaseY, _gestureWindowWidth, _gestureWindowHeight);
            const MacOSMagnifyPoint touch2 = mapMacOSTrackpadContact(
                _rawContact2X, _rawContact2Y, _rawStartX, _rawStartY,
                _gestureBaseX, _gestureBaseY, _gestureWindowWidth, _gestureWindowHeight);
            touch1X = touch1.x;
            touch1Y = touch1.y;
            touch2X = touch2.x;
            touch2Y = touch2.y;
            focalX = (touch1X + touch2X) / 2;
            focalY = (touch1Y + touch2Y) / 2;
            touchCount = _rawTouchCount;
            useRawContacts = YES;
        }

        // Stop raw-frame events before queuing the UP/CANCEL event, otherwise
        // a background callback could enqueue a MOVE after the contacts lift.
        if (ending) {
            _rawPinchActive = NO;
        }
    }

    const float magnification = (float)recognizer.magnification;
    recognizer.magnification = 0.0;

    // Raw contact frames are the authoritative geometry while available. The
    // recognizer is retained for gesture begin/end and as a fallback on Macs
    // where the private contact API cannot be loaded.
    if (!_rawPinchActive || phase != NSGestureRecognizerStateChanged) {
        [self pushMagnification:magnification
                             x:focalX
                             y:focalY
                    touchCount:touchCount
                         phase:phase
                  rawContact1X:touch1X
                  rawContact1Y:touch1Y
                  rawContact2X:touch2X
                  rawContact2Y:touch2Y
                 hasRawContacts:useRawContacts];
    }

    // Start raw-frame events only after the DOWN event is in the SDL queue.
    if (activateRawAfterPush) {
        @synchronized(self) {
            _rawPinchActive = YES;
        }
    }
}

- (void)detach
{
    @synchronized(self) {
        _rawPinchActive = NO;
    }

    if (_multitouchDevices != nullptr && _stopDevice != nullptr && _unregisterCallback != nullptr) {
        const CFIndex deviceCount = CFArrayGetCount(_multitouchDevices);
        for (CFIndex i = 0; i < deviceCount; i++) {
            MTDeviceRef device = CFArrayGetValueAtIndex(_multitouchDevices, i);
            _stopDevice(device);
            _unregisterCallback(device, rawTouchCallback);
        }
        CFRelease(_multitouchDevices);
        _multitouchDevices = nullptr;
    }

    if (_multitouchFramework != nullptr) {
        dlclose(_multitouchFramework);
        _multitouchFramework = nullptr;
    }

    if (_recognizer != nil) {
        [_contentView removeGestureRecognizer:_recognizer];
    }
}

- (void)dealloc
{
    [_recognizer release];
    [super dealloc];
}

@end

void* installMacOSMagnifyMonitor(SDL_Window* window, Uint32 sdlEventType)
{
    SDL_SysWMinfo windowInfo;
    SDL_VERSION(&windowInfo.version);

    if (!SDL_GetWindowWMInfo(window, &windowInfo) || windowInfo.subsystem != SDL_SYSWM_COCOA) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to install native macOS pinch recognizer: %s",
                    SDL_GetError());
        return nullptr;
    }

    NSView* contentView = windowInfo.info.cocoa.window.contentView;
    if (contentView == nil) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to install native macOS pinch recognizer: no content view");
        return nullptr;
    }

    MoonlightMagnifyBridge* bridge = [[MoonlightMagnifyBridge alloc]
        initWithSDLWindow:window
              contentView:contentView
                eventType:sdlEventType];

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Installed native macOS trackpad pinch recognizer");
    return (void*)bridge;
}

void removeMacOSMagnifyMonitor(void* token)
{
    if (token == nullptr) {
        return;
    }

    MoonlightMagnifyBridge* bridge = (MoonlightMagnifyBridge*)token;
    [bridge detach];
    [bridge release];
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Removed native macOS trackpad pinch recognizer");
}
