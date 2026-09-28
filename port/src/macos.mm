// macOS-specific application setup.

#import <Foundation/Foundation.h>

namespace port::macos {

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

}  // namespace port::macos
