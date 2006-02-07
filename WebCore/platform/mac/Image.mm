/*
 * Copyright (C) 2004, 2005, 2006 Apple Computer, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#import "config.h"
#import <kxmlcore/Vector.h>
#import "Array.h"
#import "IntSize.h"
#import "FloatRect.h"
#import "Image.h"
#import "PDFDocumentImage.h"
#import <qstring.h>

#import "WebCoreImageRendererFactory.h"

namespace WebCore {

void FrameData::clear()
{
    if (m_frame) {
        CFRelease(m_frame);
        m_frame = 0;
        m_duration = 0.;
    }
}

// ================================================
// Image Class
// ================================================

void Image::initNativeData()
{
    m_nsImage = 0;
    m_tiffRep = 0;
    m_solidColor = 0;
    m_isSolidColor = 0;
    m_isPDF = false;
    m_PDFDoc = 0;
}

void Image::destroyNativeData()
{
    delete m_PDFDoc;
}

void Image::invalidateNativeData()
{
    if (m_frames.size() != 1)
        return;

    if (m_nsImage) {
        [m_nsImage release];
        m_nsImage = 0;
    }

    if (m_tiffRep) {
        CFRelease(m_tiffRep);
        m_tiffRep = 0;
    }

    m_isSolidColor = false;
    if (m_solidColor) {
        CFRelease(m_solidColor);
        m_solidColor = 0;
    }
}

Image* Image::loadResource(const char *name)
{
    NSData* namedImageData = [[WebCoreImageRendererFactory sharedFactory] imageDataForName:[NSString stringWithCString:name]];
    if (namedImageData) {
        Image* image = new Image;
        image->setNativeData((CFDataRef)namedImageData, true);
        return image;
    }
    return 0;
}

bool Image::supportsType(const QString& type)
{
    return [[[WebCoreImageRendererFactory sharedFactory] supportedMIMETypes] containsObject:type.getNSString()];
}

// Drawing Routines
static CGContextRef graphicsContext(void* context)
{
    if (!context)
        return (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];
    return (CGContextRef)context;
}

static void setCompositingOperation(CGContextRef context, Image::CompositeOperator op)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [[NSGraphicsContext graphicsContextWithGraphicsPort:context flipped:NO] setCompositingOperation:(NSCompositingOperation)op];
    [pool release];
}

static void fillSolidColorInRect(CGColorRef color, CGRect rect, Image::CompositeOperator op, CGContextRef context)
{
    if (color) {
        CGContextSaveGState(context);
        CGContextSetFillColorWithColor(context, color);
        setCompositingOperation(context, op);
        CGContextFillRect(context, rect);
        CGContextRestoreGState(context);
    }
}

void Image::checkForSolidColor(CGImageRef image)
{
    m_isSolidColor = false;
    if (m_solidColor) {
        CFRelease(m_solidColor);
        m_solidColor = 0;
    }
    
    // Currently we only check for solid color in the important special case of a 1x1 image.
    if (image && CGImageGetWidth(image) == 1 && CGImageGetHeight(image) == 1) {
        float pixel[4]; // RGBA
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
// This #if won't be needed once the CG header that includes kCGBitmapByteOrder32Host is included in the OS.
#if __ppc__
        CGContextRef bmap = CGBitmapContextCreate(&pixel, 1, 1, 8*sizeof(float), sizeof(pixel), space,
            kCGImageAlphaPremultipliedLast | kCGBitmapFloatComponents);
#else
        CGContextRef bmap = CGBitmapContextCreate(&pixel, 1, 1, 8*sizeof(float), sizeof(pixel), space,
            kCGImageAlphaPremultipliedLast | kCGBitmapFloatComponents | kCGBitmapByteOrder32Host);
#endif
        if (bmap) {
            setCompositingOperation(bmap, Image::CompositeCopy);
            
            CGRect dst = { {0, 0}, {1, 1} };
            CGContextDrawImage(bmap, dst, image);
            if (pixel[3] > 0)
                m_solidColor = CGColorCreate(space,pixel);
            m_isSolidColor = true;
            CFRelease(bmap);
        } 
        CFRelease(space);
    }
}

CFDataRef Image::getTIFFRepresentation()
{
    if (m_tiffRep)
        return m_tiffRep;

    CGImageRef cgImage = frameAtIndex(0);
    if (!cgImage)
        return 0;
   
    CFMutableDataRef data = CFDataCreateMutable(0, 0);
    // FIXME:  Use type kCGImageTypeIdentifierTIFF constant once is becomes available in the API
    CGImageDestinationRef destination = CGImageDestinationCreateWithData(data, CFSTR("public.tiff"), 1, 0);
    if (destination) {
        CGImageDestinationAddImage(destination, cgImage, 0);
        CGImageDestinationFinalize(destination);
        CFRelease(destination);
    }

    m_tiffRep = data;
    return m_tiffRep;
}

NSImage* Image::getNSImage()
{
    if (m_nsImage)
        return m_nsImage;

    CFDataRef data = getTIFFRepresentation();
    if (!data)
        return 0;
    
    m_nsImage = [[NSImage alloc] initWithData:(NSData*)data];
    return m_nsImage;
}

CGImageRef Image::getCGImageRef()
{
    return frameAtIndex(0);
}

void Image::drawInRect(const FloatRect& dstRect, const FloatRect& srcRect,
                       CompositeOperator compositeOp, void* ctxt)
{
    CGContextRef context = graphicsContext(ctxt);
    if (m_isPDF) {
        if (m_PDFDoc)
            m_PDFDoc->draw(srcRect, dstRect, compositeOp, 1.0, true, context);
        return;
    } 
    
    if (!m_source.initialized())
        return;
    
    CGRect fr = srcRect;
    CGRect ir = dstRect;

    CGImageRef image = frameAtIndex(m_currentFrame);
    if (!image) // If it's too early we won't have an image yet.
        return;

    if (m_isSolidColor && m_currentFrame == 0)
        return fillSolidColorInRect(m_solidColor, ir, compositeOp, context);

    CGContextSaveGState(context);
        
    // Get the height (in adjusted, i.e. scaled, coords) of the portion of the image
    // that is currently decoded.  This could be less that the actual height.
    CGSize selfSize = size();                          // full image size, in pixels
    float curHeight = CGImageGetHeight(image);         // height of loaded portion, in pixels
    
    CGSize adjustedSize = selfSize;
    if (curHeight < selfSize.height) {
        adjustedSize.height *= curHeight / selfSize.height;

        // Is the amount of available bands less than what we need to draw?  If so,
        // we may have to clip 'fr' if it goes outside the available bounds.
        if (CGRectGetMaxY(fr) > adjustedSize.height) {
            float frHeight = adjustedSize.height - fr.origin.y; // clip fr to available bounds
            if (frHeight <= 0)
                return;                                             // clipped out entirely
            ir.size.height *= (frHeight / fr.size.height);    // scale ir proportionally to fr
            fr.size.height = frHeight;
        }
    }

    // Flip the coords.
    setCompositingOperation(context, compositeOp);
    CGContextTranslateCTM(context, ir.origin.x, ir.origin.y);
    CGContextScaleCTM(context, 1, -1);
    CGContextTranslateCTM(context, 0, -ir.size.height);
    
    // Translated to origin, now draw at 0,0.
    ir.origin.x = ir.origin.y = 0;
    
    // If we're drawing a sub portion of the image then create
    // a image for the sub portion and draw that.
    // Test using example site at http://www.meyerweb.com/eric/css/edge/complexspiral/demo.html
    if (fr.size.width != adjustedSize.width || fr.size.height != adjustedSize.height) {
        // Convert ft to image pixel coords:
        float xscale = adjustedSize.width / selfSize.width;
        float yscale = adjustedSize.height / curHeight;     // yes, curHeight, not selfSize.height!
        fr.origin.x /= xscale;
        fr.origin.y /= yscale;
        fr.size.width /= xscale;
        fr.size.height /= yscale;
        
        image = CGImageCreateWithImageInRect(image, fr);
        if (image) {
            CGContextDrawImage(context, ir, image);
            CFRelease(image);
        }
    } else // Draw the whole image.
        CGContextDrawImage(context, ir, image);

    CGContextRestoreGState(context);
    
    startAnimation();

}

static void drawPattern(void* info, CGContextRef context)
{
    Image* data = (Image*)info;
    CGImageRef image = data->frameAtIndex(data->currentFrame());
    float w = CGImageGetWidth(image);
    float h = CGImageGetHeight(image);
    CGContextDrawImage(context, CGRectMake(0, data->size().height() - h, w, h), image);    
}

static const CGPatternCallbacks patternCallbacks = { 0, drawPattern, NULL };

void Image::tileInRect(const FloatRect& dstRect, const FloatPoint& srcPoint, void* ctxt)
{    
    CGImageRef image = frameAtIndex(m_currentFrame);
    if (!image)
        return;

    CGContextRef context = graphicsContext(ctxt);

    if (m_currentFrame == 0 && m_isSolidColor)
        return fillSolidColorInRect(m_solidColor, dstRect, Image::CompositeSourceOver, context);

    CGSize tileSize = size();
    CGRect rect = dstRect;
    CGPoint point = srcPoint;

    // Check and see if a single draw of the image can cover the entire area we are supposed to tile.
    NSRect oneTileRect;
    oneTileRect.origin.x = rect.origin.x + fmodf(fmodf(-point.x, tileSize.width) - tileSize.width, tileSize.width);
    oneTileRect.origin.y = rect.origin.y + fmodf(fmodf(-point.y, tileSize.height) - tileSize.height, tileSize.height);
    oneTileRect.size.height = tileSize.height;
    oneTileRect.size.width = tileSize.width;

    // If the single image draw covers the whole area, then just draw once.
    if (NSContainsRect(oneTileRect, NSMakeRect(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height))) {
        CGRect fromRect;
        fromRect.origin.x = rect.origin.x - oneTileRect.origin.x;
        fromRect.origin.y = rect.origin.y - oneTileRect.origin.y;
        fromRect.size = rect.size;

        drawInRect(dstRect, FloatRect(fromRect), Image::CompositeSourceOver, context);
        return;
    }

    CGPatternRef pattern = CGPatternCreate(this, CGRectMake(0, 0, tileSize.width, tileSize.height),
                                           CGAffineTransformIdentity, tileSize.width, tileSize.height, 
                                           kCGPatternTilingConstantSpacing, true, &patternCallbacks);
    
    if (pattern) {
        CGContextSaveGState(context);

        CGPoint tileOrigin = CGPointMake(oneTileRect.origin.x, oneTileRect.origin.y);
        [[WebCoreImageRendererFactory sharedFactory] setPatternPhaseForContext:context inUserSpace:tileOrigin];

        CGColorSpaceRef patternSpace = CGColorSpaceCreatePattern(NULL);
        CGContextSetFillColorSpace(context, patternSpace);
        CGColorSpaceRelease(patternSpace);

        float patternAlpha = 1;
        CGContextSetFillPattern(context, pattern, &patternAlpha);

        setCompositingOperation(context, Image::CompositeSourceOver);

        CGContextFillRect(context, rect);

        CGContextRestoreGState(context);

        CGPatternRelease(pattern);
    }
    
    startAnimation();
}

void Image::scaleAndTileInRect(const FloatRect& dstRect, const FloatRect& srcRect,
                               TileRule hRule, TileRule vRule, void* ctxt)
{    
    CGImageRef image = frameAtIndex(m_currentFrame);
    if (!image)
        return;

    CGContextRef context = graphicsContext(ctxt);
    
    if (m_currentFrame == 0 && m_isSolidColor)
        return fillSolidColorInRect(m_solidColor, dstRect, Image::CompositeSourceOver, context);

    CGContextSaveGState(context);
    CGSize tileSize = srcRect.size();
    CGRect ir = dstRect;
    CGRect fr = srcRect;

    // Now scale the slice in the appropriate direction using an affine transform that we will pass into
    // the pattern.
    float scaleX = 1.0f, scaleY = 1.0f;

    if (hRule == Image::StretchTile)
        scaleX = ir.size.width / fr.size.width;
    if (vRule == Image::StretchTile)
        scaleY = ir.size.height / fr.size.height;
    
    if (hRule == Image::RepeatTile)
        scaleX = scaleY;
    if (vRule == Image::RepeatTile)
        scaleY = scaleX;
        
    if (hRule == Image::RoundTile) {
        // Complicated math ensues.
        float imageWidth = fr.size.width * scaleY;
        float newWidth = ir.size.width / ceilf(ir.size.width / imageWidth);
        scaleX = newWidth / fr.size.width;
    }
    
    if (vRule == Image::RoundTile) {
        // More complicated math ensues.
        float imageHeight = fr.size.height * scaleX;
        float newHeight = ir.size.height / ceilf(ir.size.height / imageHeight);
        scaleY = newHeight / fr.size.height;
    }
    
    CGAffineTransform patternTransform = CGAffineTransformMakeScale(scaleX, scaleY);

    // Possible optimization:  We may want to cache the CGPatternRef    
    CGPatternRef pattern = CGPatternCreate(this, CGRectMake(fr.origin.x, fr.origin.y, tileSize.width, tileSize.height),
                                           patternTransform, tileSize.width, tileSize.height, 
                                           kCGPatternTilingConstantSpacing, true, &patternCallbacks);
    if (pattern) {
        // We want to construct the phase such that the pattern is centered (when stretch is not
        // set for a particular rule).
        float hPhase = scaleX * fr.origin.x;
        float vPhase = scaleY * (tileSize.height - fr.origin.y);
        if (hRule == Image::RepeatTile)
            hPhase -= fmodf(ir.size.width, scaleX * tileSize.width) / 2.0f;
        if (vRule == Image::RepeatTile)
            vPhase -= fmodf(ir.size.height, scaleY * tileSize.height) / 2.0f;
        
        CGPoint tileOrigin = CGPointMake(ir.origin.x - hPhase, ir.origin.y - vPhase);
        [[WebCoreImageRendererFactory sharedFactory] setPatternPhaseForContext:context inUserSpace:tileOrigin];

        CGColorSpaceRef patternSpace = CGColorSpaceCreatePattern(NULL);
        CGContextSetFillColorSpace(context, patternSpace);
        CGColorSpaceRelease(patternSpace);

        float patternAlpha = 1;
        CGContextSetFillPattern(context, pattern, &patternAlpha);

        setCompositingOperation(context, Image::CompositeSourceOver);
        
        CGContextFillRect(context, ir);

        CGPatternRelease (pattern);
    }

    CGContextRestoreGState(context);

    startAnimation();
}

}
