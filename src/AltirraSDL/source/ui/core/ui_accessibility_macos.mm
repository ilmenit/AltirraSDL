// Publish the emulated display as a native VoiceOver text-area element.
#include <stdafx.h>
#include "ui_accessibility.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#import <Cocoa/Cocoa.h>

@interface ATSDLAccessibleScreen : NSAccessibilityElement {
	std::shared_ptr<const ATSDLAccessibilitySnapshot> _snapshot;
	NSView *_view;
	NSRange _selection;
	std::recursive_mutex _mutex;
}
- (instancetype)initWithView:(NSView *)view;
- (void)publish:(std::shared_ptr<const ATSDLAccessibilitySnapshot>)snapshot;
@end

@implementation ATSDLAccessibleScreen
- (instancetype)initWithView:(NSView *)view {
	self = [super init];
	if (self) {
		_view = [view retain];
		_selection = NSMakeRange(0, 0);
		[self setAccessibilityParent:view];
		[self setAccessibilityRole:NSAccessibilityTextAreaRole];
		[self setAccessibilityLabel:@"Screen"];
		[self setAccessibilityHelp:@"Emulated Atari text display"];
	}
	return self;
}
- (void)publish:(std::shared_ptr<const ATSDLAccessibilitySnapshot>)snapshot {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	auto previous = _snapshot;
	_snapshot = std::move(snapshot);
	_selection.location = std::min(_selection.location, (NSUInteger)_snapshot->text.size());
	_selection.length = std::min(_selection.length, (NSUInteger)_snapshot->text.size() - _selection.location);
	if (!previous || previous->text != _snapshot->text)
		NSAccessibilityPostNotification(self, NSAccessibilityValueChangedNotification);
	if (_snapshot->focused && (!previous || !previous->focused))
		NSAccessibilityPostNotification(self, NSAccessibilityFocusedUIElementChangedNotification);
}
- (BOOL)isAccessibilityElement {
	std::lock_guard<std::recursive_mutex> lock(_mutex); return YES; }
- (BOOL)isAccessibilityEnabled {
	std::lock_guard<std::recursive_mutex> lock(_mutex); return YES; }
- (BOOL)isAccessibilityFocused {
	std::lock_guard<std::recursive_mutex> lock(_mutex); return _snapshot && _snapshot->focused; }
- (BOOL)isAccessibilitySelectorAllowed:(SEL)selector {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	if (selector == @selector(setAccessibilityValue:)) return NO;
	return [super isAccessibilitySelectorAllowed:selector];
}
- (NSString *)accessibilityValue {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	if (!_snapshot) return @"";
	return [[[NSString alloc] initWithBytes:_snapshot->text.data()
		length:_snapshot->text.size() * sizeof(wchar_t)
		encoding:NSUTF32LittleEndianStringEncoding] autorelease];
}
- (NSRect)screenRect:(NSRect)rect {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	// SDL coordinates are top-left; Cocoa content views need not be flipped.
	if (![_view isFlipped])
		rect.origin.y = [_view bounds].size.height - NSMaxY(rect);
	return [[_view window] convertRectToScreen:[_view convertRect:rect toView:nil]];
}
- (NSRect)accessibilityFrame {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	if (!_snapshot) return NSZeroRect;
	return [self screenRect:NSMakeRect(_snapshot->x, _snapshot->y, _snapshot->width, _snapshot->height)];
}
- (NSInteger)accessibilityNumberOfCharacters {
	std::lock_guard<std::recursive_mutex> lock(_mutex); return [[self accessibilityValue] length]; }
- (NSRange)accessibilityVisibleCharacterRange {
	std::lock_guard<std::recursive_mutex> lock(_mutex); return NSMakeRange(0, [self accessibilityNumberOfCharacters]); }
- (NSString *)accessibilitySelectedText {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	return [[self accessibilityValue] substringWithRange:_selection];
}
- (NSRange)accessibilitySelectedTextRange {
	std::lock_guard<std::recursive_mutex> lock(_mutex); return _selection; }
- (NSArray *)accessibilitySelectedTextRanges {
	std::lock_guard<std::recursive_mutex> lock(_mutex); return @[[NSValue valueWithRange:_selection]]; }
- (void)setAccessibilitySelectedTextRange:(NSRange)range {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	NSUInteger count = [self accessibilityNumberOfCharacters];
	if (range.location > count || range.length > count - range.location) return;
	_selection = range;
	NSAccessibilityPostNotification(self, NSAccessibilitySelectedTextChangedNotification);
}
- (void)setAccessibilitySelectedTextRanges:(NSArray *)ranges {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	if ([ranges count]) [self setAccessibilitySelectedTextRange:[[ranges firstObject] rangeValue]];
}
- (NSString *)accessibilityStringForRange:(NSRange)range {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	NSString *text = [self accessibilityValue];
	if (range.location > [text length] || range.length > [text length] - range.location) return nil;
	return [text substringWithRange:range];
}
- (NSInteger)accessibilityLineForIndex:(NSInteger)index {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	if (!_snapshot || index < 0 || index > (NSInteger)_snapshot->text.size()) return NSNotFound;
	NSInteger row = 0;
	for (const auto& line : _snapshot->lines) {
		if (index <= line.offset + line.length) return row;
		++row;
	}
	return _snapshot->lines.empty() ? 0 : row - 1;
}
- (NSRange)accessibilityRangeForLine:(NSInteger)row {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	if (!_snapshot || row < 0 || row >= (NSInteger)_snapshot->lines.size()) return NSMakeRange(NSNotFound, 0);
	const auto& line = _snapshot->lines[row];
	return NSMakeRange(line.offset, line.length);
}
- (NSRange)accessibilityRangeForIndex:(NSInteger)index {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	if (index < 0 || index >= [self accessibilityNumberOfCharacters]) return NSMakeRange(NSNotFound, 0);
	return NSMakeRange(index, 1);
}
- (NSRect)accessibilityFrameForRange:(NSRange)range {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	if (!_snapshot || range.location > _snapshot->text.size()
		|| range.length > _snapshot->text.size() - range.location) return NSZeroRect;
	NSRect result = NSZeroRect;
	BOOL first = YES;
	for (const auto& line : _snapshot->lines) {
		const bool caret = !range.length;
		if (caret ? (range.location < (NSUInteger)line.offset || range.location > (NSUInteger)(line.offset + line.length))
			: (range.location >= (NSUInteger)(line.offset + line.length) || NSMaxRange(range) <= (NSUInteger)line.offset)) continue;
		float cell = line.length ? line.width / line.length : 0;
		int left = std::clamp((int)range.location - line.offset, 0, line.length);
		int right = std::clamp((int)NSMaxRange(range) - line.offset, 0, line.length);
		NSRect rect = [self screenRect:NSMakeRect(line.x + left * cell, line.y, (right - left) * cell, line.height)];
		result = first ? rect : NSUnionRect(result, rect);
		first = NO;
		if (caret) break;
	}
	return result;
}
- (NSRange)accessibilityRangeForPosition:(NSPoint)point {
	std::lock_guard<std::recursive_mutex> lock(_mutex);
	if (!_snapshot) return NSMakeRange(NSNotFound, 0);
	for (const auto& line : _snapshot->lines) {
		NSRect rect = [self screenRect:NSMakeRect(line.x, line.y, line.width, line.height)];
		if (point.y >= NSMinY(rect) && point.y < NSMaxY(rect)) {
			double column = line.width > 0 ? (point.x - rect.origin.x) * line.length / line.width : 0;
			if (!std::isfinite(column)) return NSMakeRange(NSNotFound, 0);
			return NSMakeRange(line.offset + (int)std::clamp(column, 0.0, (double)line.length), 0);
		}
	}
	return NSMakeRange(NSNotFound, 0);
}
- (void)dealloc {
	[_view release];
	[super dealloc];
}
@end

namespace {
	ATSDLAccessibleScreen *g_element;
	NSView *g_view;
	NSArray *g_previousChildren;
}

void ATSDLAccessibilityPlatformUpdate(SDL_Window *window,
	std::shared_ptr<const ATSDLAccessibilitySnapshot> snapshot) {
	@autoreleasepool {
		NSWindow *native = (NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
			SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
		NSView *view = [native contentView];
		if (!view) {
			ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Unavailable,
				"VoiceOver support requires a native Cocoa desktop window.");
			return;
		}
		if (g_view && g_view != view) ATSDLAccessibilityPlatformShutdown();
		if (!g_element) {
			g_view = [view retain];
			g_previousChildren = [[view accessibilityChildren] copy];
			g_element = [[ATSDLAccessibleScreen alloc] initWithView:view];
			NSMutableArray *children = [NSMutableArray arrayWithArray:g_previousChildren ?: @[]];
			[children addObject:g_element];
			[view setAccessibilityChildren:children];
			NSAccessibilityPostNotification(view, NSAccessibilityLayoutChangedNotification);
		}
		[g_element publish:std::move(snapshot)];
		ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Active,
			"VoiceOver text accessibility is active.");
	}
}

void ATSDLAccessibilityPlatformShutdown() {
	@autoreleasepool {
		if (!g_element) return;
		[g_view setAccessibilityChildren:g_previousChildren];
		NSAccessibilityPostNotification(g_element, NSAccessibilityUIElementDestroyedNotification);
		[g_element setAccessibilityParent:nil];
		[g_element release]; g_element = nil;
		[g_previousChildren release]; g_previousChildren = nil;
		[g_view release]; g_view = nil;
	}
}
