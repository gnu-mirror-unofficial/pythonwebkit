/*
 * Copyright (C) 2005, 2006 Apple Computer, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer. 
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution. 
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// This header contains WebFrame declarations that can be used anywhere in WebKit, but are neither SPI nor API.

#import "WebFramePrivate.h"

#ifdef __cplusplus
#import <WebCore/FrameLoaderTypes.h>
#endif

@class DOMCSSStyleDeclaration;
@class DOMElement;
@class DOMNode;
@class DOMRange;
@class WebInspector;
@class WebFrameView;
@class WebFrameBridge;
@class WebHistoryItem;
@class WebScriptDebugger;

#ifdef __cplusplus

namespace WebCore {
    class CSSStyleDeclaration;
    class Document;
    class DocumentLoader;
    class Element;
    class Frame;
    class FrameMac;
    class FrameLoader;
    class HTMLElement;
    class Node;
    class Page;
    class Range;
}

WebCore::CSSStyleDeclaration* core(DOMCSSStyleDeclaration *);
DOMCSSStyleDeclaration *kit(WebCore::CSSStyleDeclaration*);

WebCore::FrameMac* core(WebFrame *);
WebFrame *kit(WebCore::Frame *);

WebCore::Element* core(DOMElement *);
DOMElement *kit(WebCore::Element*);

WebCore::Node* core(DOMNode *);
DOMNode *kit(WebCore::Node*);

WebCore::Document* core(DOMDocument *);
DOMDocument *kit(WebCore::Document*);

WebCore::HTMLElement* core(DOMHTMLElement *);
DOMHTMLElement *kit(WebCore::HTMLElement*);

WebCore::Range* core(DOMRange *);
DOMRange *kit(WebCore::Range*);

WebCore::Page* core(WebView *);
WebView *kit(WebCore::Page*);

WebView *getWebView(WebFrame *webFrame);

@interface WebFramePrivate : NSObject
{
@public
    WebFrameView *webFrameView;

    WebFrameBridge *bridge;
    WebHistoryItem *currentItem;        // BF item for our current content
    WebHistoryItem *provisionalItem;    // BF item for where we're trying to go
                                        // (only known when navigating to a pre-existing BF item)
    WebHistoryItem *previousItem;       // BF item for previous content, see _itemForSavingDocState

    WebScriptDebugger *scriptDebugger;
    id internalLoadDelegate;
    
    NSMutableSet *plugInViews;
    NSMutableSet *inspectors;
}
@end

#endif

@interface WebFrame (WebInternal)

- (void)_updateBackground;
- (void)_setInternalLoadDelegate:(id)internalLoadDelegate;
- (id)_internalLoadDelegate;
#if !BUILDING_ON_TIGER
- (void)_unmarkAllBadGrammar;
#endif
- (void)_unmarkAllMisspellings;
// Note that callers should not perform any ops on these views that could change the set of frames
- (NSArray *)_documentViews;

- (BOOL)_hasSelection;
- (void)_clearSelection;
- (WebFrame *)_findFrameWithSelection;
- (void)_clearSelectionInOtherFrames;
#ifdef __cplusplus
- (id)_initWithWebFrameView:(WebFrameView *)fv webView:(WebView *)v coreFrame:(WebCore::Frame*)coreFrame;
#endif

- (void)_addPlugInView:(NSView *)plugInView;

- (BOOL)_isMainFrame;

- (void)_addInspector:(WebInspector *)inspector;
- (void)_removeInspector:(WebInspector *)inspector;

#ifdef __cplusplus

- (WebCore::FrameLoader*)_frameLoader;
- (WebDataSource *)_dataSourceForDocumentLoader:(WebCore::DocumentLoader*)loader;

- (void)_addDocumentLoader:(WebCore::DocumentLoader*)loader toUnarchiveState:(WebArchive *)archive;

#endif

- (NSURLRequest *)_webDataRequestForData:(NSData *)data MIMEType:(NSString *)MIMEType textEncodingName:(NSString *)encodingName baseURL:(NSURL *)URL unreachableURL:(NSURL *)unreachableURL;

- (WebFrameBridge *)_bridge;

#ifdef __cplusplus
- (void)_goToItem:(WebHistoryItem *)item withLoadType:(WebCore::FrameLoadType)type;
#endif
- (void)_loadURL:(NSURL *)URL referrer:(NSString *)referrer intoChild:(WebFrame *)childFrame;

- (void)_viewWillMoveToHostWindow:(NSWindow *)hostWindow;
- (void)_viewDidMoveToHostWindow;

- (void)_addChild:(WebFrame *)child;

- (WebHistoryItem *)_itemForSavingDocState;
- (WebHistoryItem *)_itemForRestoringDocState;

- (void)_saveDocumentAndScrollState;

+ (CFAbsoluteTime)_timeOfLastCompletedLoad;
- (BOOL)_canCachePage;
- (void)_purgePageCache;

- (int)_numPendingOrLoadingRequests:(BOOL)recurse;

- (void)_reloadForPluginChanges;

- (void)_attachScriptDebugger;
- (void)_detachScriptDebugger;

- (void)_recursive_pauseNullEventsForAllNetscapePlugins;
- (void)_recursive_resumeNullEventsForAllNetscapePlugins;

- (void)_setProvisionalItem:(WebHistoryItem *)item;
- (void)_setPreviousItem:(WebHistoryItem *)item;
- (void)_setCurrentItem:(WebHistoryItem *)item;

- (void)_saveScrollPositionAndViewStateToItem:(WebHistoryItem *)item;

- (void)_addBackForwardItemClippedAtTarget:(BOOL)doClip;

- (WebHistoryItem *)_createItem:(BOOL)useOriginal;

@end

@interface NSObject (WebInternalFrameLoadDelegate)
- (void)webFrame:(WebFrame *)webFrame didFinishLoadWithError:(NSError *)error;
@end
