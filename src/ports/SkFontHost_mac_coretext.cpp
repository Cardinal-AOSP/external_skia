/*
 ** Copyright 2006, The Android Open Source Project
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **     http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
*/
#include <vector>
#include <Carbon/Carbon.h>

#include "SkFontHost.h"
#include "SkDescriptor.h"
#include "SkFloatingPoint.h"
#include "SkPaint.h"
#include "SkString.h"
#include "SkTypeface_mac.h"
#include "SkUtils.h"
#include "SkTypefaceCache.h"

static const size_t FONT_CACHE_MEMORY_BUDGET    = 1024 * 1024;
static const char FONT_DEFAULT_NAME[]           = "Lucida Sans";

//============================================================================
//      Macros
//----------------------------------------------------------------------------
// Release a CFTypeRef
#ifndef CFSafeRelease
#define CFSafeRelease(_object)                                      \
    do                                                              \
        {                                                           \
        if ((_object) != NULL)                                      \
            {                                                       \
            CFRelease((CFTypeRef) (_object));                       \
            (_object) = NULL;                                       \
            }                                                       \
        }                                                           \
    while (false)
#endif


static SkTypeface::Style computeStyleBits(CTFontRef font, bool* isMonospace) {
    unsigned style = SkTypeface::kNormal;
    CTFontSymbolicTraits traits = CTFontGetSymbolicTraits(font);

    if (traits & kCTFontBoldTrait) {
        style |= SkTypeface::kBold;
    }
    if (traits & kCTFontItalicTrait) {
        style |= SkTypeface::kItalic;
    }
    if (isMonospace) {
        *isMonospace = (traits & kCTFontMonoSpaceTrait) != 0;
    }
    return (SkTypeface::Style)style;
}

class SkTypeface_Mac : public SkTypeface {
public:
    SkTypeface_Mac(SkTypeface::Style style, SkFontID fontID, bool isMonospace)
        : SkTypeface(style, fontID, isMonospace), fFontRef(0) {}

    virtual ~SkTypeface_Mac() { CFRelease(fFontRef); }

    SkString    fName;
    CTFontRef   fFontRef;
};

static SkTypeface* NewFromFontRef(CTFontRef fontRef, const char name[]) {
    bool isMonospace;
    SkTypeface::Style style = computeStyleBits(fontRef, &isMonospace);
    SkTypeface_Mac* face = new SkTypeface_Mac(style,
                                              SkTypefaceCache::NewFontID(),
                                              isMonospace);
    face->fFontRef = fontRef;   // we take over ownership of fontRef
    face->fName.set(name);
    return face;
}

static SkTypeface* NewFromName(const char familyName[],
                               SkTypeface::Style theStyle) {
    CFMutableDictionaryRef      cfAttributes, cfTraits;
    CFNumberRef                 cfFontTraits;
    CTFontSymbolicTraits        ctFontTraits;
    CTFontDescriptorRef         ctFontDesc;
    CFStringRef                 cfFontName;
    CTFontRef                   ctFont;
    
    
    // Get the state we need
    ctFontDesc   = NULL;
    ctFont       = NULL;
    ctFontTraits = 0;
    
    if (theStyle & SkTypeface::kBold) {
        ctFontTraits |= kCTFontBoldTrait;
    }

    if (theStyle & SkTypeface::kItalic) {
        ctFontTraits |= kCTFontItalicTrait;
    }
    
    // Create the font info
    cfFontName   = CFStringCreateWithCString(NULL, familyName, kCFStringEncodingUTF8);
    cfFontTraits = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &ctFontTraits);
    cfAttributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    cfTraits     = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    
    
    // Create the font
    if (cfFontName != NULL && cfFontTraits != NULL && cfAttributes != NULL && cfTraits != NULL) {
        CFDictionaryAddValue(cfTraits, kCTFontSymbolicTrait, cfFontTraits);
        
        CFDictionaryAddValue(cfAttributes, kCTFontFamilyNameAttribute, cfFontName);
        CFDictionaryAddValue(cfAttributes, kCTFontTraitsAttribute,     cfTraits);
        
        ctFontDesc = CTFontDescriptorCreateWithAttributes(cfAttributes);
        if (ctFontDesc != NULL) {
            ctFont = CTFontCreateWithFontDescriptor(ctFontDesc, 0, NULL);
        }
    }
    
    CFSafeRelease(cfFontName);
    CFSafeRelease(cfFontTraits);
    CFSafeRelease(cfAttributes);
    CFSafeRelease(cfTraits);
    CFSafeRelease(ctFontDesc);

    return ctFont ? NewFromFontRef(ctFont, familyName) : NULL;
}

static CTFontRef GetFontRefFromFontID(SkFontID fontID) {
    SkTypeface_Mac* face = reinterpret_cast<SkTypeface_Mac*>(SkTypefaceCache::FindByID(fontID));
    return face ? face->fFontRef : 0;
}

static SkTypeface* GetDefaultFace() {
    static SkTypeface* gDefaultFace;

    if (NULL == gDefaultFace) {
        gDefaultFace = new SkTypeface_Mac(SkTypeface::kNormal,
                                          SkTypefaceCache::NewFontID(), false);
    }
    return gDefaultFace;
}

///////////////////////////////////////////////////////////////////////////////

struct FontRefRec {
    CTFontRef   fFontRef;
};

static bool FindByFontRef(SkTypeface* face, SkTypeface::Style, void* ctx) {
    const SkTypeface_Mac* mface = reinterpret_cast<SkTypeface_Mac*>(face);
    const FontRefRec* rec = reinterpret_cast<const FontRefRec*>(ctx);

    return rec->fFontRef == mface->fFontRef;
}

/*  This function is visible on the outside. It first searches the cache, and if
 *  not found, returns a new entry (after adding it to the cache).
 */
SkTypeface* SkCreateTypefaceFromCTFont(CTFontRef fontRef) {
    FontRefRec rec = { fontRef };
    SkTypeface* face = SkTypefaceCache::FindByProc(FindByFontRef, &rec);
    if (face) {
        face->ref();
    } else {
        face = NewFromFontRef(fontRef, NULL);
        SkTypefaceCache::Add(face, face->style());
    }
    SkASSERT(face->getRefCnt() > 1);
    return face;
}

struct NameStyleRec {
    const char*         fName;
    SkTypeface::Style   fStyle;
};

static bool FindByNameStyle(SkTypeface* face, SkTypeface::Style style,
                            void* ctx) {
    const SkTypeface_Mac* mface = reinterpret_cast<SkTypeface_Mac*>(face);
    const NameStyleRec* rec = reinterpret_cast<const NameStyleRec*>(ctx);
    
    return rec->fStyle == style && mface->fName.equals(rec->fName);
}

static const char* map_css_names(const char* name) {
    static const struct {
        const char* fFrom;  // name the caller specified
        const char* fTo;    // "canonical" name we map to
    } gPairs[] = {
        { "sans-serif", "Helvetica" },
        { "serif",      "Times"     },
        { "monospace",  "Courier"   }
    };
    
    for (size_t i = 0; i < SK_ARRAY_COUNT(gPairs); i++) {
        if (strcmp(name, gPairs[i].fFrom) == 0) {
            return gPairs[i].fTo;
        }
    }
    return name;    // no change
}

SkTypeface* SkFontHost::CreateTypeface(const SkTypeface* familyFace,
                                       const char familyName[],
                                       const void* data, size_t bytelength,
                                       SkTypeface::Style style) {
    if (familyName) {
        familyName = map_css_names(familyName);
    }
    
    // Clone an existing typeface
    // TODO: only clone if style matches the familyFace's style...
    if (familyName == NULL && familyFace != NULL) {
        familyFace->ref();
        return const_cast<SkTypeface*>(familyFace);
    }
    
    if (!familyName || !*familyName) {
        familyName = FONT_DEFAULT_NAME;
    }
    
    NameStyleRec rec = { familyName, style };
    SkTypeface* face = SkTypefaceCache::FindByProc(FindByNameStyle, &rec);

    if (face) {
        face->ref();
    } else {
        face = NewFromName(familyName, style);
        if (face) {
            SkTypefaceCache::Add(face, style);
        } else {
            face = GetDefaultFace();
            face->ref();
        }
    }
    return face;
}

///////////////////////////////////////////////////////////////////////////////

class SkScalerContext_Mac : public SkScalerContext {
public:
                                        SkScalerContext_Mac(const SkDescriptor* desc);
    virtual                            ~SkScalerContext_Mac(void);


protected:
    unsigned                            generateGlyphCount(void);
    uint16_t                            generateCharToGlyph(SkUnichar uni);
    void                                generateAdvance(SkGlyph* glyph);
    void                                generateMetrics(SkGlyph* glyph);
    void                                generateImage(const SkGlyph& glyph);
    void                                generatePath( const SkGlyph& glyph, SkPath* path);
    void                                generateFontMetrics(SkPaint::FontMetrics* mX, SkPaint::FontMetrics* mY);


private:
    static void                         CTPathElement(void *info, const CGPathElement *element);


private:
    CGColorSpaceRef                     mColorSpaceGray;
    CGColorSpaceRef                     mColorSpaceRGB;
    CGAffineTransform                   mTransform;

    CTFontRef                           mFont;
    uint16_t                            mGlyphCount;
};

SkScalerContext_Mac::SkScalerContext_Mac(const SkDescriptor* desc)
        : SkScalerContext(desc)
{   CFIndex             numGlyphs;
    CTFontRef           ctFont;
    SkMatrix            skMatrix;



    // Get the state we need
    fRec.getSingleMatrix(&skMatrix);

    ctFont    = GetFontRefFromFontID(fRec.fFontID);
    numGlyphs = CTFontGetGlyphCount(ctFont);
    SkASSERT(numGlyphs >= 1 && numGlyphs <= 0xFFFF);


    // Initialise ourselves
    mColorSpaceRGB = CGColorSpaceCreateDeviceRGB();
//    mColorSpaceRGB = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
//    mColorSpaceRGB = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGBLinear);
    mColorSpaceGray = CGColorSpaceCreateDeviceGray();

    mTransform  = CGAffineTransformMake(SkScalarToFloat(skMatrix[SkMatrix::kMScaleX]),
                                        -SkScalarToFloat(skMatrix[SkMatrix::kMSkewY]),
                                        -SkScalarToFloat(skMatrix[SkMatrix::kMSkewX]),
                                        SkScalarToFloat(skMatrix[SkMatrix::kMScaleY]),
                                        SkScalarToFloat(skMatrix[SkMatrix::kMTransX]),
                                        SkScalarToFloat(skMatrix[SkMatrix::kMTransY]));

    // since our matrix includes everything, we pass 1 for pointSize
    mFont       = CTFontCreateCopyWithAttributes(ctFont, 1, &mTransform, NULL);
    mGlyphCount = (uint16_t) numGlyphs;
}

SkScalerContext_Mac::~SkScalerContext_Mac(void)
{

    // Clean up
    CFSafeRelease(mColorSpaceGray);
    CFSafeRelease(mColorSpaceRGB);
    CFSafeRelease(mFont);
}

unsigned SkScalerContext_Mac::generateGlyphCount(void)
{
    return(mGlyphCount);
}

uint16_t SkScalerContext_Mac::generateCharToGlyph(SkUnichar uni)
{   CGGlyph     cgGlyph;
    UniChar     theChar;


    // Validate our parameters and state
    SkASSERT(uni             <= 0x0000FFFF);
    SkASSERT(sizeof(CGGlyph) <= sizeof(uint16_t));


    // Get the glyph
    theChar = (UniChar) uni;

    if (!CTFontGetGlyphsForCharacters(mFont, &theChar, &cgGlyph, 1))
        cgGlyph = 0;

    return(cgGlyph);
}

void SkScalerContext_Mac::generateAdvance(SkGlyph* glyph)
{
    this->generateMetrics(glyph);
}

void SkScalerContext_Mac::generateMetrics(SkGlyph* glyph)
{   CGSize      theAdvance;
    CGRect      theBounds;
    CGGlyph     cgGlyph;



    // Get the state we need
    cgGlyph = (CGGlyph) glyph->getGlyphID(fBaseGlyphCount);

    CTFontGetBoundingRectsForGlyphs(mFont, kCTFontDefaultOrientation, &cgGlyph, &theBounds,  1);
    CTFontGetAdvancesForGlyphs(     mFont, kCTFontDefaultOrientation, &cgGlyph, &theAdvance, 1);



    // Adjust the bounds
    //
    // CTFontGetBoundingRectsForGlyphs ignores the font transform, so we need
    // to transform the bounding box ourselves.
    //
    // The bounds are also expanded by 1 pixel, to give CG room for anti-aliasing.
    theBounds = CGRectInset(theBounds, -1, -1);



    // Get the metrics
    glyph->zeroMetrics();
    glyph->fAdvanceX =  SkFloatToFixed(theAdvance.width);
    glyph->fAdvanceY = -SkFloatToFixed(theAdvance.height);
    glyph->fWidth    =  sk_float_round2int(theBounds.size.width);
    glyph->fHeight   =  sk_float_round2int(theBounds.size.height);
    glyph->fTop      = -sk_float_round2int(CGRectGetMaxY(theBounds));
    glyph->fLeft     =  sk_float_round2int(CGRectGetMinX(theBounds));
}

#include "SkColorPriv.h"

static void bytes_to_bits(uint8_t dst[], const uint8_t src[], int count) {
    while (count > 0) {
        uint8_t mask = 0;
        for (int i = 7; i >= 0; --i) {
            mask |= (*src++ >> 7) << i;
            if (0 == --count) {
                break;
            }
        }
        *dst++ = mask;
    }
}

static inline uint16_t rgb_to_lcd16(uint32_t rgb) {
    int r = (rgb >> 16) & 0xFF;
    int g = (rgb >>  8) & 0xFF;
    int b = (rgb >>  0) & 0xFF;

    // invert, since we draw black-on-white, but we want the original
    // src mask values.
    r = 255 - r;
    g = 255 - g;
    b = 255 - b;

    return SkPackRGB16(SkR32ToR16(r), SkG32ToG16(g), SkB32ToB16(b));
}

#define BITMAP_INFO_RGB     (kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Host)
#define BITMAP_INFO_GRAY    (kCGImageAlphaNone)

void SkScalerContext_Mac::generateImage(const SkGlyph& glyph) {
    CGContextRef        cgContext;
    CGGlyph             cgGlyph;
    CGFontRef           cgFont;

    // Get the state we need
    sk_bzero(glyph.fImage, glyph.fHeight * glyph.rowBytes());

    cgGlyph   = (CGGlyph) glyph.getGlyphID(fBaseGlyphCount);
    cgFont    = CTFontCopyGraphicsFont(mFont, NULL);

    SkAutoSMalloc<1024> storage;

    CGColorSpaceRef colorspace = mColorSpaceGray;
    uint32_t info = BITMAP_INFO_GRAY;
    void* image = glyph.fImage;
    size_t rowBytes = glyph.rowBytes();
    float grayColor = 1; // white
    bool doAA = true;

    /*  For LCD16, we first create a temp offscreen cg-context in 32bit,
     *  erase to white, and then draw a black glyph into it. Then we can
     *  extract the r,g,b values, invert-them, and now we have the original
     *  src mask components, which we pack into our 16bit mask.
     */
    if (SkMask::kLCD16_Format == glyph.fMaskFormat) {
        colorspace = mColorSpaceRGB;
        info = BITMAP_INFO_RGB;
        // need tmp storage for 32bit RGB offscreen
        rowBytes = glyph.fWidth << 2;
        size_t size = glyph.fHeight * rowBytes;
        image = storage.realloc(size);
        // we draw black-on-white (and invert in rgb_to_lcd16)
        sk_memset32((uint32_t*)image, 0xFFFFFFFF, size >> 2);
        grayColor = 0;  // black
    } else if (SkMask::kBW_Format == glyph.fMaskFormat) {
        rowBytes = SkAlign4(glyph.fWidth);
        size_t size = glyph.fHeight * rowBytes;
        image = storage.realloc(size);
        sk_bzero(image, size);
        doAA = false;
    }

    cgContext = CGBitmapContextCreate(image, glyph.fWidth, glyph.fHeight, 8,
                                      rowBytes, colorspace, info);

    // Draw the glyph
    if (cgFont != NULL && cgContext != NULL) {
#ifdef WE_ARE_RUNNING_ON_10_6_OR_LATER
        CGContextSetAllowsFontSubpixelQuantization(cgContext, true);
        CGContextSetShouldSubpixelQuantizeFonts(cgContext, true);
#endif
        CGContextSetShouldAntialias(cgContext, doAA);
        CGContextSetGrayFillColor(  cgContext, grayColor, 1.0);
        CGContextSetTextDrawingMode(cgContext, kCGTextFill);
        CGContextSetFont(           cgContext, cgFont);
        CGContextSetFontSize(       cgContext, 1); // cgFont know's its size
        CGContextSetTextMatrix(     cgContext, mTransform);
        CGContextShowGlyphsAtPoint( cgContext, -glyph.fLeft, glyph.fTop + glyph.fHeight, &cgGlyph, 1);

        if (SkMask::kLCD16_Format == glyph.fMaskFormat) {
            // downsample from rgba to rgb565
            int width = glyph.fWidth;
            const uint32_t* src = (const uint32_t*)image;
            uint16_t* dst = (uint16_t*)glyph.fImage;
            size_t dstRB = glyph.rowBytes();
            for (int y = 0; y < glyph.fHeight; y++) {
                for (int i = 0; i < width; i++) {
                    dst[i] = rgb_to_lcd16(src[i]);
                }
                src = (const uint32_t*)((const char*)src + rowBytes);
                dst = (uint16_t*)((char*)dst + dstRB);
            }
        } else if (SkMask::kBW_Format == glyph.fMaskFormat) {
            // downsample from A8 to A1
            const uint8_t* src = (const uint8_t*)image;
            uint8_t* dst = (uint8_t*)glyph.fImage;
            size_t dstRB = glyph.rowBytes();
            for (int y = 0; y < glyph.fHeight; y++) {
                bytes_to_bits(dst, src, glyph.fWidth);
                src += rowBytes;
                dst += dstRB;
            }
        }
    }

    // Clean up
    CFSafeRelease(cgFont);
    CFSafeRelease(cgContext);
}

void SkScalerContext_Mac::generatePath(const SkGlyph& glyph, SkPath* path) {
    CGGlyph   cgGlyph = (CGGlyph)glyph.getGlyphID(fBaseGlyphCount);
    CGPathRef cgPath  = CTFontCreatePathForGlyph(mFont, cgGlyph, NULL);

    path->reset();
    if (cgPath != NULL) {
        CGPathApply(cgPath, path, SkScalerContext_Mac::CTPathElement);
        CFRelease(cgPath);
    }
}

void SkScalerContext_Mac::generateFontMetrics(SkPaint::FontMetrics* mx,
                                              SkPaint::FontMetrics* my) {
    CGRect theBounds = CTFontGetBoundingBox(mFont);

    SkPaint::FontMetrics        theMetrics;
    theMetrics.fTop          = -CGRectGetMaxY(theBounds);
    theMetrics.fAscent       = -CTFontGetAscent(mFont);
    theMetrics.fDescent      =  CTFontGetDescent(mFont);
    theMetrics.fBottom       = -CGRectGetMinY(theBounds);
    theMetrics.fLeading      =  CTFontGetLeading(mFont);
    theMetrics.fAvgCharWidth =  CGRectGetWidth(theBounds);
    theMetrics.fXMin         =  CGRectGetMinX(theBounds);
    theMetrics.fXMax         =  CGRectGetMaxX(theBounds);
    theMetrics.fXHeight      =  CTFontGetXHeight(mFont);

#if 0
    SkASSERT(theMetrics.fTop          <= 0.0);
    SkASSERT(theMetrics.fAscent       <= 0.0);
    SkASSERT(theMetrics.fDescent      >= 0.0);
    SkASSERT(theMetrics.fBottom       >= 0.0);
    SkASSERT(theMetrics.fLeading      >= 0.0);
    SkASSERT(theMetrics.fAvgCharWidth >= 0.0);
    SkASSERT(theMetrics.fXMin         <= 0.0);
    SkASSERT(theMetrics.fXMax         >  0.0);
    SkASSERT(theMetrics.fXHeight      >= 0.0);
#endif

    if (mx != NULL) {
        *mx = theMetrics;
    }
    if (my != NULL) {
        *my = theMetrics;
    }
}

void SkScalerContext_Mac::CTPathElement(void *info, const CGPathElement *element)
{   SkPath      *skPath = (SkPath *) info;


    // Process the path element
    switch (element->type) {
        case kCGPathElementMoveToPoint:
            skPath->moveTo( element->points[0].x, -element->points[0].y);
            break;

        case kCGPathElementAddLineToPoint:
            skPath->lineTo( element->points[0].x, -element->points[0].y);
            break;

        case kCGPathElementAddQuadCurveToPoint:
            skPath->quadTo( element->points[0].x, -element->points[0].y,
                            element->points[1].x, -element->points[1].y);
            break;

        case kCGPathElementAddCurveToPoint:
            skPath->cubicTo(element->points[0].x, -element->points[0].y,
                            element->points[1].x, -element->points[1].y,
                            element->points[2].x, -element->points[2].y);
            break;

        case kCGPathElementCloseSubpath:
            skPath->close();
            break;

        default:
            SkASSERT("Unknown path element!");
            break;
        }
}


///////////////////////////////////////////////////////////////////////////////

SkTypeface* SkFontHost::CreateTypefaceFromStream(SkStream* stream)
{
//    SkASSERT(!"SkFontHost::CreateTypefaceFromStream unimplemented");
    return SkFontHost::CreateTypeface(NULL, NULL, NULL, NULL, SkTypeface::kNormal);
}

SkTypeface* SkFontHost::CreateTypefaceFromFile(const char path[])
{
//    SkASSERT(!"SkFontHost::CreateTypefaceFromFile unimplemented");
    return SkFontHost::CreateTypeface(NULL, NULL, NULL, NULL, SkTypeface::kNormal);
}

// static
SkAdvancedTypefaceMetrics* SkFontHost::GetAdvancedTypefaceMetrics(
        uint32_t fontID,
        SkAdvancedTypefaceMetrics::PerGlyphInfo perGlyphInfo) {
    SkASSERT(!"SkFontHost::GetAdvancedTypefaceMetrics unimplemented");
    return NULL;
}

///////////////////////////////////////////////////////////////////////////////

bool SkFontHost::ValidFontID(SkFontID fontID) {
    return SkTypefaceCache::FindByID(fontID) != NULL;
}

SkStream* SkFontHost::OpenStream(SkFontID uniqueID) {
    SkASSERT(!"SkFontHost::OpenStream unimplemented");
    return(NULL);
}

size_t SkFontHost::GetFileName(SkFontID fontID, char path[], size_t length,
                               int32_t* index) {
    SkASSERT(!"SkFontHost::GetFileName unimplemented");
    return(0);
}

///////////////////////////////////////////////////////////////////////////////

#include "SkStream.h"

void SkFontHost::Serialize(const SkTypeface* face, SkWStream* stream) {
    // hack: need a real name or something from CG
    uint32_t fontID = face->uniqueID();
    stream->write(&fontID, 4);
}

SkTypeface* SkFontHost::Deserialize(SkStream* stream) {
    // hack: need a real name or something from CG
    SkFontID fontID = stream->readU32();
    SkTypeface* face = SkTypefaceCache::FindByID(fontID);
    SkSafeRef(face);
    return face;
}

///////////////////////////////////////////////////////////////////////////////

SkScalerContext* SkFontHost::CreateScalerContext(const SkDescriptor* desc) {
    return new SkScalerContext_Mac(desc);
}

SkFontID SkFontHost::NextLogicalFont(SkFontID fontID) {
    SkFontID nextFontID = 0;
    SkTypeface* face = GetDefaultFace();
    if (face->uniqueID() != fontID) {
        nextFontID = face->uniqueID();
    }
    return nextFontID;
}

void SkFontHost::FilterRec(SkScalerContext::Rec* rec) {
    // we only support 2 levels of hinting
    SkPaint::Hinting h = rec->getHinting();
    if (SkPaint::kSlight_Hinting == h) {
        h = SkPaint::kNo_Hinting;
    } else if (SkPaint::kFull_Hinting == h) {
        h = SkPaint::kNormal_Hinting;
    }
    rec->setHinting(h);

    // we don't support LCD text
    if (SkMask::FormatIsLCD((SkMask::Format)rec->fMaskFormat)) {
        rec->fMaskFormat = SkMask::kA8_Format;
    }
}

///////////////////////////////////////////////////////////////////////////

size_t SkFontHost::ShouldPurgeFontCache(size_t sizeAllocatedSoFar) {
    if (sizeAllocatedSoFar > FONT_CACHE_MEMORY_BUDGET) {
        return sizeAllocatedSoFar - FONT_CACHE_MEMORY_BUDGET;
    }
    return 0;
}

int SkFontHost::ComputeGammaFlag(const SkPaint& paint) {
    return 0;
}

void SkFontHost::GetGammaTables(const uint8_t* tables[2]) {
    tables[0] = NULL;   // black gamma (e.g. exp=1.4)
    tables[1] = NULL;   // white gamma (e.g. exp= 1/1.4)
}

///////////////////////////////////////////////////////////////////////////

int SkFontHost::CountTables(SkFontID fontID) {
    int             numTables;
    CFArrayRef      cfArray;
    CTFontRef       ctFont;


    // Get the state we need
    ctFont    = GetFontRefFromFontID(fontID);
    cfArray   = CTFontCopyAvailableTables(ctFont, kCTFontTableOptionNoOptions);
    numTables = 0;


    // Get the table count
    if (cfArray != NULL)
        {
        numTables = CFArrayGetCount(cfArray);
        CFSafeRelease(cfArray);
        }

    return(numTables);
}

int SkFontHost::GetTableTags(SkFontID fontID, SkFontTableTag tags[])
{   int             n, numTables;
    CFArrayRef      cfArray;
    CTFontRef       ctFont;


    // Get the state we need
    ctFont    = GetFontRefFromFontID(fontID);
    cfArray   = CTFontCopyAvailableTables(ctFont, kCTFontTableOptionNoOptions);
    numTables = 0;


    // Get the table tags
    if (cfArray != NULL)
        {
        numTables = CFArrayGetCount(cfArray);
        for (n = 0; n < numTables; n++)
            tags[n] = (SkFontTableTag) ((uintptr_t) CFArrayGetValueAtIndex(cfArray, n));

        CFSafeRelease(cfArray);
        }

    return(numTables);
}

size_t SkFontHost::GetTableSize(SkFontID fontID, SkFontTableTag tag)
{   size_t      theSize;
    CTFontRef   ctFont;
    CFDataRef   cfData;


    // Get the state we need
    ctFont  = GetFontRefFromFontID(fontID);
    cfData  = CTFontCopyTable(ctFont, (CTFontTableTag) tag, kCTFontTableOptionNoOptions);
    theSize = 0;


    // Get the data size
    if (cfData != NULL)
        {
        theSize = CFDataGetLength(cfData);
        CFSafeRelease(cfData);
        }

    return(theSize);
}

size_t SkFontHost::GetTableData(SkFontID fontID, SkFontTableTag tag,
                                size_t offset, size_t length, void* data)
{   size_t          theSize;
    CTFontRef       ctFont;
    CFDataRef       cfData;


    // Get the state we need
    ctFont  = GetFontRefFromFontID(fontID);
    cfData  = CTFontCopyTable(ctFont, (CTFontTableTag) tag, kCTFontTableOptionNoOptions);
    theSize = 0;


    // Get the data
    if (cfData != NULL)
        theSize = CFDataGetLength(cfData);

    if (offset >= theSize)
        return 0;

    if ((offset + length) > theSize)
        length = theSize - offset;

    memcpy(data, CFDataGetBytePtr(cfData) + offset, length);
    return(length);
}




