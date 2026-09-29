// macOS-specific application setup: application defaults and the States
// menu in the menu bar.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include "port/savestate.hpp"

#include <SDL3/SDL.h>

#include <string>

// Target of the States menu's items; keeps the slot titles up to date.
@interface SMGStatesMenuTarget : NSObject <NSMenuDelegate>
@end

@implementation SMGStatesMenuTarget

- (void)saveState:(NSMenuItem*)item {
    port::savestate::requestSave(static_cast<int>(item.tag));
}

- (void)loadState:(NSMenuItem*)item {
    port::savestate::requestLoad(static_cast<int>(item.tag));
}

- (void)menuNeedsUpdate:(NSMenu*)menu {
    for (NSMenuItem* item in menu.itemArray) {
        const std::string saved = port::savestate::slotDescription(static_cast<int>(item.tag));
        NSString* when = saved.empty() ? @"Empty" : [NSString stringWithUTF8String:saved.c_str()];
        item.title = [NSString stringWithFormat:@"Slot %ld — %@", static_cast<long>(item.tag), when];
    }
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    if (item.action == @selector(loadState:)) {
        return !port::savestate::slotDescription(static_cast<int>(item.tag)).empty();
    }
    return YES;
}

@end

namespace port::macos {

namespace {
NSWindow* sWindow = nil;
NSString* sTitle = nil;
NSInteger sStatusGeneration = 0;
SMGStatesMenuTarget* sMenuTarget = nil;

NSMenu* slotMenu(NSString* title, SEL action, NSEventModifierFlags modifiers) {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
    menu.delegate = sMenuTarget;
    for (int slot = 1; slot <= port::savestate::kSlotCount; slot++) {
        NSString* key = [NSString stringWithFormat:@"%d", slot];
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"Slot %d", slot]
                                                      action:action
                                               keyEquivalent:key];
        item.keyEquivalentModifierMask = modifiers;
        item.target = sMenuTarget;
        item.tag = slot;
        [menu addItem:item];
        [item release];
    }
    return menu;
}
}  // namespace

void configureApplication() {
    @autoreleasepool {
        // Don't offer to restore windows after a crash: AppKit shows a modal
        // alert before the first event is delivered, which stalls startup.
        [[NSUserDefaults standardUserDefaults] registerDefaults:@{
            @"ApplePersistenceIgnoreState" : @YES,
            @"NSQuitAlwaysKeepsWindows" : @NO,
        }];
    }
}

// Adds States > Save State / Load State (slots 1-5) to the menu bar: Cmd+1-5
// loads, Shift+Cmd+1-5 saves.
void installSaveStateMenu(SDL_Window* window) {
    @autoreleasepool {
        if (window != nullptr) {
            sWindow = static_cast<NSWindow*>(
                SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr));
            sTitle = [sWindow.title copy];
        }
        NSMenu* mainMenu = NSApp.mainMenu;
        if (mainMenu == nil) {
            return;
        }
        sMenuTarget = [[SMGStatesMenuTarget alloc] init];

        NSMenu* states = [[NSMenu alloc] initWithTitle:@"States"];
        NSMenuItem* save = [[NSMenuItem alloc] initWithTitle:@"Save State" action:nil keyEquivalent:@""];
        save.submenu = slotMenu(@"Save State", @selector(saveState:),
                                NSEventModifierFlagCommand | NSEventModifierFlagShift);
        NSMenuItem* load = [[NSMenuItem alloc] initWithTitle:@"Load State" action:nil keyEquivalent:@""];
        load.submenu = slotMenu(@"Load State", @selector(loadState:), NSEventModifierFlagCommand);
        [states addItem:save];
        [states addItem:load];

        NSMenuItem* statesItem = [[NSMenuItem alloc] initWithTitle:@"States" action:nil keyEquivalent:@""];
        statesItem.submenu = states;
        // After the application menu.
        [mainMenu insertItem:statesItem atIndex:mainMenu.numberOfItems > 0 ? 1 : 0];
    }
}

// Shows a short message in the window title for a few seconds. Callable from
// any thread.
void showStatus(const std::string& message) {
    NSString* text = [[NSString alloc] initWithUTF8String:message.c_str()];
    dispatch_async(dispatch_get_main_queue(), ^{
      if (sWindow != nil) {
          sWindow.title = [NSString stringWithFormat:@"%@ — %@", sTitle != nil ? sTitle : @"", text];
          const NSInteger generation = ++sStatusGeneration;
          dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            if (generation == sStatusGeneration && sTitle != nil) {
                sWindow.title = sTitle;
            }
          });
      }
      [text release];
    });
}

}  // namespace port::macos
