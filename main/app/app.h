#pragma once

#include <lvgl.h>

class Display;

// Context passed to each app on entry
struct AppContext {
    Display* display = nullptr;
};

// Base class for all applications in the launcher
class App {
public:
    virtual ~App() = default;

    // Display name shown in the 3x3 grid
    virtual const char* GetName() const = 0;

    // Font Awesome icon shown in the 3x3 grid
    virtual const char* GetIcon() const = 0;

    // Called when the app becomes active.
    // 'screen' is a blank lv_obj_t* created by AppManager.
    // The app must build all its UI as children of 'screen'.
    virtual void OnEnter(AppContext& ctx, lv_obj_t* screen) = 0;

    // Called when the app is being left.
    // The app should stop timers, close connections, etc.
    // The screen will be deleted by AppManager after this returns.
    virtual void OnExit() = 0;

    // Called periodically (~1 Hz) from the main event loop.
    // Return true if the app needs a screen refresh.
    virtual bool OnUpdate() { return false; }

    // Swipe gesture from touch screen.
    // direction: 0=left, 1=right, 2=up, 3=down
    virtual void OnSwipeEvent(int direction) {}

    // Check if the app can be entered (e.g. daily limit not exceeded).
    // Default true; override to restrict entry.
    virtual bool CanEnter() const { return true; }
};
