/*
    Copyright (C) 2004, 2005 Nikolas Zimmermann <wildfox@kde.org>
                  2004, 2005 Rob Buis <buis@kde.org>

    This file is part of the KDE project

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
    Boston, MA 02111-1307, USA.
*/

#include "config.h"
#include <kmimetype.h>

#include <qimage.h>
#include <qbuffer.h>

#include <kdom/core/AttrImpl.h>
#include <kdom/cache/KDOMLoader.h>
#include <kdom/cache/KDOMCachedObject.h>

#include "SVGNames.h"
#include "SVGHelper.h"
#include "SVGSVGElementImpl.h"
#include "SVGImageElementImpl.h"
#include "SVGAnimatedLengthImpl.h"
#include "SVGAnimatedStringImpl.h"
#include "KCanvasRenderingStyle.h"
#include "SVGAnimatedPreserveAspectRatioImpl.h"
#ifndef APPLE_CHANGES
#include "KSVGDocumentBuilder.h"
#endif
#include "SVGDocumentImpl.h"
#include <ksvg2/KSVGView.h>

#include <kcanvas/KCanvas.h>
#include <kcanvas/KCanvasCreator.h>
#include <kcanvas/KCanvasContainer.h>
#include <kcanvas/KCanvasImage.h>
#include "KCanvasRenderingStyle.h"
#include <kcanvas/device/KRenderingDevice.h>
#include <kcanvas/device/KRenderingFillPainter.h>
#include <kcanvas/device/KRenderingPaintServerImage.h>

#include <kdom/parser/KDOMParser.h>
#ifndef APPLE_CHANGES
#include <kdom/parser/KDOMParserFactory.h>
#endif
//#include <kdom/DOMConfiguration.h>
#include <kdom/core/DOMConfigurationImpl.h>

using namespace KSVG;

SVGImageElementImpl::SVGImageElementImpl(const KDOM::QualifiedName& tagName, KDOM::DocumentImpl *doc)
: SVGStyledTransformableElementImpl(tagName, doc), SVGTestsImpl(), SVGLangSpaceImpl(), SVGExternalResourcesRequiredImpl(), SVGURIReferenceImpl(), KDOM::CachedObjectClient()
{
    m_x = m_y = m_width = m_height = 0;
    m_preserveAspectRatio = 0;
    //m_cachedDocument = 0;
    m_cachedImage = 0;
    m_svgDoc = 0;
}

SVGImageElementImpl::~SVGImageElementImpl()
{
    if(m_x)
        m_x->deref();
    if(m_y)
        m_y->deref();
    if(m_width)
        m_width->deref();
    if(m_height)
        m_height->deref();
    if(m_preserveAspectRatio)
        m_preserveAspectRatio->deref();
    if(m_svgDoc)
        m_svgDoc->deref();
}

SVGAnimatedLengthImpl *SVGImageElementImpl::x() const
{
    return lazy_create<SVGAnimatedLengthImpl>(m_x, this, LM_WIDTH, viewportElement());
}

SVGAnimatedLengthImpl *SVGImageElementImpl::y() const
{
    return lazy_create<SVGAnimatedLengthImpl>(m_y, this, LM_HEIGHT, viewportElement());
}

SVGAnimatedLengthImpl *SVGImageElementImpl::width() const
{
    return lazy_create<SVGAnimatedLengthImpl>(m_width, this, LM_WIDTH, viewportElement());
}

SVGAnimatedLengthImpl *SVGImageElementImpl::height() const
{
    return lazy_create<SVGAnimatedLengthImpl>(m_height, this, LM_HEIGHT, viewportElement());
}

SVGAnimatedPreserveAspectRatioImpl *SVGImageElementImpl::preserveAspectRatio() const
{
    return lazy_create<SVGAnimatedPreserveAspectRatioImpl>(m_preserveAspectRatio, this);
}

void SVGImageElementImpl::parseMappedAttribute(KDOM::MappedAttributeImpl *attr)
{
    const KDOM::AtomicString& value = attr->value();
    if (attr->name() == SVGNames::xAttr)
        x()->baseVal()->setValueAsString(value.impl());
    else if (attr->name() == SVGNames::yAttr)
        y()->baseVal()->setValueAsString(value.impl());
    else if (attr->name() == SVGNames::preserveAspectRatioAttr)
        preserveAspectRatio()->baseVal()->parsePreserveAspectRatio(value.impl());
    else if (attr->name() == SVGNames::widthAttr)
        width()->baseVal()->setValueAsString(value.impl());
    else if (attr->name() == SVGNames::heightAttr)
        height()->baseVal()->setValueAsString(value.impl());
    else
    {
        if(SVGTestsImpl::parseMappedAttribute(attr)) return;
        if(SVGLangSpaceImpl::parseMappedAttribute(attr)) return;
        if(SVGExternalResourcesRequiredImpl::parseMappedAttribute(attr)) return;
        if(SVGURIReferenceImpl::parseMappedAttribute(attr)) return;
        SVGStyledTransformableElementImpl::parseMappedAttribute(attr);
    }
}

khtml::RenderObject *SVGImageElementImpl::createRenderer(RenderArena *arena, khtml::RenderStyle *style)
{
    QString fname = KDOM::DOMString(href()->baseVal()).qstring();
#ifndef APPLE_COMPILE_HACK
    KURL fullUrl(ownerDocument()->documentKURI(), fname);
    KMimeType::Ptr mimeType = KMimeType::findByURL(fullUrl);
    if(mimeType->is(QString::fromLatin1("image/svg+xml"))) // does it have svg content?
    {
        RenderPath *ret = canvas()->renderingDevice()->createContainer(style, this);
        m_cachedDocument = ownerDocument()->docLoader()->requestDocument(fullUrl, QString());

        if(m_cachedDocument)
            m_cachedDocument->ref(const_cast<SVGImageElementImpl *>(this));
        return ret;
    }
#endif

    float _x = x()->baseVal()->value(), _y = y()->baseVal()->value();
    float _width = width()->baseVal()->value(), _height = height()->baseVal()->value();

    // Use dummy rect
    KCPathDataList pathData = KCanvasCreator::self()->createRectangle(_x, _y, _width, _height);
    if(pathData.isEmpty())
        return 0;

    KCanvasUserData path = KCanvasCreator::self()->createCanvasPathData(canvas()->renderingDevice(), pathData);
    return canvas()->renderingDevice()->createItem(arena, style, this, path);
}


void SVGImageElementImpl::notifyFinished(KDOM::CachedObject *finishedObj)
{
#ifndef APPLE_COMPILE_HACK
    if(finishedObj == m_cachedDocument)
    {
        KSVG::DocumentBuilder *builder = new KSVG::DocumentBuilder(0);
        KDOM::Parser *parser = KDOM::ParserFactory::self()->request(KURL(), builder);
        parser->domConfig()->setParameter(KDOM::ENTITIES.impl(), false);
        parser->domConfig()->setParameter(KDOM::ELEMENT_CONTENT_WHITESPACE.impl(), false);

        QBuffer *temp = new QBuffer(&m_cachedDocument->documentBuffer()->buffer());
        m_svgDoc = static_cast<SVGDocumentImpl *>(parser->syncParse(temp));
        if(m_svgDoc)
        {
            SVGSVGElementImpl *root = m_svgDoc->rootElement();
            KDOM::DOMStringImpl *_x = x()->baseVal()->valueAsString(), *_y = y()->baseVal()->valueAsString();
            KDOM::DOMStringImpl *_width = width()->baseVal()->valueAsString(), *_height = height()->baseVal()->valueAsString();

            root->setAttribute(SVGNames::xAttr, _x);
            root->setAttribute(SVGNames::yAttr, _y);
            root->setAttribute(SVGNames::widthAttr, _width);
            root->setAttribute(SVGNames::heightAttr, _height);

            // TODO: viewBox handling? animations? ecmascript?

            m_svgDoc->setView(getDocument()->view());
            m_svgDoc->setCanvasView(getDocument()->canvasView());

            m_svgDoc->attach();
            m_canvasItem->appendItem(root->renderer());
        }
        m_cachedDocument->deref(this);
        m_cachedDocument = 0;
    }
    else
#endif
    if(finishedObj == m_cachedImage)
    {
        if(renderer() && renderer()->isRenderPath() && renderer()->style())
        {
            RenderPath *item = static_cast<RenderPath *>(renderer());
            KRenderingFillPainter *fillPainter = item->canvasStyle()->fillPainter();
            if(!fillPainter)
                return;

            KRenderingPaintServer *fillPaintServer = fillPainter->paintServer();
            if (fillPaintServer->type() != PS_IMAGE)
                return;
            KRenderingPaintServerImage *fillPaintServerImage = static_cast<KRenderingPaintServerImage *>(fillPaintServer);
            fillPaintServerImage->setImage(m_cachedImage->pixmap());

            item->setNeedsLayout(true);
        }

        m_cachedImage->deref(this);
        m_cachedImage = 0;
    }
}

void SVGImageElementImpl::finalizeStyle(KCanvasRenderingStyle *style, bool /* needFillStrokeUpdate */)
{
    SVGElementImpl *parentElement = svg_dynamic_cast(parentNode());
    if (!parentElement || !parentElement->isStyled())
        return;
    SVGStyledElementImpl *parent = static_cast<SVGStyledElementImpl *>(parentElement);
    if(!parent->allowAttachChildren(this))
        return;

    // Set up image paint server
    style->disableFillPainter();
    style->disableStrokePainter();

    KRenderingPaintServer *fillPaintServer = canvas()->renderingDevice()->createPaintServer(PS_IMAGE);
    style->fillPainter()->setPaintServer(fillPaintServer);

//    if(!m_cachedDocument) // bitmap
//    {
        m_cachedImage = ownerDocument()->docLoader()->requestImage(href()->baseVal());

        if(m_cachedImage)
            m_cachedImage->ref(this);
//    }
}

// vim:ts=4:noet
