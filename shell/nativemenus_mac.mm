/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "nativemenus_mac.h"

#import <AppKit/AppKit.h>

namespace
{
void filterMenu(NSMenu *menu)
{
    // AppKit adds these items after Qt creates its QAction menu model.
    // Hide native items directly, keeping Qt's menu indices intact.
    static bool filtering = false;
    if (!menu || filtering) {
        return;
    }
    filtering = true;
    for (NSMenuItem *item in menu.itemArray) {
        NSString *action = item.action ? NSStringFromSelector(item.action).lowercaseString : @"";
        NSString *title = item.title.lowercaseString;
        const bool unwanted = [action containsString:@"dictation"] || [action containsString:@"autofill"]
            || [title hasPrefix:@"start dictation"] || [title hasPrefix:@"autofill"]
            || [title hasPrefix:@"avvia dettatura"] || [title isEqualToString:@"riempimento automatico"]
            || [title isEqualToString:@"compilazione automatica"]
            || (item.submenu && item.submenu == NSApp.servicesMenu);
        if (unwanted && !item.hidden) {
            item.hidden = YES;
        }
    }
    filtering = false;
    for (NSMenuItem *item in menu.itemArray) {
        if (item.submenu && !item.hidden) {
            filterMenu(item.submenu);
        }
    }
}
}

void installMobiDocNativeMenuFilter()
{
    // Retain observers for this application's lifetime only. No system preferences change.
    static NSArray<id> *observers;
    if (observers) {
        return;
    }
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    void (^filter)(NSNotification *) = ^(NSNotification *notification) {
        if ([notification.object isKindOfClass:NSMenu.class]) {
            filterMenu(notification.object);
        }
    };
    observers = @[
        [center addObserverForName:NSMenuDidAddItemNotification object:nil queue:nil usingBlock:filter],
        [center addObserverForName:NSMenuDidChangeItemNotification object:nil queue:nil usingBlock:filter],
        [center addObserverForName:NSMenuDidBeginTrackingNotification object:nil queue:nil usingBlock:filter]
    ];
    filterMenu(NSApp.mainMenu);
}
