// -*- mode:objc -*-
/*
 **  LineBuffer.h
 **
 **  Copyright (c) 2002, 2003
 **
 **  Author: George Nachman
 **
 **  Project: iTerm
 **
 **  Description: Implements a buffer of lines. It can hold a large number
 **   of lines and can quickly format them to a fixed width.
 **
 **  This program is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 2 of the License, or
 **  (at your option) any later version.
 **
 **  This program is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with this program; if not, write to the Free Software
 **  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#import <Cocoa/Cocoa.h>
#import "FindContext.h"
#import "iTermEncoderAdapter.h"
#import "iTermFindDriver.h"
#import "ScreenCharArray.h"
#import "LineBufferPosition.h"
#import "LineBufferHelpers.h"
#import "VT100GridTypes.h"

@class LineBlock;
@class LineBuffer;
@class ResultRange;

@protocol iTermLineBufferDelegate<NSObject>
- (void)lineBufferDidDropLines:(LineBuffer * _Nonnull)lineBuffer;
@end

// A LineBuffer represents an ordered collection of strings of screen_char_t. Each string forms a
// logical line of text plus color information. Logic is provided for the following major functions:
//   - If the lines are wrapped onto a screen of some width, find the Nth wrapped line
//   - Append a line
//   - If the lines are wrapped onto a screen of some width, remove the last wrapped line
//   - Store and retrieve a cursor position
//   - Store an unlimited or a fixed number of wrapped lines
// The implementation uses an array of small blocks that hold a few kb of unwrapped lines. Each
// block caches some information to speed up repeated lookups with the same screen width.
@interface LineBuffer : NSObject <NSCopying>

@property(nonatomic, assign) BOOL mayHaveDoubleWidthCharacter;

// Absolute block number of last block.
@property(nonatomic, readonly) int largestAbsoluteBlockNumber;
@property(nonatomic, weak, nullable) id<iTermLineBufferDelegate> delegate;

- (LineBuffer * _Nonnull)initWithBlockSize:(int)bs;
- (LineBuffer * _Nullable)initWithDictionary:(NSDictionary * _Nonnull)dictionary;

// Returns a copy of this buffer that can be appended to but that you must not
// pop lines from. Only the last block is deep-copied; references are held to
// all earlier blocks.
- (LineBuffer * _Nonnull)newAppendOnlyCopy;

// Call this immediately after init. Otherwise the buffer will hold unlimited lines (until you
// run out of memory).
- (void)setMaxLines:(int)maxLines;

// Add a line to the buffer. Set partial to true if there's more coming for this line:
// that is to say, this buffer contains only a prefix or infix of the entire line.
//
// NOTE: call dropExcessLinesWithWidth after this if you want to limit the buffer to max_lines.
- (void)appendLine:(screen_char_t * _Nonnull)buffer
            length:(int)length
           partial:(BOOL)partial
             width:(int)width
          metadata:(iTermMetadata)metadata
      continuation:(screen_char_t)continuation;

- (void)appendContentsOfLineBuffer:(LineBuffer * _Nonnull)other width:(int)width;

// If more lines are in the buffer than max_lines, call this function. It will adjust the count
// of excess lines and try to free the first block(s) if they are unused. Because this could happen
// after any call to appendLines, you should always call this after appendLines.
//
// Returns the number of lines in the buffer that overflowed max_lines.
//
// NOTE: This invalidates the cursor position.
- (int)dropExcessLinesWithWidth:(int)width;

// Returns the metadata associated with a line when wrapped to the specified width.
- (iTermMetadata)metadataForLineNumber:(int)lineNum width:(int)width;

// Metadata for the whole raw line.
- (iTermMetadata)metadataForRawLineWithWrappedLineNumber:(int)lineNum width:(int)width;

- (NSInteger)generationForLineNumber:(int)lineNum width:(int)width;

// Copy a line into the buffer. If the line is shorter than 'width' then only the first 'width'
// characters will be modified.
// 0 <= lineNum < numLinesWithWidth:width
// Returns EOL code.
// DEPRECATED, use wrappedLineAtIndex:width: instead.
- (int)copyLineToBuffer:(screen_char_t * _Nonnull)buffer
                  width:(int)width
                lineNum:(int)lineNum
           continuation:(screen_char_t * _Nullable)continuationPtr;

- (void)enumerateLinesInRange:(NSRange)range
                        width:(int)width
                        block:(void (^ _Nonnull)(int,
                                                 ScreenCharArray * _Nonnull,
                                                 iTermMetadata,
                                                 BOOL * _Nonnull))block;

// Like the above but with a saner way of holding the returned data. Callers are advised not
// to modify the screen_char_t's returned, but the ScreenCharArray is otherwise safe to
// mutate. |continuation| is optional and if set will be filled in with the continuation character.
- (ScreenCharArray * _Nonnull)wrappedLineAtIndex:(int)lineNum
                                           width:(int)width
                                    continuation:(screen_char_t * _Nullable)continuation;

- (ScreenCharArray * _Nonnull)rawLineAtWrappedLine:(int)lineNum width:(int)width;

// This is the fast way to get a bunch of lines at once.
- (NSArray<ScreenCharArray *> * _Nonnull)wrappedLinesFromIndex:(int)lineNum
                                                         width:(int)width
                                                         count:(int)count;

// Copy up to width chars from the last line into *ptr. The last line will be removed or
// truncated from the buffer. Sets *includesEndOfLine to true if this line should have a
// continuation marker.
- (BOOL)popAndCopyLastLineInto:(screen_char_t * _Nonnull)ptr
                         width:(int)width
             includesEndOfLine:(int *_Nonnull)includesEndOfLine
                      metadata:(out iTermMetadata * _Nullable)metadataPtr
                  continuation:(screen_char_t * _Nullable)continuationPtr;

// Removes the last wrapped lines.
- (void)removeLastWrappedLines:(int)numberOfLinesToRemove
                         width:(int)width;

// Remove the last raw (unwrapped) line.
- (void)removeLastRawLine;

// Get the number of buffer lines at a given width.
- (int)numLinesWithWidth:(int)width;

// Save the cursor position. Call this just before appending the line the cursor is in.
// x gives the offset from the start of the next line appended. The cursor position is
// invalidated if dropExcessLinesWithWidth is called.
- (void)setCursor:(int)x;

// If the last wrapped line has the cursor, return true and set *x to its horizontal position.
// 0 <= *x <= width (if *x == width then the cursor is actually on the next line).
// Call this just before popAndCopyLastLineInto:width:includesEndOfLine:metadata:continuation.
- (BOOL)getCursorInLastLineWithWidth:(int)width atX:(int * _Nonnull)x;

// Print the raw lines to the console for debugging.
- (void)dump;

// Set up the find context. See FindContext.h for options bit values.
- (void)prepareToSearchFor:(NSString * _Nonnull)substring
                startingAt:(LineBufferPosition * _Nonnull)start
                   options:(FindOptions)options
                      mode:(iTermFindMode)findMode
               withContext:(FindContext * _Nonnull)context;

// Performs a search. Use prepareToSearchFor:startingAt:options:withContext: to initialize
// the FindContext prior to calling this.
- (void)findSubstring:(FindContext * _Nonnull)context stopAt:(LineBufferPosition * _Nonnull)stopAt;

// Returns an array of XYRange values
- (NSArray<XYRange *> * _Nonnull)convertPositions:(NSArray<ResultRange *> * _Nonnull)resultRanges
                                        withWidth:(int)width;

- (LineBufferPosition * _Nullable)positionForCoordinate:(VT100GridCoord)coord
                                                  width:(int)width
                                                 offset:(int)offset;

- (VT100GridCoord)coordinateForPosition:(LineBufferPosition * _Nonnull)position
                                  width:(int)width
                           extendsRight:(BOOL)extendsRight
                                     ok:(BOOL * _Nullable)ok;

- (LineBufferPosition * _Nonnull)firstPosition;
- (LineBufferPosition * _Nonnull)lastPosition;
- (LineBufferPosition * _Nonnull)positionForStartOfLastLine;

// Convert the block,offset in a findcontext into an absolute position.
- (long long)absPositionOfFindContext:(FindContext * _Nonnull)findContext;
// Convert an absolute position into a position.
- (int)positionForAbsPosition:(long long)absPosition;
// Convert a position into an absolute position.
- (long long)absPositionForPosition:(int)pos;

// Set the start location of a find context to an absolute position.
- (void)storeLocationOfAbsPos:(long long)absPos
                    inContext:(FindContext * _Nonnull)context;

- (NSString * _Nonnull)debugString;
- (void)dumpWrappedToWidth:(int)width;
- (NSString * _Nonnull)compactLineDumpWithWidth:(int)width andContinuationMarks:(BOOL)continuationMarks;

- (int)numberOfDroppedBlocks;

// Returns a dictionary with the contents of the line buffer. If it is more than 10k lines @ 80 columns
// then it is truncated. The data is a weak reference and will be invalid if the line buffer is
// changed.
//- (NSDictionary *)dictionary;
- (void)encode:(id<iTermEncoderAdapter> _Nonnull)encoder maxLines:(NSInteger)maxLines;

// Append text in reverse video to the end of the line buffer.
- (void)appendMessage:(NSString * _Nonnull)message;

// Make a copy of the last |minLines| at width |width|. May copy more than |minLines| for speed.
- (LineBuffer * _Nonnull)appendOnlyCopyWithMinimumLines:(int)minLines atWidth:(int)width;

- (int)numberOfWrappedLinesWithWidth:(int)width;

- (void)beginResizing;
- (void)endResizing;

- (void)setPartial:(BOOL)partial;

// Tests only!
- (LineBlock * _Nonnull)internalBlockAtIndex:(int)i;

@end
