/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */
#include <ScrollHelper.hxx>
#include <DesignView.hxx>
#include <ReportController.hxx>
#include <ReportWindow.hxx>
#include <UITools.hxx>
#include <com/sun/star/accessibility/AccessibleRole.hpp>

#include <vcl/commandevent.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>

namespace rptui
{
#define SECTION_OFFSET      3
#define SCR_LINE_SIZE       10
using namespace ::com::sun::star;


static void lcl_setScrollBar(sal_Int32 _nNewValue,const Point& _aPos,const Size& _aSize,ScrollAdaptor& _rScrollBar)
{
    _rScrollBar.SetPosSizePixel(_aPos,_aSize);
    _rScrollBar.SetPageSize( _nNewValue );
    _rScrollBar.SetVisibleSize( _nNewValue );
}


OScrollWindowHelper::OScrollWindowHelper( ODesignView* _pDesignView)
    : vcl::Window( _pDesignView,WB_DIALOGCONTROL)
    ,OPropertyChangeListener()
    ,m_aHScroll( VclPtr<ScrollAdaptor>::Create(this, true) )
    ,m_aVScroll( VclPtr<ScrollAdaptor>::Create(this, false) )
    ,m_pParent(_pDesignView)
    ,m_aReportWindow(VclPtr<rptui::OReportWindow>::Create(this,m_pParent))
{
    SetMapMode( MapMode( MapUnit::Map100thMM ) );

    impl_initScrollBar( *m_aHScroll );
    impl_initScrollBar( *m_aVScroll );

    m_aReportWindow->SetMapMode( MapMode( MapUnit::Map100thMM ) );
    m_aReportWindow->Show();

    // normally we should be SCROLL_PANE
    SetAccessibleRole(css::accessibility::AccessibleRole::SCROLL_PANE);
    ImplInitSettings();
}


OScrollWindowHelper::~OScrollWindowHelper()
{
    disposeOnce();
}

void OScrollWindowHelper::dispose()
{
    if ( m_pReportDefinitionMultiPlexer.is() )
        m_pReportDefinitionMultiPlexer->dispose();

    m_aHScroll.disposeAndClear();
    m_aVScroll.disposeAndClear();
    m_aReportWindow.disposeAndClear();
    m_pParent.reset();
    vcl::Window::dispose();
}

void OScrollWindowHelper::impl_initScrollBar( ScrollAdaptor& _rScrollBar ) const
{
    _rScrollBar.SetScrollHdl( LINK( const_cast<OScrollWindowHelper*>(this), OScrollWindowHelper, ScrollHdl ) );
    _rScrollBar.SetLineSize( SCR_LINE_SIZE );
}

void OScrollWindowHelper::initialize()
{
    uno::Reference<report::XReportDefinition> xReportDefinition = m_pParent->getController().getReportDefinition();
    m_pReportDefinitionMultiPlexer = addStyleListener(xReportDefinition,this);
}

void OScrollWindowHelper::setTotalSize(sal_Int32 _nWidth ,sal_Int32 _nHeight)
{
    m_aTotalPixelSize.setWidth( _nWidth );
    m_aTotalPixelSize.setHeight( _nHeight );

    // now set the ranges without start marker
    Fraction aStartWidth(REPORT_STARTMARKER_WIDTH * m_pParent->getController().getZoomValue(),100);
    tools::Long nWidth = tools::Long(_nWidth - static_cast<double>(aStartWidth));
    m_aHScroll->SetRangeMax( nWidth );
    m_aVScroll->SetRangeMax( m_aTotalPixelSize.Height() );

    Resize();
}

Size OScrollWindowHelper::ResizeScrollBars()
{
    // get the new output-size in pixel
    Size aOutPixSz = GetOutputSizePixel();
    if ( aOutPixSz.IsEmpty() )
        return aOutPixSz;

    aOutPixSz.AdjustHeight( -(m_aReportWindow->getRulerHeight()) );
    // determine the size of the output-area and if we need scrollbars
    const tools::Long nScrSize = GetSettings().GetStyleSettings().GetScrollBarSize();
    bool bVVisible = false; // by default no vertical-ScrollBar
    bool bHVisible = false; // by default no horizontal-ScrollBar
    bool bChanged;          // determines if a visibility was changed
    do
    {
        bChanged = false;

        // does we need a vertical ScrollBar
        if ( aOutPixSz.Width() < m_aTotalPixelSize.Width() && !bHVisible )
        {
            bHVisible = true;
            aOutPixSz.AdjustHeight( -nScrSize );
            bChanged = true;
        }

        // does we need a horizontal ScrollBar
        if ( aOutPixSz.Height() < m_aTotalPixelSize.Height() && !bVVisible )
        {
            bVVisible = true;
            aOutPixSz.AdjustWidth( -nScrSize );
            bChanged = true;
        }

    }
    while ( bChanged );   // until no visibility has changed

    aOutPixSz.AdjustHeight(m_aReportWindow->getRulerHeight() );

    // show or hide scrollbars
    m_aVScroll->Show( bVVisible );
    m_aHScroll->Show( bHVisible );

    const Point aOffset = LogicToPixel(Point(SECTION_OFFSET, SECTION_OFFSET), MapMode(MapUnit::MapAppFont));
    // resize scrollbars and set their ranges
    {
        Fraction aStartWidth(tools::Long(REPORT_STARTMARKER_WIDTH*m_pParent->getController().getZoomValue()),100);
        const sal_Int32 nNewWidth = aOutPixSz.Width() - aOffset.X() - static_cast<tools::Long>(aStartWidth);
        lcl_setScrollBar(nNewWidth,Point( static_cast<tools::Long>(aStartWidth) + aOffset.X(), aOutPixSz.Height() ), Size( nNewWidth, nScrSize ), *m_aHScroll);
    }
    {
        const sal_Int32 nNewHeight = aOutPixSz.Height() - m_aReportWindow->getRulerHeight();
        lcl_setScrollBar(nNewHeight,Point( aOutPixSz.Width(), m_aReportWindow->getRulerHeight() ), Size( nScrSize,nNewHeight), *m_aVScroll);
    }

    return aOutPixSz;
}

void OScrollWindowHelper::Resize()
{
    vcl::Window::Resize();
    const Size aTotalOutputSize = ResizeScrollBars();

    m_aReportWindow->SetPosSizePixel(Point( 0, 0 ),aTotalOutputSize);
}

IMPL_LINK_NOARG(OScrollWindowHelper, ScrollHdl, weld::Scrollbar&, void)
{
    m_aReportWindow->ScrollChildren( getThumbPos() );
}

void OScrollWindowHelper::addSection(const uno::Reference< report::XSection >& _xSection
                                   ,const OUString& _sColorEntry
                                   ,sal_uInt16 _nPosition)
{
    m_aReportWindow->addSection(_xSection,_sColorEntry,_nPosition);
}

void OScrollWindowHelper::removeSection(sal_uInt16 _nPosition)
{
    m_aReportWindow->removeSection(_nPosition);
}

void OScrollWindowHelper::toggleGrid(bool _bVisible)
{
    m_aReportWindow->toggleGrid(_bVisible);
}

sal_uInt16 OScrollWindowHelper::getSectionCount() const
{
    return m_aReportWindow->getSectionCount();
}

void OScrollWindowHelper::SetInsertObj(SdrObjKind eObj, const OUString& _sShapeType)
{
    m_aReportWindow->SetInsertObj(eObj,_sShapeType);
}

OUString const & OScrollWindowHelper::GetInsertObjString() const
{
    return m_aReportWindow->GetInsertObjString();
}

void OScrollWindowHelper::SetMode( DlgEdMode _eNewMode )
{
    m_aReportWindow->SetMode(_eNewMode);
}

bool OScrollWindowHelper::HasSelection() const
{
    return m_aReportWindow->HasSelection();
}

void OScrollWindowHelper::Delete()
{
    m_aReportWindow->Delete();
}

void OScrollWindowHelper::Copy()
{
    m_aReportWindow->Copy();
}

void OScrollWindowHelper::Paste()
{
    m_aReportWindow->Paste();
}

bool OScrollWindowHelper::IsPasteAllowed() const
{
    return m_aReportWindow->IsPasteAllowed();
}

void OScrollWindowHelper::SelectAll(const SdrObjKind _nObjectType)
{
    m_aReportWindow->SelectAll(_nObjectType);
}

void OScrollWindowHelper::unmarkAllObjects()
{
    m_aReportWindow->unmarkAllObjects();
}

sal_Int32 OScrollWindowHelper::getMaxMarkerWidth() const
{
    return m_aReportWindow->getMaxMarkerWidth();
}

void OScrollWindowHelper::showRuler(bool _bShow)
{
    m_aReportWindow->showRuler(_bShow);
}

bool OScrollWindowHelper::handleKeyEvent(const KeyEvent& _rEvent)
{
    return m_aReportWindow->handleKeyEvent(_rEvent);
}

void OScrollWindowHelper::setMarked(OSectionView const * _pSectionView, bool _bMark)
{
    m_aReportWindow->setMarked(_pSectionView,_bMark);
}

void OScrollWindowHelper::setMarked(const uno::Reference< report::XSection>& _xSection, bool _bMark)
{
    m_aReportWindow->setMarked(_xSection,_bMark);
}

void OScrollWindowHelper::setMarked(const uno::Sequence< uno::Reference< report::XReportComponent> >& _xShape, bool _bMark)
{
    m_aReportWindow->setMarked(_xShape,_bMark);
}

OSectionWindow* OScrollWindowHelper::getMarkedSection(NearSectionAccess nsa) const
{
    return m_aReportWindow->getMarkedSection(nsa);
}

OSectionWindow* OScrollWindowHelper::getSectionWindow(const css::uno::Reference< css::report::XSection>& _xSection) const
{
    return  m_aReportWindow->getSectionWindow(_xSection);
}

void OScrollWindowHelper::markSection(const sal_uInt16 _nPos)
{
    m_aReportWindow->markSection(_nPos);
}

void OScrollWindowHelper::fillCollapsedSections(::std::vector<sal_uInt16>& _rCollapsedPositions) const
{
    m_aReportWindow->fillCollapsedSections(_rCollapsedPositions);
}

void OScrollWindowHelper::collapseSections(const uno::Sequence< css::beans::PropertyValue>& _aCollapsedSections)
{
    m_aReportWindow->collapseSections(_aCollapsedSections);
}

bool OScrollWindowHelper::EventNotify( NotifyEvent& rNEvt )
{
    const CommandEvent* pCommandEvent = rNEvt.GetCommandEvent();
    if ( pCommandEvent &&
        ((pCommandEvent->GetCommand() == CommandEventId::Wheel) ||
         (pCommandEvent->GetCommand() == CommandEventId::StartAutoScroll) ||
         (pCommandEvent->GetCommand() == CommandEventId::AutoScroll)) )
    {
        ScrollAdaptor* pHScrBar = nullptr;
        ScrollAdaptor* pVScrBar = nullptr;
        if ( m_aHScroll->IsVisible() )
            pHScrBar = m_aHScroll.get();

        if ( m_aVScroll->IsVisible() )
            pVScrBar = m_aVScroll.get();

        if ( HandleScrollCommand( *pCommandEvent, pHScrBar, pVScrBar ) )
            return true;
    }
    return vcl::Window::EventNotify(rNEvt);
}

void OScrollWindowHelper::alignMarkedObjects(ControlModification _nControlModification, bool _bAlignAtSection)
{
    m_aReportWindow->alignMarkedObjects(_nControlModification, _bAlignAtSection);
}

void OScrollWindowHelper::ImplInitSettings()
{
    SetBackground( Wallpaper( Application::GetSettings().GetStyleSettings().GetFaceColor() ));
    GetOutDev()->SetFillColor( Application::GetSettings().GetStyleSettings().GetFaceColor() );
    SetTextFillColor( Application::GetSettings().GetStyleSettings().GetFaceColor() );
}

void OScrollWindowHelper::DataChanged( const DataChangedEvent& rDCEvt )
{
    Window::DataChanged( rDCEvt );

    if ( (rDCEvt.GetType() == DataChangedEventType::SETTINGS) &&
         (rDCEvt.GetFlags() & AllSettingsFlags::STYLE) )
    {
        ImplInitSettings();
        Invalidate();
    }
}

void OScrollWindowHelper::_propertyChanged(const beans::PropertyChangeEvent& /*_rEvent*/)
{
    m_aReportWindow->notifySizeChanged();
}

void OScrollWindowHelper::setGridSnap(bool bOn)
{
    m_aReportWindow->setGridSnap(bOn);
}

void OScrollWindowHelper::setDragStripes(bool bOn)
{
    m_aReportWindow->setDragStripes(bOn);
}

sal_uInt32 OScrollWindowHelper::getMarkedObjectCount() const
{
    return m_aReportWindow->getMarkedObjectCount();
}

void OScrollWindowHelper::zoom(const Fraction& _aZoom)
{
    m_aReportWindow->zoom(_aZoom);
    Resize();
    Invalidate(InvalidateFlags::NoChildren|InvalidateFlags::Transparent);
}

void OScrollWindowHelper::fillControlModelSelection(::std::vector< uno::Reference< uno::XInterface > >& _rSelection) const
{
    m_aReportWindow->fillControlModelSelection(_rSelection);
}

sal_uInt16 OScrollWindowHelper::getZoomFactor(SvxZoomType _eType) const
{
    return m_aReportWindow->getZoomFactor(_eType);
}

} // rptui


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
