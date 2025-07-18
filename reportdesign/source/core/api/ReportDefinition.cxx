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

#include <sal/config.h>

#include <vector>
#include <string_view>

#include <officecfg/Office/Common.hxx>
#include <ReportDefinition.hxx>

#include <Functions.hxx>
#include <Groups.hxx>
#include <ReportComponent.hxx>
#include <ReportHelperImpl.hxx>
#include <RptDef.hxx>
#include <RptModel.hxx>
#include <Section.hxx>
#include <Tools.hxx>
#include <UndoEnv.hxx>
#include <strings.hrc>
#include <core_resource.hxx>
#include <strings.hxx>
#include <xmlExport.hxx>

#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/beans/XMultiPropertyStates.hpp>
#include <com/sun/star/chart2/data/DatabaseDataProvider.hpp>
#include <com/sun/star/datatransfer/UnsupportedFlavorException.hpp>
#include <com/sun/star/document/DocumentProperties.hpp>
#include <com/sun/star/document/IndexedPropertyValues.hpp>
#include <com/sun/star/document/EventObject.hpp>
#include <com/sun/star/document/XEventListener.hpp>
#include <com/sun/star/document/XExporter.hpp>
#include <com/sun/star/document/XFilter.hpp>
#include <com/sun/star/document/XImporter.hpp>
#include <com/sun/star/embed/Aspects.hpp>
#include <com/sun/star/embed/ElementModes.hpp>
#include <com/sun/star/embed/EmbedMapUnits.hpp>
#include <com/sun/star/embed/XTransactedObject.hpp>
#include <com/sun/star/embed/StorageFactory.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/io/IOException.hpp>
#include <com/sun/star/io/XSeekable.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <com/sun/star/lang/XSingleServiceFactory.hpp>
#include <com/sun/star/report/GroupKeepTogether.hpp>
#include <com/sun/star/report/ReportPrintOption.hpp>
#include <com/sun/star/sdb/CommandType.hpp>
#include <com/sun/star/style/GraphicLocation.hpp>
#include <com/sun/star/style/NumberingType.hpp>
#include <com/sun/star/style/PageStyleLayout.hpp>
#include <com/sun/star/style/XStyle.hpp>
#include <com/sun/star/table/BorderLine2.hpp>
#include <com/sun/star/table/ShadowFormat.hpp>
#include <com/sun/star/task/InteractionHandler.hpp>
#include <com/sun/star/task/XStatusIndicator.hpp>
#include <com/sun/star/ui/UIConfigurationManager.hpp>
#include <com/sun/star/util/CloseVetoException.hpp>
#include <com/sun/star/util/NumberFormatsSupplier.hpp>
#include <com/sun/star/xml/AttributeData.hpp>
#include <com/sun/star/xml/sax/Writer.hpp>

#include <comphelper/broadcasthelper.hxx>
#include <comphelper/documentconstants.hxx>
#include <comphelper/genericpropertyset.hxx>
#include <comphelper/indexedpropertyvalues.hxx>
#include <unotools/mediadescriptor.hxx>
#include <comphelper/namecontainer.hxx>
#include <comphelper/namedvaluecollection.hxx>
#include <comphelper/numberedcollection.hxx>
#include <comphelper/proparrhlp.hxx>
#include <comphelper/propertysetinfo.hxx>
#include <comphelper/propertystatecontainer.hxx>
#include <comphelper/seqstream.hxx>
#include <comphelper/sequence.hxx>
#include <comphelper/servicehelper.hxx>
#include <comphelper/storagehelper.hxx>
#include <comphelper/uno3.hxx>
#include <comphelper/interfacecontainer3.hxx>
#include <connectivity/CommonTools.hxx>
#include <connectivity/dbtools.hxx>
#include <cppuhelper/exc_hlp.hxx>
#include <cppuhelper/implbase.hxx>
#include <cppuhelper/interfacecontainer.h>
#include <cppuhelper/supportsservice.hxx>
#include <comphelper/types.hxx>
#include <dbaccess/dbaundomanager.hxx>
#include <editeng/paperinf.hxx>
#include <framework/titlehelper.hxx>
#include <o3tl/safeint.hxx>
#include <svl/itempool.hxx>
#include <svl/undo.hxx>
#include <svx/svdlayer.hxx>
#include <svx/unofill.hxx>
#include <svx/xmleohlp.hxx>
#include <svx/xmlgrhlp.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <vcl/svapp.hxx>

//  page styles
constexpr OUStringLiteral SC_UNO_PAGE_LEFTBORDER = u"LeftBorder";
constexpr OUStringLiteral SC_UNO_PAGE_RIGHTBORDER = u"RightBorder";
constexpr OUStringLiteral SC_UNO_PAGE_BOTTBORDER = u"BottomBorder";
constexpr OUStringLiteral SC_UNO_PAGE_TOPBORDER = u"TopBorder";
constexpr OUStringLiteral SC_UNO_PAGE_LEFTBRDDIST = u"LeftBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_RIGHTBRDDIST = u"RightBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_BOTTBRDDIST = u"BottomBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_TOPBRDDIST = u"TopBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_BORDERDIST = u"BorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_SHADOWFORM = u"ShadowFormat";
constexpr OUStringLiteral SC_UNO_PAGE_PAPERTRAY = u"PrinterPaperTray";
constexpr OUStringLiteral SC_UNO_PAGE_SCALEVAL = u"PageScale";
constexpr OUStringLiteral SC_UNO_PAGE_SCALETOPAG = u"ScaleToPages";
constexpr OUStringLiteral SC_UNO_PAGE_SCALETOX = u"ScaleToPagesX";
constexpr OUStringLiteral SC_UNO_PAGE_SCALETOY = u"ScaleToPagesY";
constexpr OUStringLiteral SC_UNO_PAGE_HDRBACKCOL = u"HeaderBackColor";
constexpr OUStringLiteral SC_UNO_PAGE_HDRBACKTRAN = u"HeaderBackTransparent";
constexpr OUStringLiteral SC_UNO_PAGE_HDRGRFFILT = u"HeaderBackGraphicFilter";
constexpr OUStringLiteral SC_UNO_PAGE_HDRGRFLOC = u"HeaderBackGraphicLocation";
constexpr OUStringLiteral SC_UNO_PAGE_HDRGRF = u"HeaderBackGraphic";
constexpr OUStringLiteral SC_UNO_PAGE_HDRLEFTBOR = u"HeaderLeftBorder";
constexpr OUStringLiteral SC_UNO_PAGE_HDRRIGHTBOR = u"HeaderRightBorder";
constexpr OUStringLiteral SC_UNO_PAGE_HDRBOTTBOR = u"HeaderBottomBorder";
constexpr OUStringLiteral SC_UNO_PAGE_HDRTOPBOR = u"HeaderTopBorder";
constexpr OUStringLiteral SC_UNO_PAGE_HDRLEFTBDIS = u"HeaderLeftBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_HDRRIGHTBDIS = u"HeaderRightBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_HDRBOTTBDIS = u"HeaderBottomBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_HDRTOPBDIS = u"HeaderTopBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_HDRBRDDIST = u"HeaderBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_HDRSHADOW = u"HeaderShadowFormat";
constexpr OUStringLiteral SC_UNO_PAGE_HDRLEFTMAR = u"HeaderLeftMargin";
constexpr OUStringLiteral SC_UNO_PAGE_HDRRIGHTMAR = u"HeaderRightMargin";
constexpr OUStringLiteral SC_UNO_PAGE_HDRBODYDIST = u"HeaderBodyDistance";
constexpr OUStringLiteral SC_UNO_PAGE_HDRHEIGHT = u"HeaderHeight";
constexpr OUStringLiteral SC_UNO_PAGE_HDRON = u"HeaderIsOn";
constexpr OUStringLiteral SC_UNO_PAGE_HDRDYNAMIC = u"HeaderIsDynamicHeight";
constexpr OUStringLiteral SC_UNO_PAGE_HDRSHARED = u"HeaderIsShared";
constexpr OUStringLiteral SC_UNO_PAGE_FIRSTHDRSHARED = u"FirstPageHeaderIsShared";
constexpr OUStringLiteral SC_UNO_PAGE_FTRBACKCOL = u"FooterBackColor";
constexpr OUStringLiteral SC_UNO_PAGE_FTRBACKTRAN = u"FooterBackTransparent";
constexpr OUStringLiteral SC_UNO_PAGE_FTRGRFFILT = u"FooterBackGraphicFilter";
constexpr OUStringLiteral SC_UNO_PAGE_FTRGRFLOC = u"FooterBackGraphicLocation";
constexpr OUStringLiteral SC_UNO_PAGE_FTRGRF = u"FooterBackGraphic";
constexpr OUStringLiteral SC_UNO_PAGE_FTRLEFTBOR = u"FooterLeftBorder";
constexpr OUStringLiteral SC_UNO_PAGE_FTRRIGHTBOR = u"FooterRightBorder";
constexpr OUStringLiteral SC_UNO_PAGE_FTRBOTTBOR = u"FooterBottomBorder";
constexpr OUStringLiteral SC_UNO_PAGE_FTRTOPBOR = u"FooterTopBorder";
constexpr OUStringLiteral SC_UNO_PAGE_FTRLEFTBDIS = u"FooterLeftBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_FTRRIGHTBDIS = u"FooterRightBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_FTRBOTTBDIS = u"FooterBottomBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_FTRTOPBDIS = u"FooterTopBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_FTRBRDDIST = u"FooterBorderDistance";
constexpr OUStringLiteral SC_UNO_PAGE_FTRSHADOW = u"FooterShadowFormat";
constexpr OUStringLiteral SC_UNO_PAGE_FTRLEFTMAR = u"FooterLeftMargin";
constexpr OUStringLiteral SC_UNO_PAGE_FTRRIGHTMAR = u"FooterRightMargin";
constexpr OUStringLiteral SC_UNO_PAGE_FTRBODYDIST = u"FooterBodyDistance";
constexpr OUStringLiteral SC_UNO_PAGE_FTRHEIGHT = u"FooterHeight";
constexpr OUStringLiteral SC_UNO_PAGE_FTRON = u"FooterIsOn";
constexpr OUStringLiteral SC_UNO_PAGE_FTRDYNAMIC = u"FooterIsDynamicHeight";
constexpr OUStringLiteral SC_UNO_PAGE_FTRSHARED = u"FooterIsShared";
constexpr OUStringLiteral SC_UNO_PAGE_FIRSTFTRSHARED = u"FirstPageFooterIsShared";

namespace reportdesign
{
    using namespace com::sun::star;
    using namespace rptui;

static void lcl_setModelReadOnly(const uno::Reference< embed::XStorage >& _xStorage,std::shared_ptr<rptui::OReportModel> const & _rModel)
{
    uno::Reference<beans::XPropertySet> xProp(_xStorage,uno::UNO_QUERY);
    sal_Int32 nOpenMode = embed::ElementModes::READ;
    if ( xProp.is() )
        xProp->getPropertyValue(u"OpenMode"_ustr) >>= nOpenMode;

    _rModel->SetReadOnly((nOpenMode & embed::ElementModes::WRITE) != embed::ElementModes::WRITE);
}
static void lcl_stripLoadArguments( utl::MediaDescriptor& _rDescriptor, uno::Sequence< beans::PropertyValue >& _rArgs )
{
    _rDescriptor.erase( u"StatusIndicator"_ustr );
    _rDescriptor.erase( u"InteractionHandler"_ustr );
    _rDescriptor.erase( u"Model"_ustr );
    _rDescriptor >> _rArgs;
}

static void lcl_extractAndStartStatusIndicator( const utl::MediaDescriptor& _rDescriptor, uno::Reference< task::XStatusIndicator >& _rxStatusIndicator,
    uno::Sequence< uno::Any >& _rCallArgs )
{
    try
    {
        _rxStatusIndicator = _rDescriptor.getUnpackedValueOrDefault( utl::MediaDescriptor::PROP_STATUSINDICATOR, _rxStatusIndicator );
        if ( _rxStatusIndicator.is() )
        {
            _rxStatusIndicator->start( OUString(), sal_Int32(1000000) );

            sal_Int32 nLength = _rCallArgs.getLength();
            _rCallArgs.realloc( nLength + 1 );
            _rCallArgs.getArray()[ nLength ] <<= _rxStatusIndicator;
        }
    }
    catch (const uno::Exception&)
    {
        TOOLS_WARN_EXCEPTION( "reportdesign", "lcl_extractAndStartStatusIndicator" );
    }
}

typedef ::comphelper::OPropertyStateContainer       OStyle_PBASE;

namespace {

class OStyle;

}

typedef ::comphelper::OPropertyArrayUsageHelper <   OStyle
                                                >   OStyle_PABASE;
typedef ::cppu::WeakImplHelper< style::XStyle, beans::XMultiPropertyStates> TStyleBASE;

namespace {

class OStyle :   public ::comphelper::OMutexAndBroadcastHelper
                ,public TStyleBASE
                ,public OStyle_PBASE
                ,public OStyle_PABASE
{
    awt::Size m_aSize;

protected:
    void getPropertyDefaultByHandle( sal_Int32 _nHandle, uno::Any& _rDefault ) const override;
    virtual ~OStyle() override {}
public:
    OStyle();


    DECLARE_XINTERFACE( )

    // XPropertySet
    css::uno::Reference<css::beans::XPropertySetInfo>  SAL_CALL getPropertySetInfo() override;
    ::cppu::IPropertyArrayHelper& SAL_CALL getInfoHelper() override;
    ::cppu::IPropertyArrayHelper* createArrayHelper( ) const override;

    // XStyle
    sal_Bool SAL_CALL isUserDefined(  ) override;
    sal_Bool SAL_CALL isInUse(  ) override;
    OUString SAL_CALL getParentStyle(  ) override;
    void SAL_CALL setParentStyle( const OUString& aParentStyle ) override;

    // XNamed
    OUString SAL_CALL getName(  ) override;
    void SAL_CALL setName( const OUString& aName ) override;

    // XMultiPropertyState
    uno::Sequence< beans::PropertyState > SAL_CALL getPropertyStates( const uno::Sequence< OUString >& aPropertyNames ) override
    {
        return OStyle_PBASE::getPropertyStates(aPropertyNames);
    }
    void SAL_CALL setAllPropertiesToDefault(  ) override;
    void SAL_CALL setPropertiesToDefault( const uno::Sequence< OUString >& aPropertyNames ) override;
    uno::Sequence< uno::Any > SAL_CALL getPropertyDefaults( const uno::Sequence< OUString >& aPropertyNames ) override;
};

}

OStyle::OStyle()
:OStyle_PBASE(m_aBHelper)
,m_aSize(21000,29700)
{
    const ::Size aDefaultSize = SvxPaperInfo::GetDefaultPaperSize( MapUnit::Map100thMM );
    m_aSize.Height = aDefaultSize.Height();
    m_aSize.Width = aDefaultSize.Width();

    const sal_Int32 nMargin = 2000;
    const sal_Int32 nBound = beans::PropertyAttribute::BOUND;
    const sal_Int32 nMayBeVoid = beans::PropertyAttribute::MAYBEVOID;

    sal_Int32 i = 0;
    registerPropertyNoMember( PROPERTY_NAME, ++i, nBound, cppu::UnoType<OUString>::get(), css::uno::Any(u"Default"_ustr) );

    registerPropertyNoMember(PROPERTY_BACKCOLOR,                    ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(COL_TRANSPARENT));

    registerPropertyNoMember(PROPERTY_BACKGRAPHICLOCATION,  ++i,nBound, cppu::UnoType<style::GraphicLocation>::get(), css::uno::Any(style::GraphicLocation_NONE));
    registerPropertyNoMember(PROPERTY_BACKTRANSPARENT,  ++i,nBound,cppu::UnoType<bool>::get(), css::uno::Any(true));
    registerPropertyNoMember(SC_UNO_PAGE_BORDERDIST,  ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_BOTTBORDER,  ++i,nBound, cppu::UnoType<table::BorderLine2>::get(), css::uno::Any(table::BorderLine2()));
    registerPropertyNoMember(SC_UNO_PAGE_BOTTBRDDIST, ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(PROPERTY_BOTTOMMARGIN, ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(nMargin));
    registerPropertyNoMember(u"DisplayName"_ustr,       ++i,nBound, cppu::UnoType<OUString>::get(), css::uno::Any(OUString()));
    registerPropertyNoMember(SC_UNO_PAGE_FTRBACKCOL,  ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(COL_TRANSPARENT));
    registerPropertyNoMember(SC_UNO_PAGE_FTRGRFFILT,  ++i,nBound, cppu::UnoType<OUString>::get(), css::uno::Any(OUString()));
    registerPropertyNoMember(SC_UNO_PAGE_FTRGRFLOC,   ++i,nBound, cppu::UnoType<style::GraphicLocation>::get(), css::uno::Any(style::GraphicLocation_NONE));
    registerPropertyNoMember(SC_UNO_PAGE_FTRGRF,      ++i,nBound|nMayBeVoid, cppu::UnoType<graphic::XGraphic>::get(), css::uno::Any(uno::Reference<graphic::XGraphic>()));
    registerPropertyNoMember(SC_UNO_PAGE_FTRBACKTRAN, ++i,nBound,cppu::UnoType<bool>::get(), css::uno::Any(true));
    registerPropertyNoMember(SC_UNO_PAGE_FTRBODYDIST, ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_FTRBRDDIST,  ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_FTRBOTTBOR,  ++i,nBound, cppu::UnoType<table::BorderLine2>::get(), css::uno::Any(table::BorderLine2()));
    registerPropertyNoMember(SC_UNO_PAGE_FTRBOTTBDIS, ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_FTRHEIGHT,   ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_FTRDYNAMIC,  ++i,nBound,cppu::UnoType<bool>::get(), css::uno::Any(false));
    registerPropertyNoMember(SC_UNO_PAGE_FTRON,       ++i,nBound,cppu::UnoType<bool>::get(), css::uno::Any(false));
    registerPropertyNoMember(SC_UNO_PAGE_FTRSHARED,   ++i,nBound,cppu::UnoType<bool>::get(), css::uno::Any(false));
    registerPropertyNoMember(SC_UNO_PAGE_FIRSTFTRSHARED, ++i,nBound,cppu::UnoType<bool>::get(), css::uno::Any(false));
    registerPropertyNoMember(SC_UNO_PAGE_FTRLEFTBOR,  ++i,nBound, cppu::UnoType<table::BorderLine2>::get(), css::uno::Any(table::BorderLine2()));
    registerPropertyNoMember(SC_UNO_PAGE_FTRLEFTBDIS, ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_FTRLEFTMAR,  ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_FTRRIGHTBOR, ++i,nBound, cppu::UnoType<table::BorderLine2>::get(), css::uno::Any(table::BorderLine2()));
    registerPropertyNoMember(SC_UNO_PAGE_FTRRIGHTBDIS,++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_FTRRIGHTMAR, ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_FTRSHADOW,   ++i,nBound, cppu::UnoType<table::ShadowFormat>::get(), css::uno::Any(table::ShadowFormat()));
    registerPropertyNoMember(SC_UNO_PAGE_FTRTOPBOR,   ++i,nBound, cppu::UnoType<table::BorderLine2>::get(), css::uno::Any(table::BorderLine2()));
    registerPropertyNoMember(SC_UNO_PAGE_FTRTOPBDIS,  ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));

    registerPropertyNoMember(SC_UNO_PAGE_HDRBACKCOL,  ++i,nBound|nMayBeVoid, cppu::UnoType<sal_Int32>::get(), css::uno::Any(COL_TRANSPARENT));
    registerPropertyNoMember(SC_UNO_PAGE_HDRGRFFILT,  ++i,nBound|nMayBeVoid, cppu::UnoType<OUString>::get(), css::uno::Any(OUString()));
    registerPropertyNoMember(SC_UNO_PAGE_HDRGRFLOC,   ++i,nBound|nMayBeVoid, cppu::UnoType<style::GraphicLocation>::get(), css::uno::Any(style::GraphicLocation_NONE));
    registerPropertyNoMember(SC_UNO_PAGE_HDRGRF,      ++i,nBound|nMayBeVoid, cppu::UnoType<graphic::XGraphic>::get(), css::uno::Any(uno::Reference<graphic::XGraphic>()));
    registerPropertyNoMember(SC_UNO_PAGE_HDRBACKTRAN, ++i,nBound|nMayBeVoid,cppu::UnoType<bool>::get(), css::uno::Any(true));
    registerPropertyNoMember(SC_UNO_PAGE_HDRBODYDIST, ++i,nBound|nMayBeVoid, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_HDRBRDDIST,  ++i,nBound|nMayBeVoid, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_HDRBOTTBOR,  ++i,nBound|nMayBeVoid, cppu::UnoType<table::BorderLine2>::get(), css::uno::Any(table::BorderLine2()));
    registerPropertyNoMember(SC_UNO_PAGE_HDRBOTTBDIS, ++i,nBound|nMayBeVoid, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_HDRHEIGHT,   ++i,nBound|nMayBeVoid, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_HDRDYNAMIC,  ++i,nBound|nMayBeVoid,cppu::UnoType<bool>::get(), css::uno::Any(false));
    registerPropertyNoMember(SC_UNO_PAGE_HDRON,       ++i,nBound|nMayBeVoid,cppu::UnoType<bool>::get(), css::uno::Any(false));
    registerPropertyNoMember(SC_UNO_PAGE_HDRSHARED,   ++i,nBound|nMayBeVoid,cppu::UnoType<bool>::get(), css::uno::Any(false));
    registerPropertyNoMember(SC_UNO_PAGE_FIRSTHDRSHARED, ++i,nBound|nMayBeVoid,cppu::UnoType<bool>::get(), css::uno::Any(false));
    registerPropertyNoMember(SC_UNO_PAGE_HDRLEFTBOR,  ++i,nBound|nMayBeVoid, cppu::UnoType<table::BorderLine2>::get(), css::uno::Any(table::BorderLine2()));
    registerPropertyNoMember(SC_UNO_PAGE_HDRLEFTBDIS, ++i,nBound|nMayBeVoid, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_HDRLEFTMAR,  ++i,nBound|nMayBeVoid, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_HDRRIGHTBOR, ++i,nBound|nMayBeVoid, cppu::UnoType<table::BorderLine2>::get(), css::uno::Any(table::BorderLine2()));
    registerPropertyNoMember(SC_UNO_PAGE_HDRRIGHTBDIS,++i,nBound|nMayBeVoid, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_HDRRIGHTMAR, ++i,nBound|nMayBeVoid, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(SC_UNO_PAGE_HDRSHADOW,   ++i,nBound|nMayBeVoid, cppu::UnoType<table::ShadowFormat>::get(), css::uno::Any(table::ShadowFormat()));
    registerPropertyNoMember(SC_UNO_PAGE_HDRTOPBOR,   ++i,nBound|nMayBeVoid, cppu::UnoType<table::BorderLine2>::get(), css::uno::Any(table::BorderLine2()));
    registerPropertyNoMember(SC_UNO_PAGE_HDRTOPBDIS,  ++i,nBound|nMayBeVoid, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));

    registerProperty(PROPERTY_HEIGHT,       ++i,nBound,&m_aSize.Height,     ::cppu::UnoType<sal_Int32>::get() );
    registerPropertyNoMember(PROPERTY_ISLANDSCAPE,                  ++i,nBound,         cppu::UnoType<bool>::get(), css::uno::Any(false));
    registerPropertyNoMember(SC_UNO_PAGE_LEFTBORDER,  ++i,nBound, cppu::UnoType<table::BorderLine2>::get(), css::uno::Any(table::BorderLine2()));
    registerPropertyNoMember(SC_UNO_PAGE_LEFTBRDDIST, ++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(PROPERTY_LEFTMARGIN,   ++i,beans::PropertyAttribute::BOUND,        ::cppu::UnoType<sal_Int32>::get(), css::uno::Any(nMargin));
    registerPropertyNoMember(PROPERTY_NUMBERINGTYPE,                ++i,nBound, cppu::UnoType<sal_Int16>::get(), css::uno::Any(style::NumberingType::ARABIC));
    registerPropertyNoMember(SC_UNO_PAGE_SCALEVAL,    ++i,nBound, cppu::UnoType<sal_Int16>::get(), css::uno::Any(sal_Int16(0)));
    registerPropertyNoMember(PROPERTY_PAGESTYLELAYOUT,              ++i,nBound, cppu::UnoType<style::PageStyleLayout>::get(), css::uno::Any(style::PageStyleLayout_ALL));
    registerPropertyNoMember(SC_UNO_PAGE_PAPERTRAY,   ++i,nBound, cppu::UnoType<OUString>::get(), css::uno::Any(u"[From printer settings]"_ustr));
    registerPropertyNoMember(SC_UNO_PAGE_RIGHTBORDER, ++i,nBound, cppu::UnoType<table::BorderLine2>::get(), css::uno::Any(table::BorderLine2()));
    registerPropertyNoMember(SC_UNO_PAGE_RIGHTBRDDIST,++i,nBound, cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(PROPERTY_RIGHTMARGIN,  ++i,beans::PropertyAttribute::BOUND,::cppu::UnoType<sal_Int32>::get(), css::uno::Any(nMargin));
    registerPropertyNoMember(SC_UNO_PAGE_SCALETOPAG,  ++i,nBound, cppu::UnoType<sal_Int16>::get(), css::uno::Any(sal_Int16(0)));
    registerPropertyNoMember(SC_UNO_PAGE_SCALETOX,    ++i,nBound, cppu::UnoType<sal_Int16>::get(), css::uno::Any(sal_Int16(0)));
    registerPropertyNoMember(SC_UNO_PAGE_SCALETOY,    ++i,nBound, cppu::UnoType<sal_Int16>::get(), css::uno::Any(sal_Int16(0)));
    registerPropertyNoMember(SC_UNO_PAGE_SHADOWFORM,  ++i,nBound, cppu::UnoType<table::ShadowFormat>::get(), css::uno::Any(table::ShadowFormat()));
    registerProperty(PROPERTY_PAPERSIZE,                    ++i,beans::PropertyAttribute::BOUND,&m_aSize, cppu::UnoType<awt::Size>::get() );
    registerPropertyNoMember(SC_UNO_PAGE_TOPBORDER,   ++i,nBound, cppu::UnoType<table::BorderLine2>::get(), css::uno::Any(table::BorderLine2()));
    registerPropertyNoMember(SC_UNO_PAGE_TOPBRDDIST,  ++i,nBound,::cppu::UnoType<sal_Int32>::get(), css::uno::Any(sal_Int32(0)));
    registerPropertyNoMember(PROPERTY_TOPMARGIN,    ++i,nBound,::cppu::UnoType<sal_Int32>::get(), css::uno::Any(nMargin));
    registerPropertyNoMember(u"UserDefinedAttributes"_ustr,     ++i,nBound, cppu::UnoType<container::XNameContainer>::get(), css::uno::Any(comphelper::NameContainer_createInstance(cppu::UnoType<xml::AttributeData>::get())));
    registerProperty(PROPERTY_WIDTH,        ++i,nBound,&m_aSize.Width, cppu::UnoType<sal_Int32>::get() );
    registerPropertyNoMember(u"PrinterName"_ustr,               ++i,nBound, cppu::UnoType<OUString>::get(), css::uno::Any(OUString()));
    registerPropertyNoMember(u"PrinterSetup"_ustr,              ++i,nBound,cppu::UnoType<uno::Sequence<sal_Int8>>::get(), css::uno::Any(uno::Sequence<sal_Int8>()));


}

IMPLEMENT_FORWARD_XINTERFACE2(OStyle,TStyleBASE,OStyle_PBASE)

uno::Reference< beans::XPropertySetInfo>  SAL_CALL OStyle::getPropertySetInfo()
{
    return createPropertySetInfo( getInfoHelper() );
}

void OStyle::getPropertyDefaultByHandle( sal_Int32 /*_nHandle*/, uno::Any& /*_rDefault*/ ) const
{
}

::cppu::IPropertyArrayHelper& OStyle::getInfoHelper()
{
    return *getArrayHelper();
}

::cppu::IPropertyArrayHelper* OStyle::createArrayHelper( ) const
{
    uno::Sequence< beans::Property > aProps;
    describeProperties(aProps);
    return new ::cppu::OPropertyArrayHelper(aProps);
}

// XStyle
sal_Bool SAL_CALL OStyle::isUserDefined(  )
{
    return false;
}

sal_Bool SAL_CALL OStyle::isInUse(  )
{
    return true;
}

OUString SAL_CALL OStyle::getParentStyle(  )
{
    return OUString();
}

void SAL_CALL OStyle::setParentStyle( const OUString& /*aParentStyle*/ )
{
}

// XNamed
OUString SAL_CALL OStyle::getName(  )
{
    OUString sName;
    getPropertyValue(PROPERTY_NAME) >>= sName;
    return sName;
}

void SAL_CALL OStyle::setName( const OUString& aName )
{
    setPropertyValue(PROPERTY_NAME,uno::Any(aName));
}

void SAL_CALL OStyle::setAllPropertiesToDefault(  )
{
}

void SAL_CALL OStyle::setPropertiesToDefault( const uno::Sequence< OUString >& aPropertyNames )
{
    for(const OUString& rName : aPropertyNames)
        setPropertyToDefault(rName);
}

uno::Sequence< uno::Any > SAL_CALL OStyle::getPropertyDefaults( const uno::Sequence< OUString >& aPropertyNames )
{
    uno::Sequence< uno::Any > aRet(aPropertyNames.getLength());
    std::transform(aPropertyNames.begin(), aPropertyNames.end(), aRet.getArray(),
        [this](const OUString& rName) -> uno::Any { return getPropertyDefault(rName); });
    return aRet;
}

namespace { class OStylesHelper; }

struct OReportDefinitionImpl
{
    uno::WeakReference< uno::XInterface >                   m_xParent;
    ::comphelper::OInterfaceContainerHelper3<document::XStorageChangeListener> m_aStorageChangeListeners;
    ::comphelper::OInterfaceContainerHelper3<util::XCloseListener> m_aCloseListener;
    ::comphelper::OInterfaceContainerHelper3<util::XModifyListener> m_aModifyListeners;
    ::comphelper::OInterfaceContainerHelper3<document::XEventListener> m_aLegacyEventListeners;
    ::comphelper::OInterfaceContainerHelper3<document::XDocumentEventListener> m_aDocEventListeners;
    ::std::vector< uno::Reference< frame::XController> >    m_aControllers;
    uno::Sequence< beans::PropertyValue >                   m_aArgs;

    rtl::Reference< OGroups >                               m_xGroups;
    rtl::Reference< OSection >                              m_xReportHeader;
    rtl::Reference< OSection >                              m_xReportFooter;
    rtl::Reference< OSection >                              m_xPageHeader;
    rtl::Reference< OSection >                              m_xPageFooter;
    rtl::Reference< OSection >                              m_xDetail;
    uno::Reference< embed::XStorage >                       m_xStorage;
    uno::Reference< frame::XController >                    m_xCurrentController;
    uno::Reference< container::XIndexAccess >               m_xViewData;
    rtl::Reference< OStylesHelper >                         m_xStyles;
    uno::Reference< container::XNameAccess>                 m_xXMLNamespaceMap;
    uno::Reference< container::XNameAccess>                 m_xGradientTable;
    uno::Reference< container::XNameAccess>                 m_xHatchTable;
    uno::Reference< container::XNameAccess>                 m_xBitmapTable;
    uno::Reference< container::XNameAccess>                 m_xTransparencyGradientTable;
    uno::Reference< container::XNameAccess>                 m_xDashTable;
    uno::Reference< container::XNameAccess>                 m_xMarkerTable;
    rtl::Reference< OFunctions >                            m_xFunctions;
    uno::Reference< ui::XUIConfigurationManager2>           m_xUIConfigurationManager;
    uno::Reference< util::XNumberFormatsSupplier>           m_xNumberFormatsSupplier;
    uno::Reference< sdbc::XConnection>                      m_xActiveConnection;
    rtl::Reference< ::framework::TitleHelper >              m_xTitleHelper;
    rtl::Reference< ::comphelper::NumberedCollection >      m_xNumberedControllers;
    uno::Reference< document::XDocumentProperties >         m_xDocumentProperties;

    std::shared_ptr< ::comphelper::EmbeddedObjectContainer>
                                                            m_pObjectContainer;
    std::shared_ptr<rptui::OReportModel>                m_pReportModel;
    ::rtl::Reference< ::dbaui::UndoManager >                m_pUndoManager;
    OUString                                         m_sCaption;
    OUString                                         m_sCommand;
    OUString                                         m_sFilter;
    OUString                                         m_sMimeType;
    OUString                                         m_sIdentifier;
    OUString                                         m_sDataSourceName;
    awt::Size                                               m_aVisualAreaSize;
    ::sal_Int64                                             m_nAspect;
    ::sal_Int16                                             m_nGroupKeepTogether;
    ::sal_Int16                                             m_nPageHeaderOption;
    ::sal_Int16                                             m_nPageFooterOption;
    ::sal_Int32                                             m_nCommandType;
    bool                                                    m_bControllersLocked;
    bool                                                    m_bModified;
    bool                                                    m_bEscapeProcessing;
    bool                                                    m_bSetModifiedEnabled;

    explicit OReportDefinitionImpl(::osl::Mutex& _aMutex)
    :m_aStorageChangeListeners(_aMutex)
    ,m_aCloseListener(_aMutex)
    ,m_aModifyListeners(_aMutex)
    ,m_aLegacyEventListeners(_aMutex)
    ,m_aDocEventListeners(_aMutex)
    ,m_sMimeType(MIMETYPE_OASIS_OPENDOCUMENT_TEXT_ASCII)
    ,m_sIdentifier(SERVICE_REPORTDEFINITION)
    // default visual area is 8 x 7 cm
    ,m_aVisualAreaSize( 8000, 7000 )
    ,m_nAspect(embed::Aspects::MSOLE_CONTENT)
    ,m_nGroupKeepTogether(0)
    ,m_nPageHeaderOption(0)
    ,m_nPageFooterOption(0)
    ,m_nCommandType(sdb::CommandType::TABLE)
    ,m_bControllersLocked(false)
    ,m_bModified(false)
    ,m_bEscapeProcessing(true)
    ,m_bSetModifiedEnabled( true )
    {}
};

OReportDefinition::OReportDefinition(uno::Reference< uno::XComponentContext > const & _xContext)
:   ::cppu::BaseMutex(),
    ReportDefinitionBase(m_aMutex),
    ReportDefinitionPropertySet(_xContext,IMPLEMENTS_PROPERTY_SET,uno::Sequence< OUString >()),
    ::comphelper::IEmbeddedHelper(),
    m_aProps(std::make_shared<OReportComponentProperties>(_xContext)),
    m_pImpl(std::make_shared<OReportDefinitionImpl>(m_aMutex))
{
    m_aProps->m_sName  = RptResId(RID_STR_REPORT);
    osl_atomic_increment(&m_refCount);
    {
        init();
        m_pImpl->m_xGroups = new OGroups(this,m_aProps->m_xContext);
        m_pImpl->m_xDetail = OSection::createOSection(this,m_aProps->m_xContext);
        m_pImpl->m_xDetail->setName(RptResId(RID_STR_DETAIL));
    }
    osl_atomic_decrement( &m_refCount );
}

OReportDefinition::OReportDefinition(
    uno::Reference< uno::XComponentContext > const & _xContext,
    const uno::Reference< lang::XMultiServiceFactory>& _xFactory,
    uno::Reference< drawing::XShape >& _xShape)
:   ::cppu::BaseMutex(),
    ReportDefinitionBase(m_aMutex),
    ReportDefinitionPropertySet(_xContext,IMPLEMENTS_PROPERTY_SET,uno::Sequence< OUString >()),
    ::comphelper::IEmbeddedHelper(),
    m_aProps(std::make_shared<OReportComponentProperties>(_xContext)),
    m_pImpl(std::make_shared<OReportDefinitionImpl>(m_aMutex))
{
    m_aProps->m_sName  = RptResId(RID_STR_REPORT);
    m_aProps->m_xFactory = _xFactory;
    osl_atomic_increment(&m_refCount);
    {
        m_aProps->setShape(_xShape,this,m_refCount);
        init();
        m_pImpl->m_xGroups = new OGroups(this,m_aProps->m_xContext);
        m_pImpl->m_xDetail = OSection::createOSection(this,m_aProps->m_xContext);
        m_pImpl->m_xDetail->setName(RptResId(RID_STR_DETAIL));
    }
    osl_atomic_decrement( &m_refCount );
}

OReportDefinition::~OReportDefinition()
{
    if ( !ReportDefinitionBase::rBHelper.bInDispose && !ReportDefinitionBase::rBHelper.bDisposed )
    {
        acquire();
        dispose();
    }
}

IMPLEMENT_FORWARD_REFCOUNT( OReportDefinition, ReportDefinitionBase )
void OReportDefinition::init()
{
    try
    {
        m_pImpl->m_pReportModel = std::make_shared<OReportModel>(this);
        m_pImpl->m_pReportModel->SetScaleUnit( MapUnit::Map100thMM );
        SdrLayerAdmin& rAdmin = m_pImpl->m_pReportModel->GetLayerAdmin();
        rAdmin.NewLayer(u"front"_ustr, RPT_LAYER_FRONT.get());
        rAdmin.NewLayer(u"back"_ustr, RPT_LAYER_BACK.get());
        rAdmin.NewLayer(u"HiddenLayer"_ustr, RPT_LAYER_HIDDEN.get());

        m_pImpl->m_pUndoManager = new ::dbaui::UndoManager( *this, m_aMutex );
        m_pImpl->m_pReportModel->SetSdrUndoManager( &m_pImpl->m_pUndoManager->GetSfxUndoManager() );

        m_pImpl->m_xFunctions = new OFunctions(this,m_aProps->m_xContext);
        if ( !m_pImpl->m_xStorage.is() )
            m_pImpl->m_xStorage = ::comphelper::OStorageHelper::GetTemporaryStorage();

        uno::Reference<beans::XPropertySet> xStorProps(m_pImpl->m_xStorage,uno::UNO_QUERY);
        if ( xStorProps.is())
        {
            OUString sMediaType;
            xStorProps->getPropertyValue(u"MediaType"_ustr) >>= sMediaType;
            if ( sMediaType.isEmpty() )
                xStorProps->setPropertyValue(u"MediaType"_ustr,uno::Any(MIMETYPE_OASIS_OPENDOCUMENT_REPORT_ASCII));
        }
        m_pImpl->m_pObjectContainer = std::make_shared<comphelper::EmbeddedObjectContainer>(m_pImpl->m_xStorage , getXWeak() );
    }
    catch (const uno::Exception&)
    {
        DBG_UNHANDLED_EXCEPTION("reportdesign");
    }
}

void SAL_CALL OReportDefinition::dispose()
{
    ReportDefinitionPropertySet::dispose();
    cppu::WeakComponentImplHelperBase::dispose();
}

void SAL_CALL OReportDefinition::disposing()
{
    notifyEvent(u"OnUnload"_ustr);

    uno::Reference< frame::XModel > xHoldAlive( this );

    lang::EventObject aDisposeEvent( getXWeak() );
    m_pImpl->m_aModifyListeners.disposeAndClear( aDisposeEvent );
    m_pImpl->m_aCloseListener.disposeAndClear( aDisposeEvent );
    m_pImpl->m_aLegacyEventListeners.disposeAndClear( aDisposeEvent );
    m_pImpl->m_aDocEventListeners.disposeAndClear( aDisposeEvent );
    m_pImpl->m_aStorageChangeListeners.disposeAndClear( aDisposeEvent );

    // SYNCHRONIZED --->
    {
    SolarMutexGuard aSolarGuard;
    osl::MutexGuard aGuard(m_aMutex);

    m_pImpl->m_aControllers.clear();

    ::comphelper::disposeComponent(m_pImpl->m_xGroups);
    m_pImpl->m_xReportHeader.clear();
    m_pImpl->m_xReportFooter.clear();
    m_pImpl->m_xPageHeader.clear();
    m_pImpl->m_xPageFooter.clear();
    m_pImpl->m_xDetail.clear();
    ::comphelper::disposeComponent(m_pImpl->m_xFunctions);

    //::comphelper::disposeComponent(m_pImpl->m_xStorage);
        // don't dispose, this currently is the task of either the ref count going to
        // 0, or of the embedded object (if we're embedded, which is the only possible
        // case so far)
        // #i78366#
    m_pImpl->m_xStorage.clear();
    m_pImpl->m_xViewData.clear();
    m_pImpl->m_xCurrentController.clear();
    m_pImpl->m_xNumberFormatsSupplier.clear();
    m_pImpl->m_xStyles.clear();
    m_pImpl->m_xXMLNamespaceMap.clear();
    m_pImpl->m_xGradientTable.clear();
    m_pImpl->m_xHatchTable.clear();
    m_pImpl->m_xBitmapTable.clear();
    m_pImpl->m_xTransparencyGradientTable.clear();
    m_pImpl->m_xDashTable.clear();
    m_pImpl->m_xMarkerTable.clear();
    m_pImpl->m_xUIConfigurationManager.clear();
    m_pImpl->m_pReportModel.reset();
    m_pImpl->m_pObjectContainer.reset();
    m_pImpl->m_aArgs.realloc(0);
    m_pImpl->m_xTitleHelper.clear();
    m_pImpl->m_xNumberedControllers.clear();
    }
    // <--- SYNCHRONIZED
}


OUString OReportDefinition::getImplementationName_Static(  )
{
    return u"com.sun.star.comp.report.OReportDefinition"_ustr;
}

OUString SAL_CALL OReportDefinition::getImplementationName(  )
{
    return getImplementationName_Static();
}

uno::Sequence< OUString > OReportDefinition::getSupportedServiceNames_Static(  )
{
    uno::Sequence< OUString > aServices { SERVICE_REPORTDEFINITION };

    return aServices;
}

uno::Sequence< OUString > SAL_CALL OReportDefinition::getSupportedServiceNames(  )
{
    // first collect the services which are supported by our aggregate
    uno::Sequence< OUString > aSupported;
    if ( m_aProps->m_xServiceInfo.is() )
        aSupported = m_aProps->m_xServiceInfo->getSupportedServiceNames();

    // append our own service, if necessary
    if ( ::comphelper::findValue( aSupported, SERVICE_REPORTDEFINITION ) == -1 )
    {
        sal_Int32 nLen = aSupported.getLength();
        aSupported.realloc( nLen + 1 );
        aSupported.getArray()[ nLen ] = SERVICE_REPORTDEFINITION;
    }

    // outta here
    return aSupported;
}

sal_Bool SAL_CALL OReportDefinition::supportsService( const OUString& _rServiceName )
{
    return cppu::supportsService(this, _rServiceName);
}

uno::Any SAL_CALL OReportDefinition::queryInterface( const uno::Type& _rType )
{
    uno::Any aReturn = ReportDefinitionBase::queryInterface(_rType);
    if ( !aReturn.hasValue() )
        aReturn = ReportDefinitionPropertySet::queryInterface(_rType);

    return aReturn.hasValue() ? aReturn : (m_aProps->m_xProxy.is() ? m_aProps->m_xProxy->queryAggregation(_rType) : aReturn);
}
uno::Sequence< uno::Type > SAL_CALL OReportDefinition::getTypes(  )
{
    if ( m_aProps->m_xTypeProvider.is() )
        return ::comphelper::concatSequences(
            ReportDefinitionBase::getTypes(),
            m_aProps->m_xTypeProvider->getTypes()
        );
    return ReportDefinitionBase::getTypes();
}

uno::Reference< uno::XInterface > OReportDefinition::create(uno::Reference< uno::XComponentContext > const & xContext)
{
    return *(new OReportDefinition(xContext));
}

// XReportDefinition
OUString SAL_CALL OReportDefinition::getCaption()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_sCaption;
}

void SAL_CALL OReportDefinition::setCaption( const OUString& _caption )
{
    set(PROPERTY_CAPTION,_caption,m_pImpl->m_sCaption);
}

::sal_Int16 SAL_CALL OReportDefinition::getGroupKeepTogether()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_nGroupKeepTogether;
}

void SAL_CALL OReportDefinition::setGroupKeepTogether( ::sal_Int16 _groupkeeptogether )
{
    if ( _groupkeeptogether < report::GroupKeepTogether::PER_PAGE || _groupkeeptogether > report::GroupKeepTogether::PER_COLUMN )
        throwIllegallArgumentException(u"css::report::GroupKeepTogether"
                        ,*this
                        ,1);
    set(PROPERTY_GROUPKEEPTOGETHER,_groupkeeptogether,m_pImpl->m_nGroupKeepTogether);
}

::sal_Int16 SAL_CALL OReportDefinition::getPageHeaderOption()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_nPageHeaderOption;
}

void SAL_CALL OReportDefinition::setPageHeaderOption( ::sal_Int16 _pageheaderoption )
{
    if ( _pageheaderoption < report::ReportPrintOption::ALL_PAGES || _pageheaderoption > report::ReportPrintOption::NOT_WITH_REPORT_HEADER_FOOTER )
        throwIllegallArgumentException(u"css::report::ReportPrintOption"
                        ,*this
                        ,1);
    set(PROPERTY_PAGEHEADEROPTION,_pageheaderoption,m_pImpl->m_nPageHeaderOption);
}

::sal_Int16 SAL_CALL OReportDefinition::getPageFooterOption()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_nPageFooterOption;
}

void SAL_CALL OReportDefinition::setPageFooterOption( ::sal_Int16 _pagefooteroption )
{
    if ( _pagefooteroption < report::ReportPrintOption::ALL_PAGES || _pagefooteroption > report::ReportPrintOption::NOT_WITH_REPORT_HEADER_FOOTER )
        throwIllegallArgumentException(u"css::report::ReportPrintOption"
                        ,*this
                        ,1);
    set(PROPERTY_PAGEFOOTEROPTION,_pagefooteroption,m_pImpl->m_nPageFooterOption);
}

OUString SAL_CALL OReportDefinition::getCommand()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_sCommand;
}

void SAL_CALL OReportDefinition::setCommand( const OUString& _command )
{
    set(PROPERTY_COMMAND,_command,m_pImpl->m_sCommand);
}

::sal_Int32 SAL_CALL OReportDefinition::getCommandType()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_nCommandType;
}

void SAL_CALL OReportDefinition::setCommandType( ::sal_Int32 _commandtype )
{
    if ( _commandtype < sdb::CommandType::TABLE || _commandtype > sdb::CommandType::COMMAND )
        throwIllegallArgumentException(u"css::sdb::CommandType"
                        ,*this
                        ,1);
    set(PROPERTY_COMMANDTYPE,_commandtype,m_pImpl->m_nCommandType);
}

OUString SAL_CALL OReportDefinition::getFilter()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_sFilter;
}

void SAL_CALL OReportDefinition::setFilter( const OUString& _filter )
{
    set(PROPERTY_FILTER,_filter,m_pImpl->m_sFilter);
}

sal_Bool SAL_CALL OReportDefinition::getEscapeProcessing()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_bEscapeProcessing;
}

void SAL_CALL OReportDefinition::setEscapeProcessing( sal_Bool _escapeprocessing )
{
    set(PROPERTY_ESCAPEPROCESSING,_escapeprocessing,m_pImpl->m_bEscapeProcessing);
}

sal_Bool SAL_CALL OReportDefinition::getReportHeaderOn()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_xReportHeader.is();
}

void SAL_CALL OReportDefinition::setReportHeaderOn( sal_Bool _reportheaderon )
{
    if ( bool(_reportheaderon) != m_pImpl->m_xReportHeader.is() )
    {
        setSection(PROPERTY_REPORTHEADERON,_reportheaderon,RptResId(RID_STR_REPORT_HEADER),m_pImpl->m_xReportHeader);
    }
}

sal_Bool SAL_CALL OReportDefinition::getReportFooterOn()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_xReportFooter.is();
}

void SAL_CALL OReportDefinition::setReportFooterOn( sal_Bool _reportfooteron )
{
    if ( bool(_reportfooteron) != m_pImpl->m_xReportFooter.is() )
    {
        setSection(PROPERTY_REPORTFOOTERON,_reportfooteron,RptResId(RID_STR_REPORT_FOOTER),m_pImpl->m_xReportFooter);
    }
}

sal_Bool SAL_CALL OReportDefinition::getPageHeaderOn()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_xPageHeader.is();
}

void SAL_CALL OReportDefinition::setPageHeaderOn( sal_Bool _pageheaderon )
{
    if ( bool(_pageheaderon) != m_pImpl->m_xPageHeader.is() )
    {
        setSection(PROPERTY_PAGEHEADERON,_pageheaderon,RptResId(RID_STR_PAGE_HEADER),m_pImpl->m_xPageHeader);
    }
}

sal_Bool SAL_CALL OReportDefinition::getPageFooterOn()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_xPageFooter.is();
}

void SAL_CALL OReportDefinition::setPageFooterOn( sal_Bool _pagefooteron )
{
    if ( bool(_pagefooteron) != m_pImpl->m_xPageFooter.is() )
    {
        setSection(PROPERTY_PAGEFOOTERON,_pagefooteron,RptResId(RID_STR_PAGE_FOOTER),m_pImpl->m_xPageFooter);
    }
}

uno::Reference< report::XGroups > SAL_CALL OReportDefinition::getGroups()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_xGroups;
}

uno::Reference< report::XSection > SAL_CALL OReportDefinition::getReportHeader()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    if ( !m_pImpl->m_xReportHeader.is() )
        throw container::NoSuchElementException();
    return m_pImpl->m_xReportHeader;
}

uno::Reference< report::XSection > SAL_CALL OReportDefinition::getPageHeader()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    if ( !m_pImpl->m_xPageHeader.is() )
        throw container::NoSuchElementException();
    return m_pImpl->m_xPageHeader;
}

uno::Reference< report::XSection > SAL_CALL OReportDefinition::getDetail()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_xDetail;
}

uno::Reference< report::XSection > SAL_CALL OReportDefinition::getPageFooter()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    if ( !m_pImpl->m_xPageFooter.is() )
        throw container::NoSuchElementException();
    return m_pImpl->m_xPageFooter;
}

uno::Reference< report::XSection > SAL_CALL OReportDefinition::getReportFooter()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    if ( !m_pImpl->m_xReportFooter.is() )
        throw container::NoSuchElementException();
    return m_pImpl->m_xReportFooter;
}

uno::Reference< document::XEventBroadcaster > SAL_CALL OReportDefinition::getEventBroadcaster(  )
{
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    return this;
}

// XReportComponent
REPORTCOMPONENT_MASTERDETAIL(OReportDefinition,*m_aProps)
REPORTCOMPONENT_IMPL(OReportDefinition,*m_aProps)
REPORTCOMPONENT_IMPL2(OReportDefinition,*m_aProps)

uno::Reference< beans::XPropertySetInfo > SAL_CALL OReportDefinition::getPropertySetInfo(  )
{
    return ReportDefinitionPropertySet::getPropertySetInfo();
}

void SAL_CALL OReportDefinition::setPropertyValue( const OUString& aPropertyName, const uno::Any& aValue )
{
    ReportDefinitionPropertySet::setPropertyValue( aPropertyName, aValue );
}

uno::Any SAL_CALL OReportDefinition::getPropertyValue( const OUString& PropertyName )
{
    return ReportDefinitionPropertySet::getPropertyValue( PropertyName);
}

void SAL_CALL OReportDefinition::addPropertyChangeListener( const OUString& aPropertyName, const uno::Reference< beans::XPropertyChangeListener >& xListener )
{
    ReportDefinitionPropertySet::addPropertyChangeListener( aPropertyName, xListener );
}

void SAL_CALL OReportDefinition::removePropertyChangeListener( const OUString& aPropertyName, const uno::Reference< beans::XPropertyChangeListener >& aListener )
{
    ReportDefinitionPropertySet::removePropertyChangeListener( aPropertyName, aListener );
}

void SAL_CALL OReportDefinition::addVetoableChangeListener( const OUString& PropertyName, const uno::Reference< beans::XVetoableChangeListener >& aListener )
{
    ReportDefinitionPropertySet::addVetoableChangeListener( PropertyName, aListener );
}

void SAL_CALL OReportDefinition::removeVetoableChangeListener( const OUString& PropertyName, const uno::Reference< beans::XVetoableChangeListener >& aListener )
{
    ReportDefinitionPropertySet::removeVetoableChangeListener( PropertyName, aListener );
}

// XChild
uno::Reference< uno::XInterface > SAL_CALL OReportDefinition::getParent(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    if (auto xChild = comphelper::query_aggregation<container::XChild>(m_aProps->m_xProxy))
        return xChild->getParent();
    return m_pImpl->m_xParent;
}

void SAL_CALL OReportDefinition::setParent( const uno::Reference< uno::XInterface >& Parent )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    m_aProps->m_xParent = uno::Reference< container::XChild >(Parent,uno::UNO_QUERY);
    m_pImpl->m_xParent = Parent;
    if (auto xChild = comphelper::query_aggregation<container::XChild>(m_aProps->m_xProxy))
        xChild->setParent(Parent);
}

// XCloneable
uno::Reference< util::XCloneable > SAL_CALL OReportDefinition::createClone(  )
{
    OSL_FAIL("Not yet implemented correctly");
    uno::Reference< report::XReportComponent> xSource = this;
    uno::Reference< report::XReportDefinition> xSet(cloneObject(xSource,m_aProps->m_xFactory,SERVICE_REPORTDEFINITION),uno::UNO_QUERY_THROW);
    return xSet;
}

void OReportDefinition::setSection(  const OUString& _sProperty
                            ,bool _bOn
                            ,const OUString& _sName
                            ,rtl::Reference< OSection>& _member)
{
    BoundListeners l;
    {
        ::osl::MutexGuard aGuard(m_aMutex);
        prepareSet(_sProperty, uno::Any(uno::Reference<report::XSection>(_member)), uno::Any(_bOn), &l);

        // create section if needed
        if ( _bOn && !_member.is() )
            _member = OSection::createOSection(this, getContext(), _sProperty == PROPERTY_PAGEHEADERON || _sProperty == PROPERTY_PAGEFOOTERON);
        else if ( !_bOn )
            ::comphelper::disposeComponent(_member);

        if ( _member.is() )
            _member->setName(_sName);
    }
    l.notify();
}

// XCloseBroadcaster
void SAL_CALL OReportDefinition::addCloseListener( const uno::Reference< util::XCloseListener >& _xListener )
{
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( _xListener.is() )
        m_pImpl->m_aCloseListener.addInterface(_xListener);
}

void SAL_CALL OReportDefinition::removeCloseListener( const uno::Reference< util::XCloseListener >& _xListener )
{
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    m_pImpl->m_aCloseListener.removeInterface(_xListener);
}

// XCloseable
void SAL_CALL OReportDefinition::close(sal_Bool bDeliverOwnership)
{
    SolarMutexGuard aSolarGuard;

    ::osl::ResettableMutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    // notify our container listeners
    lang::EventObject aEvt( getXWeak() );
    aGuard.clear();
    m_pImpl->m_aCloseListener.forEach(
        [&aEvt, &bDeliverOwnership] (uno::Reference<util::XCloseListener> const& xListener) {
            return xListener->queryClosing(aEvt, bDeliverOwnership);
        });
    aGuard.reset();


    ::std::vector< uno::Reference< frame::XController> > aCopy = m_pImpl->m_aControllers;
    for (auto& rxController : aCopy)
    {
        if ( rxController.is() )
        {
            try
            {
                uno::Reference< util::XCloseable> xFrame( rxController->getFrame(), uno::UNO_QUERY );
                if ( xFrame.is() )
                    xFrame->close( bDeliverOwnership );
            }
            catch (const util::CloseVetoException&) { throw; }
            catch (const uno::Exception&)
            {
                TOOLS_WARN_EXCEPTION( "reportdesign", "ODatabaseDocument::impl_closeControllerFrames" );
            }
        }
    }

    aGuard.clear();
    m_pImpl->m_aCloseListener.notifyEach(&util::XCloseListener::notifyClosing,aEvt);
    aGuard.reset();

    dispose();
}

// XModel
sal_Bool SAL_CALL OReportDefinition::attachResource( const OUString& /*_rURL*/, const uno::Sequence< beans::PropertyValue >& _aArguments )
{
    // LLA: we had a deadlock problem in our context, so we get the SolarMutex earlier.
    SolarMutexGuard aSolarGuard;

    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed( ReportDefinitionBase::rBHelper.bDisposed );
    utl::MediaDescriptor aDescriptor( _aArguments );

    m_pImpl->m_pUndoManager->GetSfxUndoManager().EnableUndo( false );
    try
    {
        fillArgs(aDescriptor);
        m_pImpl->m_pReportModel->SetModified(false);
    }
    catch (...)
    {
        m_pImpl->m_pUndoManager->GetSfxUndoManager().EnableUndo( true );
        throw;
    }
    m_pImpl->m_pUndoManager->GetSfxUndoManager().EnableUndo( true );
    return true;
}

void OReportDefinition::fillArgs(utl::MediaDescriptor& _aDescriptor)
{
    uno::Sequence<beans::PropertyValue> aComponentData;
    aComponentData = _aDescriptor.getUnpackedValueOrDefault(u"ComponentData"_ustr,aComponentData);
    if ( aComponentData.hasElements() && (!m_pImpl->m_xActiveConnection.is() || !m_pImpl->m_xNumberFormatsSupplier.is()) )
    {
        ::comphelper::SequenceAsHashMap aComponentDataMap( aComponentData );
        m_pImpl->m_xActiveConnection = aComponentDataMap.getUnpackedValueOrDefault(u"ActiveConnection"_ustr,m_pImpl->m_xActiveConnection);
        m_pImpl->m_xNumberFormatsSupplier = dbtools::getNumberFormats(m_pImpl->m_xActiveConnection);
    }
    if ( !m_pImpl->m_xNumberFormatsSupplier.is() )
    {
        m_pImpl->m_xNumberFormatsSupplier.set( util::NumberFormatsSupplier::createWithDefaultLocale( m_aProps->m_xContext ) );
    }
    lcl_stripLoadArguments( _aDescriptor, m_pImpl->m_aArgs );
    OUString sCaption;
    sCaption = _aDescriptor.getUnpackedValueOrDefault(u"DocumentTitle"_ustr,sCaption);
    setCaption(sCaption);
}

OUString SAL_CALL OReportDefinition::getURL(  )
{
    return OUString();
}

uno::Sequence< beans::PropertyValue > SAL_CALL OReportDefinition::getArgs(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    return m_pImpl->m_aArgs;
}

void SAL_CALL OReportDefinition::connectController( const uno::Reference< frame::XController >& _xController )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    m_pImpl->m_aControllers.push_back(_xController);
    if ( _xController.is() && m_pImpl->m_xViewData.is() )
    {
        sal_Int32 nCount = m_pImpl->m_xViewData->getCount();
        if (nCount)
            _xController->restoreViewData(m_pImpl->m_xViewData->getByIndex(nCount - 1));
    }
}

void SAL_CALL OReportDefinition::disconnectController( const uno::Reference< frame::XController >& _xController )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    ::std::vector< uno::Reference< frame::XController> >::iterator aFind = ::std::find(m_pImpl->m_aControllers.begin(),m_pImpl->m_aControllers.end(),_xController);
    if ( aFind != m_pImpl->m_aControllers.end() )
        m_pImpl->m_aControllers.erase(aFind);
    if ( m_pImpl->m_xCurrentController == _xController )
        m_pImpl->m_xCurrentController.clear();
}

void SAL_CALL OReportDefinition::lockControllers(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    m_pImpl->m_bControllersLocked = true;
}

void SAL_CALL OReportDefinition::unlockControllers(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    m_pImpl->m_bControllersLocked = false;
}

sal_Bool SAL_CALL OReportDefinition::hasControllersLocked(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    return m_pImpl->m_bControllersLocked;
}

uno::Reference< frame::XController > SAL_CALL OReportDefinition::getCurrentController(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    return m_pImpl->m_xCurrentController;
}

void SAL_CALL OReportDefinition::setCurrentController( const uno::Reference< frame::XController >& _xController )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( ::std::find(m_pImpl->m_aControllers.begin(),m_pImpl->m_aControllers.end(),_xController) == m_pImpl->m_aControllers.end() )
        throw container::NoSuchElementException();
    m_pImpl->m_xCurrentController = _xController;
}

uno::Reference< uno::XInterface > SAL_CALL OReportDefinition::getCurrentSelection(  )
{
    return uno::Reference< uno::XInterface >();
}

void OReportDefinition::impl_loadFromStorage_nolck_throw( const uno::Reference< embed::XStorage >& _xStorageToLoadFrom,
        const uno::Sequence< beans::PropertyValue >& _aMediaDescriptor )
{
    m_pImpl->m_xStorage = _xStorageToLoadFrom;

    utl::MediaDescriptor aDescriptor( _aMediaDescriptor );
    fillArgs(aDescriptor);
    aDescriptor.createItemIfMissing(u"Storage"_ustr,uno::Any(_xStorageToLoadFrom));

    uno::Sequence< uno::Any > aDelegatorArguments(_aMediaDescriptor.getLength());
    uno::Any* pIter = aDelegatorArguments.getArray();
    uno::Any* pEnd  = pIter + aDelegatorArguments.getLength();
    for(sal_Int32 i = 0;pIter != pEnd;++pIter,++i)
    {
        *pIter <<= _aMediaDescriptor[i];
    }
    sal_Int32 nPos = aDelegatorArguments.getLength();
    aDelegatorArguments.realloc(nPos+1);
    beans::PropertyValue aPropVal;
    aPropVal.Name = "Storage";
    aPropVal.Value <<= _xStorageToLoadFrom;
    aDelegatorArguments.getArray()[nPos] <<= aPropVal;

    rptui::OXUndoEnvironment& rEnv = m_pImpl->m_pReportModel->GetUndoEnv();
    rptui::OXUndoEnvironment::OUndoEnvLock aLock(rEnv);
    {
        uno::Reference< document::XFilter > xFilter(
            m_aProps->m_xContext->getServiceManager()->createInstanceWithArgumentsAndContext(u"com.sun.star.comp.report.OReportFilter"_ustr,aDelegatorArguments,m_aProps->m_xContext),
            uno::UNO_QUERY_THROW );

        uno::Reference< document::XImporter> xImporter(xFilter,uno::UNO_QUERY_THROW);
        uno::Reference<XComponent> xComponent(getXWeak(),uno::UNO_QUERY);
        xImporter->setTargetDocument(xComponent);

        utl::MediaDescriptor aTemp;
        aTemp << aDelegatorArguments;
        xFilter->filter(aTemp.getAsConstPropertyValueList());

        lcl_setModelReadOnly(m_pImpl->m_xStorage,m_pImpl->m_pReportModel);
        m_pImpl->m_pObjectContainer->SwitchPersistence(m_pImpl->m_xStorage);
    }
}

// XStorageBasedDocument
void SAL_CALL OReportDefinition::loadFromStorage( const uno::Reference< embed::XStorage >& _xStorageToLoadFrom
                                                 , const uno::Sequence< beans::PropertyValue >& _aMediaDescriptor )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    impl_loadFromStorage_nolck_throw( _xStorageToLoadFrom, _aMediaDescriptor );
}

void SAL_CALL OReportDefinition::storeToStorage( const uno::Reference< embed::XStorage >& _xStorageToSaveTo, const uno::Sequence< beans::PropertyValue >& _aMediaDescriptor )
{
    if ( !_xStorageToSaveTo.is() )
        throw lang::IllegalArgumentException(RptResId(RID_STR_ARGUMENT_IS_NULL),*this,1);

    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    // create XStatusIndicator
    uno::Reference<task::XStatusIndicator> xStatusIndicator;
    uno::Sequence< uno::Any > aDelegatorArguments;
    utl::MediaDescriptor aDescriptor( _aMediaDescriptor );
    lcl_extractAndStartStatusIndicator( aDescriptor, xStatusIndicator, aDelegatorArguments );
    bool AutoSaveEvent = false;
    aDescriptor[utl::MediaDescriptor::PROP_AUTOSAVEEVENT] >>= AutoSaveEvent;

    // export sub streams for package, else full stream into a file
    uno::Reference< beans::XPropertySet> xProp(_xStorageToSaveTo,uno::UNO_QUERY);
    if ( xProp.is() )
    {
        static constexpr OUString sPropName = u"MediaType"_ustr;
        OUString sOldMediaType;
        xProp->getPropertyValue(sPropName) >>= sOldMediaType;
        if ( !xProp->getPropertyValue(sPropName).hasValue() || sOldMediaType.isEmpty() || MIMETYPE_OASIS_OPENDOCUMENT_REPORT_ASCII != sOldMediaType )
            xProp->setPropertyValue( sPropName, uno::Any(MIMETYPE_OASIS_OPENDOCUMENT_REPORT_ASCII) );
    }

    /** property map for export info set */
    static comphelper::PropertyMapEntry const aExportInfoMap[] =
    {
        { u"UsePrettyPrinting"_ustr , 0, cppu::UnoType<sal_Bool>::get(),          beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"StreamName"_ustr        , 0, cppu::UnoType<OUString>::get(), beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"StreamRelPath"_ustr     , 0, cppu::UnoType<OUString>::get(), beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"BaseURI"_ustr           , 0, cppu::UnoType<OUString>::get(), beans::PropertyAttribute::MAYBEVOID, 0 },
    };
    uno::Reference< beans::XPropertySet > xInfoSet( comphelper::GenericPropertySet_CreateInstance( new comphelper::PropertySetInfo( aExportInfoMap ) ) );

    xInfoSet->setPropertyValue(u"UsePrettyPrinting"_ustr, uno::Any(officecfg::Office::Common::Save::Document::PrettyPrinting::get()));
    if ( officecfg::Office::Common::Save::URL::FileSystem::get() )
    {
        const OUString sVal( aDescriptor.getUnpackedValueOrDefault(utl::MediaDescriptor::PROP_DOCUMENTBASEURL, OUString()) );
        xInfoSet->setPropertyValue(u"BaseURI"_ustr, uno::Any(sVal));
    }
    const OUString sHierarchicalDocumentName( aDescriptor.getUnpackedValueOrDefault(u"HierarchicalDocumentName"_ustr,OUString()) );
    xInfoSet->setPropertyValue(u"StreamRelPath"_ustr, uno::Any(sHierarchicalDocumentName));


    sal_Int32 nArgsLen = aDelegatorArguments.getLength();
    aDelegatorArguments.realloc(nArgsLen+3);
    auto pDelegatorArguments = aDelegatorArguments.getArray();
    pDelegatorArguments[nArgsLen++] <<= xInfoSet;

    uno::Reference<document::XGraphicStorageHandler> xGraphicStorageHandler
        = SvXMLGraphicHelper::Create(_xStorageToSaveTo, SvXMLGraphicHelperMode::Write);
    uno::Reference<document::XEmbeddedObjectResolver> xObjectResolver
        = SvXMLEmbeddedObjectHelper::Create(_xStorageToSaveTo, *this,
                                            SvXMLEmbeddedObjectHelperMode::Write);

    pDelegatorArguments[nArgsLen++] <<= xGraphicStorageHandler;
    pDelegatorArguments[nArgsLen++] <<= xObjectResolver;

    uno::Reference<XComponent> xCom(getXWeak(),uno::UNO_QUERY);
    // Try to write to settings.xml, meta.xml, and styles.xml; only really care about success of
    // write to content.xml (keeping logic of commit 94ccba3eebc83b58e74e18f0e028c6a995ce6aa6)
    xInfoSet->setPropertyValue(u"StreamName"_ustr, uno::Any(u"settings.xml"_ustr));
    rtl::Reference<rptxml::ORptExport> pSettingsExporter
        = rptxml::ORptExport::createSettingsExporter(m_aProps->m_xContext);
    WriteThroughComponent(xCom, u"settings.xml"_ustr, pSettingsExporter, aDelegatorArguments,
                          _xStorageToSaveTo);

    xInfoSet->setPropertyValue(u"StreamName"_ustr, uno::Any(u"meta.xml"_ustr));
    rtl::Reference<rptxml::ORptExport> pMetaExporter
        = rptxml::ORptExport::createMetaExporter(m_aProps->m_xContext);
    WriteThroughComponent(xCom, u"meta.xml"_ustr, pMetaExporter, aDelegatorArguments,
                          _xStorageToSaveTo);

    xInfoSet->setPropertyValue(u"StreamName"_ustr, uno::Any(u"styles.xml"_ustr));
    rtl::Reference<rptxml::ORptExport> pStylesExporter
        = rptxml::ORptExport::createStylesExporter(m_aProps->m_xContext);
    WriteThroughComponent(xCom, u"styles.xml"_ustr, pStylesExporter, aDelegatorArguments,
                          _xStorageToSaveTo);

    xInfoSet->setPropertyValue(u"StreamName"_ustr, uno::Any(u"content.xml"_ustr));
    rtl::Reference<rptxml::ORptExport> pExportFilter
        = rptxml::ORptExport::createExportFilter(m_aProps->m_xContext);
    bool bOk = WriteThroughComponent(xCom, u"content.xml"_ustr, pExportFilter, aDelegatorArguments,
                                     _xStorageToSaveTo);

    uno::Any aImage;
    uno::Reference< embed::XVisualObject > xCurrentController(getCurrentController(),uno::UNO_QUERY);
    if ( xCurrentController.is() )
    {
        xCurrentController->setVisualAreaSize(m_pImpl->m_nAspect,m_pImpl->m_aVisualAreaSize);
        aImage = xCurrentController->getPreferredVisualRepresentation( m_pImpl->m_nAspect ).Data;
    }
    if ( aImage.hasValue() )
    {
        uno::Sequence<sal_Int8> aSeq;
        aImage >>= aSeq;
        uno::Reference<io::XInputStream> xStream = new ::comphelper::SequenceInputStream( aSeq );
        m_pImpl->m_pObjectContainer->InsertGraphicStreamDirectly(xStream, u"report"_ustr, u"image/png"_ustr);
    }

    if (bOk)
    {
        bool bPersist = false;
        if ( _xStorageToSaveTo == m_pImpl->m_xStorage )
            bPersist = m_pImpl->m_pObjectContainer->StoreChildren(true,false);
        else
            bPersist = m_pImpl->m_pObjectContainer->StoreAsChildren(true,true,AutoSaveEvent,_xStorageToSaveTo);

        if( bPersist )
            m_pImpl->m_pObjectContainer->SetPersistentEntries(m_pImpl->m_xStorage);
        try
        {
            uno::Reference<embed::XTransactedObject> xTransact(_xStorageToSaveTo,uno::UNO_QUERY);
            if ( xTransact.is() )
                xTransact->commit();
        }
        catch (const uno::Exception&)
        {
            TOOLS_WARN_EXCEPTION( "reportdesign", "Could not commit report storage!");
            throw io::IOException();
        }

        if ( _xStorageToSaveTo == m_pImpl->m_xStorage )
            setModified(false);
    }
    if ( xStatusIndicator.is() )
        xStatusIndicator->end();
}

void SAL_CALL OReportDefinition::switchToStorage(
        const uno::Reference< embed::XStorage >& xStorage)
{
    if (!xStorage.is())
        throw lang::IllegalArgumentException(RptResId(RID_STR_ARGUMENT_IS_NULL),*this,1);
    {
        ::osl::MutexGuard aGuard(m_aMutex);
        ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
        m_pImpl->m_xStorage = xStorage;
        lcl_setModelReadOnly(m_pImpl->m_xStorage,m_pImpl->m_pReportModel);
        m_pImpl->m_pObjectContainer->SwitchPersistence(m_pImpl->m_xStorage);
    }
    // notify our container listeners
    m_pImpl->m_aStorageChangeListeners.forEach(
        [this, &xStorage] (uno::Reference<document::XStorageChangeListener> const& xListener) {
            return xListener->notifyStorageChange(getXWeak(), xStorage);
        });
}

uno::Reference< embed::XStorage > SAL_CALL OReportDefinition::getDocumentStorage(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    return m_pImpl->m_xStorage;
}

void SAL_CALL OReportDefinition::addStorageChangeListener( const uno::Reference< document::XStorageChangeListener >& xListener )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( xListener.is() )
        m_pImpl->m_aStorageChangeListeners.addInterface(xListener);
}

void SAL_CALL OReportDefinition::removeStorageChangeListener( const uno::Reference< document::XStorageChangeListener >& xListener )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    m_pImpl->m_aStorageChangeListeners.removeInterface(xListener);
}

bool OReportDefinition::WriteThroughComponent(
    const uno::Reference<lang::XComponent> & xComponent,
    const OUString& rStreamName,
    const rtl::Reference<rptxml::ORptExport>& pExporter,
    const uno::Sequence<uno::Any> & rArguments,
    const uno::Reference<embed::XStorage>& _xStorageToSaveTo)
{
    OSL_ENSURE( xComponent.is(), "Need component!" );
    assert(pExporter.is());

    // open stream
    uno::Reference<io::XStream> xStream = _xStorageToSaveTo->openStreamElement(rStreamName,
                                                                               embed::ElementModes::READWRITE | embed::ElementModes::TRUNCATE);
    if ( !xStream.is() )
        return false;
    uno::Reference<io::XOutputStream> xOutputStream = xStream->getOutputStream();
    OSL_ENSURE(xOutputStream.is(), "Can't create output stream in package!");
    if ( ! xOutputStream.is() )
        return false;

    uno::Reference<beans::XPropertySet> xStreamProp(xOutputStream,uno::UNO_QUERY);
    OSL_ENSURE(xStreamProp.is(),"No valid property set for the output stream!");

    uno::Reference<io::XSeekable> xSeek(xStreamProp,uno::UNO_QUERY);
    if ( xSeek.is() )
    {
        xSeek->seek(0);
    }

    xStreamProp->setPropertyValue( u"MediaType"_ustr, uno::Any(u"text/xml"_ustr) );

    // encrypt all streams
    xStreamProp->setPropertyValue( u"UseCommonStoragePasswordEncryption"_ustr,
                                       uno::Any( true ) );

    // get component
    uno::Reference< xml::sax::XWriter > xSaxWriter(
        xml::sax::Writer::create(m_aProps->m_xContext) );

    // connect XML writer to output stream
    xSaxWriter->setOutputStream( xOutputStream );

    // prepare arguments (prepend doc handler to given arguments)
    uno::Sequence<uno::Any> aArgs( 1 + rArguments.getLength() );
    auto pArgs = aArgs.getArray();
    *pArgs <<= xSaxWriter;
    std::copy(rArguments.begin(), rArguments.end(), std::next(pArgs));

    pExporter->initialize(aArgs);

    // connect model and filter
    pExporter->setSourceDocument(xComponent);

    // filter!
    uno::Sequence<beans::PropertyValue> aMediaDesc;
    return pExporter->filter(aMediaDesc);
}

// XLoadable
void SAL_CALL OReportDefinition::initNew(  )
{
     setPageHeaderOn( true );
     setPageFooterOn( true );
}

void SAL_CALL OReportDefinition::load( const uno::Sequence< beans::PropertyValue >& _rArguments )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    // TODO: this code is pretty similar to what happens in ODatabaseModelImpl::getOrCreateRootStorage,
    //       perhaps we can share code here.

    ::comphelper::NamedValueCollection aArguments( _rArguments );

    // the source for the to-be-created storage: either a URL, or a stream
    uno::Reference< io::XInputStream > xStream;
    OUString sURL;

    if ( aArguments.has( u"Stream"_ustr ) )
    {
        aArguments.get_ensureType( u"Stream"_ustr, xStream );
        aArguments.remove( u"Stream"_ustr );
    }
    else if ( aArguments.has( u"InputStream"_ustr ) )
    {
        aArguments.get_ensureType( u"InputStream"_ustr, xStream );
        aArguments.remove( u"InputStream"_ustr );
    }

    if ( aArguments.has( u"FileName"_ustr ) )
    {
        aArguments.get_ensureType( u"FileName"_ustr, sURL );
        aArguments.remove( u"FileName"_ustr );
    }
    else if ( aArguments.has( u"URL"_ustr ) )
    {
        aArguments.get_ensureType( u"URL"_ustr, sURL );
        aArguments.remove( u"URL"_ustr );
    }

    uno::Any aStorageSource;
    if ( xStream.is() )
        aStorageSource <<= xStream;
    else if ( !sURL.isEmpty() )
        aStorageSource <<= sURL;
    else
        throw lang::IllegalArgumentException(
            u"No input source (URL or InputStream) found."_ustr,
                // TODO: resource
            *this,
            1
        );

    uno::Reference< lang::XSingleServiceFactory > xStorageFactory( embed::StorageFactory::create( m_aProps->m_xContext ) );

    // open read-write per default, unless told otherwise in the MediaDescriptor
    uno::Reference< embed::XStorage > xDocumentStorage;
    const sal_Int32 nOpenModes[2] = {
        embed::ElementModes::READWRITE,
        embed::ElementModes::READ
    };
    size_t nFirstOpenMode = 0;
    if ( aArguments.has( u"ReadOnly"_ustr ) )
    {
        bool bReadOnly = false;
        aArguments.get_ensureType( u"ReadOnly"_ustr, bReadOnly );
        nFirstOpenMode = bReadOnly ? 1 : 0;
    }
    const size_t nLastOpenMode = SAL_N_ELEMENTS( nOpenModes ) - 1;
    for ( size_t i=nFirstOpenMode; i <= nLastOpenMode; ++i )
    {
        uno::Sequence< uno::Any > aStorageCreationArgs{ aStorageSource, uno::Any(nOpenModes[i]) };

        try
        {
            xDocumentStorage.set( xStorageFactory->createInstanceWithArguments( aStorageCreationArgs ), uno::UNO_QUERY_THROW );
        }
        catch (const uno::Exception&)
        {
            if ( i == nLastOpenMode )
            {
                css::uno::Any anyEx = cppu::getCaughtException();
                throw lang::WrappedTargetException(
                    u"An error occurred while creating the document storage."_ustr,
                        // TODO: resource
                    *this,
                    anyEx
                );
            }
        }
    }

    if ( !xDocumentStorage.is() )
    {
        throw uno::RuntimeException();
    }

    if (!aArguments.has(u"DocumentBaseURL"_ustr) && !sURL.isEmpty())
    {
        aArguments.put(u"DocumentBaseURL"_ustr, sURL);
    }

    impl_loadFromStorage_nolck_throw( xDocumentStorage, aArguments.getPropertyValues() );
    // TODO: do we need to take ownership of the storage? In opposite to loadFromStorage, we created the storage
    // ourself here, and perhaps this means we're also responsible for it ...?
}

// XVisualObject
void SAL_CALL OReportDefinition::setVisualAreaSize( ::sal_Int64 _nAspect, const awt::Size& _aSize )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    bool bChanged =
            (m_pImpl->m_aVisualAreaSize.Width != _aSize.Width ||
             m_pImpl->m_aVisualAreaSize.Height != _aSize.Height);
    m_pImpl->m_aVisualAreaSize = _aSize;
    if( bChanged )
            setModified( true );
    m_pImpl->m_nAspect = _nAspect;
}

awt::Size SAL_CALL OReportDefinition::getVisualAreaSize( ::sal_Int64 /*_nAspect*/ )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    return m_pImpl->m_aVisualAreaSize;
}

embed::VisualRepresentation SAL_CALL OReportDefinition::getPreferredVisualRepresentation( ::sal_Int64 /*_nAspect*/ )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    embed::VisualRepresentation aResult;
    OUString sMimeType;
    uno::Reference<io::XInputStream> xStream = m_pImpl->m_pObjectContainer->GetGraphicStream(u"report"_ustr, &sMimeType);
    if ( xStream.is() )
    {
        uno::Sequence<sal_Int8> aSeq;
        xStream->readBytes(aSeq,xStream->available());
        xStream->closeInput();
        aResult.Data <<= aSeq;
        aResult.Flavor.MimeType = sMimeType;
        aResult.Flavor.DataType = cppu::UnoType<decltype(aSeq)>::get();
    }

    return aResult;
}

::sal_Int32 SAL_CALL OReportDefinition::getMapUnit( ::sal_Int64 /*nAspect*/ )
{
    return embed::EmbedMapUnits::ONE_100TH_MM;
}

// XModifiable
sal_Bool SAL_CALL OReportDefinition::disableSetModified(  )
{
    ::osl::MutexGuard aGuard( m_aMutex );
    ::connectivity::checkDisposed( ReportDefinitionBase::rBHelper.bDisposed );

    const bool bWasEnabled = m_pImpl->m_bSetModifiedEnabled;
    m_pImpl->m_bSetModifiedEnabled = false;
    return bWasEnabled;
}

sal_Bool SAL_CALL OReportDefinition::enableSetModified(  )
{
    ::osl::MutexGuard aGuard( m_aMutex );
    ::connectivity::checkDisposed( ReportDefinitionBase::rBHelper.bDisposed );

    const bool bWasEnabled = m_pImpl->m_bSetModifiedEnabled;
    m_pImpl->m_bSetModifiedEnabled = true;
    return bWasEnabled;
}

sal_Bool SAL_CALL OReportDefinition::isSetModifiedEnabled(  )
{
    ::osl::MutexGuard aGuard( m_aMutex );
    ::connectivity::checkDisposed( ReportDefinitionBase::rBHelper.bDisposed );

    return m_pImpl->m_bSetModifiedEnabled;
}

// XModifiable
sal_Bool SAL_CALL OReportDefinition::isModified(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    return m_pImpl->m_bModified;
}

void SAL_CALL OReportDefinition::setModified( sal_Bool _bModified )
{
    osl::ClearableMutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    if ( !m_pImpl->m_bSetModifiedEnabled )
        return;

    if ( m_pImpl->m_pReportModel->IsReadOnly() && _bModified )
        throw beans::PropertyVetoException();
    if ( m_pImpl->m_bModified != bool(_bModified) )
    {
        m_pImpl->m_bModified = _bModified;
        if ( m_pImpl->m_pReportModel->IsChanged() != bool(_bModified) )
            m_pImpl->m_pReportModel->SetChanged(_bModified);

        lang::EventObject aEvent(*this);
        aGuard.clear();
        m_pImpl->m_aModifyListeners.notifyEach(&util::XModifyListener::modified,aEvent);
        notifyEvent(u"OnModifyChanged"_ustr);
    }
}

// XModifyBroadcaster
void SAL_CALL OReportDefinition::addModifyListener( const uno::Reference< util::XModifyListener >& _xListener )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( _xListener.is() )
        m_pImpl->m_aModifyListeners.addInterface(_xListener);
}

void SAL_CALL OReportDefinition::removeModifyListener( const uno::Reference< util::XModifyListener >& _xListener )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    m_pImpl->m_aModifyListeners.removeInterface(_xListener);
}

void OReportDefinition::notifyEvent(const OUString& _sEventName)
{
    try
    {
        osl::ClearableMutexGuard aGuard(m_aMutex);
        ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
        document::EventObject aEvt(*this, _sEventName);
        aGuard.clear();
        m_pImpl->m_aLegacyEventListeners.notifyEach(&document::XEventListener::notifyEvent,aEvt);
    }
    catch (const uno::Exception&)
    {
    }

    notifyDocumentEvent(_sEventName, nullptr, css::uno::Any());
}

// document::XDocumentEventBroadcaster
void SAL_CALL OReportDefinition::notifyDocumentEvent( const OUString& rEventName, const uno::Reference< frame::XController2 >& rViewController, const uno::Any& rSupplement )
{
    try
    {
        osl::ClearableMutexGuard aGuard(m_aMutex);
        ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
        document::DocumentEvent aEvt(*this, rEventName, rViewController, rSupplement);
        aGuard.clear();
        m_pImpl->m_aDocEventListeners.notifyEach(&document::XDocumentEventListener::documentEventOccured,aEvt);
    }
    catch (const uno::Exception&)
    {
    }
}

void SAL_CALL OReportDefinition::addDocumentEventListener( const uno::Reference< document::XDocumentEventListener >& rListener )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( rListener.is() )
        m_pImpl->m_aDocEventListeners.addInterface(rListener);
}

void SAL_CALL OReportDefinition::removeDocumentEventListener( const uno::Reference< document::XDocumentEventListener >& rListener )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    m_pImpl->m_aDocEventListeners.removeInterface(rListener);
}

// document::XEventBroadcaster
void SAL_CALL OReportDefinition::addEventListener(const uno::Reference< document::XEventListener >& _xListener )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( _xListener.is() )
        m_pImpl->m_aLegacyEventListeners.addInterface(_xListener);
}

void SAL_CALL OReportDefinition::removeEventListener( const uno::Reference< document::XEventListener >& _xListener )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    m_pImpl->m_aLegacyEventListeners.removeInterface(_xListener);
}

// document::XViewDataSupplier
uno::Reference< container::XIndexAccess > SAL_CALL OReportDefinition::getViewData(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( !m_pImpl->m_xViewData.is() )
    {
        rtl::Reference<comphelper::IndexedPropertyValuesContainer> xNewViewData = new comphelper::IndexedPropertyValuesContainer();
        m_pImpl->m_xViewData = xNewViewData;
        for (const auto& rxController : m_pImpl->m_aControllers)
        {
            if ( rxController.is() )
            {
                try
                {
                    xNewViewData->insertByIndex(xNewViewData->getCount(), rxController->getViewData());
                }
                catch (const uno::Exception&)
                {
                }
            }
        }

    }
    return m_pImpl->m_xViewData;
}

void SAL_CALL OReportDefinition::setViewData( const uno::Reference< container::XIndexAccess >& Data )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    m_pImpl->m_xViewData = Data;
}

uno::Reference< report::XFunctions > SAL_CALL OReportDefinition::getFunctions()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    return m_pImpl->m_xFunctions;
}

uno::Reference< ui::XUIConfigurationManager > SAL_CALL OReportDefinition::getUIConfigurationManager(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    if ( !m_pImpl->m_xUIConfigurationManager.is() )
    {
        m_pImpl->m_xUIConfigurationManager = ui::UIConfigurationManager::create(m_aProps->m_xContext);

        uno::Reference< embed::XStorage > xConfigStorage;
        // initialize ui configuration manager with document substorage
        m_pImpl->m_xUIConfigurationManager->setStorage( xConfigStorage );
    }

    return m_pImpl->m_xUIConfigurationManager;
}

uno::Reference< embed::XStorage > SAL_CALL OReportDefinition::getDocumentSubStorage( const OUString& aStorageName, sal_Int32 nMode )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    return m_pImpl->m_xStorage->openStorageElement(aStorageName, nMode);
}

uno::Sequence< OUString > SAL_CALL OReportDefinition::getDocumentSubStoragesNames(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    uno::Reference<container::XNameAccess> xNameAccess = m_pImpl->m_xStorage;
    return xNameAccess.is() ? xNameAccess->getElementNames() : uno::Sequence< OUString >();
}

OUString SAL_CALL OReportDefinition::getMimeType()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    return m_pImpl->m_sMimeType;
}

void SAL_CALL OReportDefinition::setMimeType( const OUString& _mimetype )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    const uno::Sequence< OUString > aList = getAvailableMimeTypes();
    if ( ::std::find(aList.begin(), aList.end(), _mimetype) == aList.end() )
        throwIllegallArgumentException(u"getAvailableMimeTypes()"
                        ,*this
                        ,1);
    set(PROPERTY_MIMETYPE,_mimetype,m_pImpl->m_sMimeType);
}

uno::Sequence< OUString > SAL_CALL OReportDefinition::getAvailableMimeTypes(  )
{
    return { MIMETYPE_OASIS_OPENDOCUMENT_TEXT_ASCII, MIMETYPE_OASIS_OPENDOCUMENT_SPREADSHEET_ASCII };
}

// css::XUnoTunnel
sal_Int64 SAL_CALL OReportDefinition::getSomething( const uno::Sequence< sal_Int8 >& rId )
{
    sal_Int64 nRet = 0;
    if (comphelper::isUnoTunnelId<OReportDefinition>(rId) )
        nRet = comphelper::getSomething_cast(this);
    else
    {
        uno::Reference< lang::XUnoTunnel> xUnoTunnel(m_pImpl->m_xNumberFormatsSupplier,uno::UNO_QUERY);
        if ( xUnoTunnel.is() )
            nRet = xUnoTunnel->getSomething(rId);
    }
    if ( !nRet )
    {
        if (auto xTunnel = comphelper::query_aggregation<lang::XUnoTunnel>(m_aProps->m_xProxy))
            nRet = xTunnel->getSomething(rId);
    }

    return nRet;
}

uno::Sequence< sal_Int8 > SAL_CALL OReportDefinition::getImplementationId(  )
{
    return css::uno::Sequence<sal_Int8>();
}

const uno::Sequence< sal_Int8 > & OReportDefinition::getUnoTunnelId()
{
    static const comphelper::UnoIdInit implId;
    return implId.getSeq();
}

uno::Reference< uno::XComponentContext > OReportDefinition::getContext()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    return m_aProps->m_xContext;
}

std::shared_ptr<rptui::OReportModel> OReportDefinition::getSdrModel(const uno::Reference< report::XReportDefinition >& _xReportDefinition)
{
    std::shared_ptr<rptui::OReportModel> pReportModel;
    auto pReportDefinition = comphelper::getFromUnoTunnel<OReportDefinition>(_xReportDefinition);
    if (pReportDefinition)
        pReportModel = pReportDefinition->m_pImpl->m_pReportModel;
    return pReportModel;
}

SdrModel& OReportDefinition::getSdrModelFromUnoModel() const
{
    OSL_ENSURE(m_pImpl->m_pReportModel, "No SdrModel in ReportDesign, should not happen");
    return *m_pImpl->m_pReportModel;
}

uno::Reference< uno::XInterface > SAL_CALL OReportDefinition::createInstanceWithArguments( const OUString& aServiceSpecifier, const uno::Sequence< uno::Any >& _aArgs)
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    uno::Reference< uno::XInterface > xRet;
    if ( aServiceSpecifier.startsWith( "com.sun.star.document.ImportEmbeddedObjectResolver") )
    {
        uno::Reference< embed::XStorage > xStorage;
        for(const uno::Any& rArg : _aArgs)
        {
            beans::NamedValue aValue;
            rArg >>= aValue;
            if ( aValue.Name == "Storage" )
                aValue.Value >>= xStorage;
        }
        m_pImpl->m_pObjectContainer->SwitchPersistence(xStorage);
        xRet = cppu::getXWeak(SvXMLEmbeddedObjectHelper::Create( xStorage,*this, SvXMLEmbeddedObjectHelperMode::Read ).get());
    }
    else if (aServiceSpecifier == "com.sun.star.drawing.OLE2Shape")
    {
        uno::Reference<drawing::XShape> xShape(SvxUnoDrawMSFactory::createInstanceWithArguments(aServiceSpecifier, _aArgs), uno::UNO_QUERY_THROW);
        xRet = m_pImpl->m_pReportModel->createShape(aServiceSpecifier, xShape);
    }

    return xRet;
}

uno::Reference< uno::XInterface > SAL_CALL OReportDefinition::createInstance( const OUString& aServiceSpecifier )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    uno::Reference< drawing::XShape > xShape;
    if ( aServiceSpecifier.startsWith( "com.sun.star.report." ) )
    {
        if ( aServiceSpecifier == SERVICE_SHAPE )
            xShape.set(SvxUnoDrawMSFactory::createInstance(u"com.sun.star.drawing.CustomShape"_ustr),uno::UNO_QUERY_THROW);
        else if (   aServiceSpecifier == SERVICE_FORMATTEDFIELD
            ||      aServiceSpecifier == SERVICE_FIXEDTEXT
            ||      aServiceSpecifier == SERVICE_FIXEDLINE
            ||      aServiceSpecifier == SERVICE_IMAGECONTROL )
            xShape.set(SvxUnoDrawMSFactory::createInstance(u"com.sun.star.drawing.ControlShape"_ustr),uno::UNO_QUERY_THROW);
        else
            xShape.set(SvxUnoDrawMSFactory::createInstance(u"com.sun.star.drawing.OLE2Shape"_ustr),uno::UNO_QUERY_THROW);
    }
    else if ( aServiceSpecifier.startsWith( "com.sun.star.form.component." ) )
    {
        xShape.set(m_aProps->m_xContext->getServiceManager()->createInstanceWithContext(aServiceSpecifier,m_aProps->m_xContext),uno::UNO_QUERY);
    }
    else if ( aServiceSpecifier == "com.sun.star.style.PageStyle" ||
              aServiceSpecifier == "com.sun.star.style.FrameStyle" ||
              aServiceSpecifier == "com.sun.star.style.GraphicStyle"
              )
    {
        uno::Reference< style::XStyle> xStyle = new OStyle();
        xStyle->setName(u"Default"_ustr);
        return xStyle;
    }
    else if ( aServiceSpecifier == "com.sun.star.document.Settings" )
    {
        uno::Reference<beans::XPropertySet> xProp = new OStyle();

        return xProp;
    }
    else if ( aServiceSpecifier == "com.sun.star.drawing.Defaults" )
    {
        uno::Reference<beans::XPropertySet> xProp = new OStyle();
        return xProp;
    }
    else if ( aServiceSpecifier == "com.sun.star.drawing.GradientTable" )
    {
        if ( !m_pImpl->m_xGradientTable.is() )
            m_pImpl->m_xGradientTable.set(SvxUnoGradientTable_createInstance(m_pImpl->m_pReportModel.get()),uno::UNO_QUERY);
        return m_pImpl->m_xGradientTable;
    }
    else if ( aServiceSpecifier == "com.sun.star.drawing.HatchTable" )
    {
        if ( !m_pImpl->m_xHatchTable.is() )
            m_pImpl->m_xHatchTable.set(SvxUnoHatchTable_createInstance(m_pImpl->m_pReportModel.get()),uno::UNO_QUERY);
        return m_pImpl->m_xHatchTable;
    }
    else if ( aServiceSpecifier == "com.sun.star.drawing.BitmapTable"  )
    {
        if ( !m_pImpl->m_xBitmapTable.is() )
            m_pImpl->m_xBitmapTable.set(SvxUnoBitmapTable_createInstance(m_pImpl->m_pReportModel.get()),uno::UNO_QUERY);
        return m_pImpl->m_xBitmapTable;
    }
    else if ( aServiceSpecifier == "com.sun.star.drawing.TransparencyGradientTable" )
    {
        if ( !m_pImpl->m_xTransparencyGradientTable.is() )
            m_pImpl->m_xTransparencyGradientTable.set(SvxUnoTransGradientTable_createInstance(m_pImpl->m_pReportModel.get()),uno::UNO_QUERY);
        return m_pImpl->m_xTransparencyGradientTable;
    }
    else if ( aServiceSpecifier == "com.sun.star.drawing.DashTable" )
    {
        if ( !m_pImpl->m_xDashTable.is() )
            m_pImpl->m_xDashTable.set(SvxUnoDashTable_createInstance(m_pImpl->m_pReportModel.get()),uno::UNO_QUERY);
        return m_pImpl->m_xDashTable;
    }
    else if( aServiceSpecifier == "com.sun.star.drawing.MarkerTable" )
    {
        if( !m_pImpl->m_xMarkerTable.is() )
            m_pImpl->m_xMarkerTable.set(SvxUnoMarkerTable_createInstance( m_pImpl->m_pReportModel.get() ),uno::UNO_QUERY);
        return m_pImpl->m_xMarkerTable;
    }
    else if ( aServiceSpecifier == "com.sun.star.document.ImportEmbeddedObjectResolver" )
        return cppu::getXWeak(SvXMLEmbeddedObjectHelper::Create( m_pImpl->m_xStorage,*this, SvXMLEmbeddedObjectHelperMode::Read ).get());
    else if ( aServiceSpecifier == "com.sun.star.document.ExportEmbeddedObjectResolver" )
        return cppu::getXWeak(SvXMLEmbeddedObjectHelper::Create( m_pImpl->m_xStorage,*this, SvXMLEmbeddedObjectHelperMode::Write ).get());
    else if (aServiceSpecifier == "com.sun.star.document.ImportGraphicStorageHandler")
    {
        rtl::Reference<SvXMLGraphicHelper> xGraphicHelper = SvXMLGraphicHelper::Create(m_pImpl->m_xStorage,SvXMLGraphicHelperMode::Write);
        return cppu::getXWeak(xGraphicHelper.get());
    }
    else if (aServiceSpecifier == "com.sun.star.document.ExportGraphicStorageHandler")
    {
        rtl::Reference<SvXMLGraphicHelper> xGraphicHelper = SvXMLGraphicHelper::Create(m_pImpl->m_xStorage,SvXMLGraphicHelperMode::Write);
        return cppu::getXWeak(xGraphicHelper.get());
    }
    else if ( aServiceSpecifier == "com.sun.star.chart2.data.DataProvider" )
    {
        uno::Reference<chart2::data::XDatabaseDataProvider> xDataProvider(chart2::data::DatabaseDataProvider::createWithConnection( m_aProps->m_xContext, m_pImpl->m_xActiveConnection ));
        xDataProvider->setRowLimit(10);
        uno::Reference< container::XChild > xChild(xDataProvider,uno::UNO_QUERY);
        if ( xChild.is() )
            xChild->setParent(*this);
        return uno::Reference< uno::XInterface >(xDataProvider,uno::UNO_QUERY);
    }
    else if ( aServiceSpecifier == "com.sun.star.xml.NamespaceMap" )
    {
        if ( !m_pImpl->m_xXMLNamespaceMap.is() )
            m_pImpl->m_xXMLNamespaceMap = comphelper::NameContainer_createInstance( cppu::UnoType<OUString>::get() ).get();
        return m_pImpl->m_xXMLNamespaceMap;
    }
    else
        xShape.set(SvxUnoDrawMSFactory::createInstance( aServiceSpecifier ),uno::UNO_QUERY_THROW);

    return m_pImpl->m_pReportModel->createShape(aServiceSpecifier,xShape);
}

uno::Sequence< OUString > SAL_CALL OReportDefinition::getAvailableServiceNames()
{
    static const uno::Sequence<OUString> aSvxComponentServiceNameSeq
        = { u"com.sun.star.form.component.FixedText"_ustr,
            u"com.sun.star.form.component.DatabaseImageControl"_ustr,
            u"com.sun.star.style.PageStyle"_ustr,
            u"com.sun.star.style.GraphicStyle"_ustr,
            u"com.sun.star.style.FrameStyle"_ustr,
            u"com.sun.star.drawing.Defaults"_ustr,
            u"com.sun.star.document.ImportEmbeddedObjectResolver"_ustr,
            u"com.sun.star.document.ExportEmbeddedObjectResolver"_ustr,
            u"com.sun.star.document.ImportGraphicStorageHandler"_ustr,
            u"com.sun.star.document.ExportGraphicStorageHandler"_ustr,
            u"com.sun.star.chart2.data.DataProvider"_ustr,
            u"com.sun.star.xml.NamespaceMap"_ustr,
            u"com.sun.star.document.Settings"_ustr,
            u"com.sun.star.drawing.GradientTable"_ustr,
            u"com.sun.star.drawing.HatchTable"_ustr,
            u"com.sun.star.drawing.BitmapTable"_ustr,
            u"com.sun.star.drawing.TransparencyGradientTable"_ustr,
            u"com.sun.star.drawing.DashTable"_ustr,
            u"com.sun.star.drawing.MarkerTable"_ustr };

    uno::Sequence< OUString > aParentSeq( SvxUnoDrawMSFactory::getAvailableServiceNames() );
    return comphelper::concatSequences(aParentSeq, aSvxComponentServiceNameSeq);
}

// XShape
awt::Point SAL_CALL OReportDefinition::getPosition(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( m_aProps->m_xShape.is() )
        return m_aProps->m_xShape->getPosition();
    return awt::Point(m_aProps->m_nPosX,m_aProps->m_nPosY);
}

void SAL_CALL OReportDefinition::setPosition( const awt::Point& aPosition )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( m_aProps->m_xShape.is() )
        m_aProps->m_xShape->setPosition(aPosition);
    set(PROPERTY_POSITIONX,aPosition.X,m_aProps->m_nPosX);
    set(PROPERTY_POSITIONY,aPosition.Y,m_aProps->m_nPosY);
}

awt::Size SAL_CALL OReportDefinition::getSize(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( m_aProps->m_xShape.is() )
        return m_aProps->m_xShape->getSize();
    return awt::Size(m_aProps->m_nWidth,m_aProps->m_nHeight);
}

void SAL_CALL OReportDefinition::setSize( const awt::Size& aSize )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( m_aProps->m_xShape.is() )
        m_aProps->m_xShape->setSize(aSize);
    set(PROPERTY_WIDTH,aSize.Width,m_aProps->m_nWidth);
    set(PROPERTY_HEIGHT,aSize.Height,m_aProps->m_nHeight);
}


// XShapeDescriptor
OUString SAL_CALL OReportDefinition::getShapeType(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( m_aProps->m_xShape.is() )
        return m_aProps->m_xShape->getShapeType();
    return u"com.sun.star.drawing.OLE2Shape"_ustr;
}

typedef ::cppu::WeakImplHelper< container::XNameContainer,
                             container::XIndexAccess
                            > TStylesBASE;

namespace {

class OStylesHelper:
    public cppu::BaseMutex, public TStylesBASE
{
    typedef ::std::map< OUString, uno::Any  , ::comphelper::UStringMixLess> TStyleElements;
    TStyleElements                                  m_aElements;
    ::std::vector<TStyleElements::iterator>         m_aElementsPos;
    uno::Type                                       m_aType;

protected:
    virtual ~OStylesHelper() override {}
public:
    explicit OStylesHelper(const uno::Type& rType = cppu::UnoType<container::XElementAccess>::get());
    OStylesHelper(const OStylesHelper&) = delete;
    OStylesHelper& operator=(const OStylesHelper&) = delete;

    // XNameContainer
    virtual void SAL_CALL insertByName( const OUString& aName, const uno::Any& aElement ) override;
    virtual void SAL_CALL removeByName( const OUString& Name ) override;

    // XNameReplace
    virtual void SAL_CALL replaceByName( const OUString& aName, const uno::Any& aElement ) override;

    // container::XElementAccess
    virtual uno::Type SAL_CALL getElementType(  ) override;
    virtual sal_Bool SAL_CALL hasElements(  ) override;
    // container::XIndexAccess
    virtual sal_Int32 SAL_CALL getCount(  ) override;
    virtual uno::Any SAL_CALL getByIndex( sal_Int32 Index ) override;

    // container::XNameAccess
    virtual uno::Any SAL_CALL getByName( const OUString& aName ) override;
    virtual uno::Sequence< OUString > SAL_CALL getElementNames(  ) override;
    virtual sal_Bool SAL_CALL hasByName( const OUString& aName ) override;
};

}

OStylesHelper::OStylesHelper(const uno::Type& rType)
    : cppu::BaseMutex()
    , m_aType(rType)
{
}
;

// container::XElementAccess
uno::Type SAL_CALL OStylesHelper::getElementType(  )
{
    return m_aType;
}

sal_Bool SAL_CALL OStylesHelper::hasElements(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return !m_aElementsPos.empty();
}

// container::XIndexAccess
sal_Int32 SAL_CALL OStylesHelper::getCount(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_aElementsPos.size();
}

uno::Any SAL_CALL OStylesHelper::getByIndex( sal_Int32 Index )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    if ( Index < 0 || o3tl::make_unsigned(Index) >= m_aElementsPos.size() )
        throw lang::IndexOutOfBoundsException();
    return m_aElementsPos[Index]->second;
}

// container::XNameAccess
uno::Any SAL_CALL OStylesHelper::getByName( const OUString& aName )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    TStyleElements::const_iterator aFind = m_aElements.find(aName);
    if ( aFind == m_aElements.end() )
        throw container::NoSuchElementException();
    return aFind->second;
}

uno::Sequence< OUString > SAL_CALL OStylesHelper::getElementNames(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    uno::Sequence< OUString > aNameList(m_aElementsPos.size());

    OUString* pStringArray = aNameList.getArray();
    for(const auto& rIter : m_aElementsPos)
    {
        *pStringArray = rIter->first;
        ++pStringArray;
    }

    return aNameList;
}

sal_Bool SAL_CALL OStylesHelper::hasByName( const OUString& aName )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_aElements.find(aName) != m_aElements.end();
}

// XNameContainer
void SAL_CALL OStylesHelper::insertByName( const OUString& aName, const uno::Any& aElement )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    if ( m_aElements.find(aName) != m_aElements.end() )
        throw container::ElementExistException();

    if ( !aElement.isExtractableTo(m_aType) )
        throw lang::IllegalArgumentException();

    m_aElementsPos.push_back(m_aElements.emplace(aName,aElement).first);
}

void SAL_CALL OStylesHelper::removeByName( const OUString& aName )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    TStyleElements::const_iterator aFind = m_aElements.find(aName);
    if ( aFind != m_aElements.end() )
        throw container::NoSuchElementException();
    m_aElementsPos.erase(::std::find(m_aElementsPos.begin(),m_aElementsPos.end(),aFind));
    m_aElements.erase(aFind);
}

// XNameReplace
void SAL_CALL OStylesHelper::replaceByName( const OUString& aName, const uno::Any& aElement )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    TStyleElements::iterator aFind = m_aElements.find(aName);
    if ( aFind == m_aElements.end() )
        throw container::NoSuchElementException();
    if ( !aElement.isExtractableTo(m_aType) )
        throw lang::IllegalArgumentException();
    aFind->second = aElement;
}

uno::Reference< container::XNameAccess > SAL_CALL OReportDefinition::getStyleFamilies(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( !m_pImpl->m_xStyles.is() )
    {
        m_pImpl->m_xStyles = new OStylesHelper();

        uno::Reference< container::XNameContainer> xPageStyles = new OStylesHelper(cppu::UnoType<style::XStyle>::get());
        m_pImpl->m_xStyles->insertByName(u"PageStyles"_ustr,uno::Any(xPageStyles));
        uno::Reference< style::XStyle> xPageStyle(createInstance(u"com.sun.star.style.PageStyle"_ustr),uno::UNO_QUERY);
        xPageStyles->insertByName(xPageStyle->getName(),uno::Any(xPageStyle));

        uno::Reference< container::XNameContainer> xFrameStyles = new OStylesHelper(cppu::UnoType<style::XStyle>::get());
        m_pImpl->m_xStyles->insertByName(u"FrameStyles"_ustr,uno::Any(xFrameStyles));
        uno::Reference< style::XStyle> xFrameStyle(createInstance(u"com.sun.star.style.FrameStyle"_ustr),uno::UNO_QUERY);
        xFrameStyles->insertByName(xFrameStyle->getName(),uno::Any(xFrameStyle));

        uno::Reference< container::XNameContainer> xGraphicStyles = new OStylesHelper(cppu::UnoType<style::XStyle>::get());
        m_pImpl->m_xStyles->insertByName(u"graphics"_ustr,uno::Any(xGraphicStyles));
        uno::Reference< style::XStyle> xGraphicStyle(createInstance(u"com.sun.star.style.GraphicStyle"_ustr),uno::UNO_QUERY);
        xGraphicStyles->insertByName(xGraphicStyle->getName(),uno::Any(xGraphicStyle));
    }
    return m_pImpl->m_xStyles;
}
OUString SAL_CALL  OReportDefinition::getIdentifier(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    return m_pImpl->m_sIdentifier;
}

void SAL_CALL OReportDefinition::setIdentifier( const OUString& Identifier )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    m_pImpl->m_sIdentifier = Identifier;
}

// XNumberFormatsSupplier
uno::Reference< beans::XPropertySet > SAL_CALL OReportDefinition::getNumberFormatSettings(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    if ( m_pImpl->m_xNumberFormatsSupplier.is() )
        return m_pImpl->m_xNumberFormatsSupplier->getNumberFormatSettings();
    return uno::Reference< beans::XPropertySet >();
}

uno::Reference< util::XNumberFormats > SAL_CALL OReportDefinition::getNumberFormats(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    if ( m_pImpl->m_xNumberFormatsSupplier.is() )
        return m_pImpl->m_xNumberFormatsSupplier->getNumberFormats();
    return uno::Reference< util::XNumberFormats >();
}

::comphelper::EmbeddedObjectContainer& OReportDefinition::getEmbeddedObjectContainer() const
{
    return *m_pImpl->m_pObjectContainer;
}

uno::Reference< embed::XStorage > OReportDefinition::getStorage() const
{
    return m_pImpl->m_xStorage;
}

uno::Reference< task::XInteractionHandler > OReportDefinition::getInteractionHandler() const
{
    uno::Reference< task::XInteractionHandler > xRet(
        task::InteractionHandler::createWithParent(m_aProps->m_xContext, nullptr), uno::UNO_QUERY_THROW);
    return xRet;
}

uno::Reference< sdbc::XConnection > SAL_CALL OReportDefinition::getActiveConnection()
{
    ::osl::MutexGuard aGuard(m_aMutex);
    return m_pImpl->m_xActiveConnection;
}

void SAL_CALL OReportDefinition::setActiveConnection( const uno::Reference< sdbc::XConnection >& _activeconnection )
{
    if ( !_activeconnection.is() )
        throw lang::IllegalArgumentException();
    set(PROPERTY_ACTIVECONNECTION,_activeconnection,m_pImpl->m_xActiveConnection);
}

OUString SAL_CALL OReportDefinition::getDataSourceName()
{
    osl::MutexGuard g(m_aMutex);
    return m_pImpl->m_sDataSourceName;
}

void SAL_CALL OReportDefinition::setDataSourceName(const OUString& the_value)
{
    set(PROPERTY_DATASOURCENAME,the_value,m_pImpl->m_sDataSourceName);
}

bool OReportDefinition::isEnableSetModified() const
{
    return true;
}

OUString OReportDefinition::getDocumentBaseURL() const
{
    // TODO: should this be in getURL()? not sure...
    uno::Reference<frame::XModel> const xParent(
        const_cast<OReportDefinition*>(this)->getParent(), uno::UNO_QUERY);
    if (xParent.is())
    {
        return xParent->getURL();
    }

    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    for (beans::PropertyValue const& it : m_pImpl->m_aArgs)
    {
        if (it.Name == "DocumentBaseURL")
            return it.Value.get<OUString>();
    }

    return OUString();
}

uno::Reference< frame::XTitle > OReportDefinition::impl_getTitleHelper_throw()
{
    SolarMutexGuard aSolarGuard;

    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    if ( ! m_pImpl->m_xTitleHelper.is ())
    {
        uno::Reference< frame::XDesktop2 >    xDesktop = frame::Desktop::create(m_aProps->m_xContext);

        m_pImpl->m_xTitleHelper = new ::framework::TitleHelper( m_aProps->m_xContext, uno::Reference< frame::XModel >(this),
                                    uno::Reference<frame::XUntitledNumbers>(xDesktop, uno::UNO_QUERY_THROW) );
    }

    return m_pImpl->m_xTitleHelper;
}

uno::Reference< frame::XUntitledNumbers > OReportDefinition::impl_getUntitledHelper_throw()
{
    SolarMutexGuard aSolarGuard;

    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    if ( ! m_pImpl->m_xNumberedControllers.is ())
    {
        rtl::Reference<::comphelper::NumberedCollection>  pHelper = new ::comphelper::NumberedCollection();
        m_pImpl->m_xNumberedControllers = pHelper;

        pHelper->setOwner          (uno::Reference< frame::XModel >(this));
        pHelper->setUntitledPrefix (u" : "_ustr);
    }

    return m_pImpl->m_xNumberedControllers;
}

// css.frame.XTitle
OUString SAL_CALL OReportDefinition::getTitle()
{
    // SYNCHRONIZED ->
    SolarMutexGuard aSolarGuard;

    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    return impl_getTitleHelper_throw()->getTitle ();
}

// css.frame.XTitle
void SAL_CALL OReportDefinition::setTitle( const OUString& sTitle )
{
    // SYNCHRONIZED ->
    SolarMutexGuard aSolarGuard;

    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    impl_getTitleHelper_throw()->setTitle (sTitle);
}

// css.frame.XTitleChangeBroadcaster
void SAL_CALL OReportDefinition::addTitleChangeListener( const uno::Reference< frame::XTitleChangeListener >& xListener )
{
    // SYNCHRONIZED ->
    SolarMutexGuard aSolarGuard;

    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    uno::Reference< frame::XTitleChangeBroadcaster > xBroadcaster(impl_getTitleHelper_throw(), uno::UNO_QUERY);
    if (xBroadcaster.is ())
        xBroadcaster->addTitleChangeListener (xListener);
}

// css.frame.XTitleChangeBroadcaster
void SAL_CALL OReportDefinition::removeTitleChangeListener( const uno::Reference< frame::XTitleChangeListener >& xListener )
{
    // SYNCHRONIZED ->
    SolarMutexGuard aSolarGuard;

    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    uno::Reference< frame::XTitleChangeBroadcaster > xBroadcaster(impl_getTitleHelper_throw(), uno::UNO_QUERY);
    if (xBroadcaster.is ())
        xBroadcaster->removeTitleChangeListener (xListener);
}

// css.frame.XUntitledNumbers
::sal_Int32 SAL_CALL OReportDefinition::leaseNumber( const uno::Reference< uno::XInterface >& xComponent )
{
    // object already disposed?
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    return impl_getUntitledHelper_throw()->leaseNumber (xComponent);
}

// css.frame.XUntitledNumbers
void SAL_CALL OReportDefinition::releaseNumber( ::sal_Int32 nNumber )
{
    // object already disposed?
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    impl_getUntitledHelper_throw()->releaseNumber (nNumber);
}

// css.frame.XUntitledNumbers
void SAL_CALL OReportDefinition::releaseNumberForComponent( const uno::Reference< uno::XInterface >& xComponent )
{
    // object already disposed?
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    impl_getUntitledHelper_throw()->releaseNumberForComponent (xComponent);
}

// css.frame.XUntitledNumbers
OUString SAL_CALL OReportDefinition::getUntitledPrefix()
{
    // object already disposed?
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);

    return impl_getUntitledHelper_throw()->getUntitledPrefix ();
}

uno::Reference< document::XDocumentProperties > SAL_CALL OReportDefinition::getDocumentProperties(  )
{
    ::osl::MutexGuard aGuard(m_aMutex);
    ::connectivity::checkDisposed(ReportDefinitionBase::rBHelper.bDisposed);
    if ( !m_pImpl->m_xDocumentProperties.is() )
    {
        m_pImpl->m_xDocumentProperties.set(document::DocumentProperties::create(m_aProps->m_xContext));
    }
    return m_pImpl->m_xDocumentProperties;
}

uno::Any SAL_CALL OReportDefinition::getTransferData( const datatransfer::DataFlavor& aFlavor )
{
    uno::Any aResult;
    if( !isDataFlavorSupported( aFlavor ) )
    {
        throw datatransfer::UnsupportedFlavorException(aFlavor.MimeType, getXWeak());
    }

    try
    {
        aResult = getPreferredVisualRepresentation(0).Data;
    }
    catch (const uno::Exception &)
    {
        DBG_UNHANDLED_EXCEPTION("reportdesign");
    }


    return aResult;
}

uno::Sequence< datatransfer::DataFlavor > SAL_CALL OReportDefinition::getTransferDataFlavors(  )
{
    return { { u"image/png"_ustr, u"PNG"_ustr, cppu::UnoType<uno::Sequence< sal_Int8 >>::get() } };
}

sal_Bool SAL_CALL OReportDefinition::isDataFlavorSupported( const datatransfer::DataFlavor& aFlavor )
{
    return aFlavor.MimeType == "image/png";
}


uno::Reference< document::XUndoManager > SAL_CALL OReportDefinition::getUndoManager(  )
{
    ::osl::MutexGuard aGuard( m_aMutex );
    return m_pImpl->m_pUndoManager;
}

}// namespace reportdesign

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
