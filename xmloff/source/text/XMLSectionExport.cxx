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

#include "XMLSectionExport.hxx"
#include <o3tl/any.hxx>
#include <rtl/ustring.hxx>
#include <rtl/ustrbuf.hxx>
#include <osl/diagnose.h>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/Locale.hpp>
#include <com/sun/star/container/XIndexReplace.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/beans/PropertyValues.hpp>
#include <com/sun/star/text/XTextSection.hpp>
#include <com/sun/star/text/SectionFileLink.hpp>
#include <com/sun/star/container/XNamed.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/text/XDocumentIndex.hpp>
#include <com/sun/star/text/BibliographyDataField.hpp>
#include <com/sun/star/text/XTextFieldsSupplier.hpp>
#include <com/sun/star/text/XChapterNumberingSupplier.hpp>
#include <com/sun/star/text/ChapterFormat.hpp>

#include <comphelper/base64.hxx>

#include <xmloff/xmltoken.hxx>
#include <xmloff/xmlnamespace.hxx>
#include <xmloff/families.hxx>
#include <xmloff/xmluconv.hxx>
#include <xmloff/namespacemap.hxx>
#include <xmloff/xmlexp.hxx>
#include <xmloff/xmlement.hxx>
#include <txtflde.hxx>


using namespace ::com::sun::star;
using namespace ::com::sun::star::text;
using namespace ::com::sun::star::uno;
using namespace ::xmloff::token;

using ::com::sun::star::beans::XPropertySet;
using ::com::sun::star::beans::PropertyValue;
using ::com::sun::star::beans::PropertyValues;
using ::com::sun::star::container::XIndexReplace;
using ::com::sun::star::container::XNameAccess;
using ::com::sun::star::container::XNamed;
using ::com::sun::star::lang::Locale;


XMLSectionExport::XMLSectionExport(
    SvXMLExport& rExp,
    XMLTextParagraphExport& rParaExp)
:   rExport(rExp)
,   rParaExport(rParaExp)
,   bHeadingDummiesExported( false )
{
}


void XMLSectionExport::ExportSectionStart(
    const Reference<XTextSection> & rSection,
    bool bAutoStyles)
{
    Reference<XPropertySet> xPropertySet(rSection, UNO_QUERY);

    // always export section (auto) style
    if (bAutoStyles)
    {
        // get PropertySet and add section style
        GetParaExport().Add( XmlStyleFamily::TEXT_SECTION, xPropertySet );
    }
    else
    {
        // always export section style
        GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_STYLE_NAME,
                                     GetParaExport().Find(
                                     XmlStyleFamily::TEXT_SECTION,
                                     xPropertySet, u""_ustr ) );

        // xml:id for RDF metadata
        GetExport().AddAttributeXmlId(rSection);

        // export index or regular section
        Reference<XDocumentIndex> xIndex;
        if (GetIndex(rSection, xIndex))
        {
            if (xIndex.is())
            {
                // we are an index
                ExportIndexStart(xIndex);
            }
            else
            {
                // we are an index header
                ExportIndexHeaderStart(rSection);
            }
        }
        else
        {
            // we are not an index
            ExportRegularSectionStart(rSection);
        }
    }
}

bool XMLSectionExport::GetIndex(
    const Reference<XTextSection> & rSection,
    Reference<XDocumentIndex> & rIndex)
{
    // first, reset result
    bool bRet = false;
    rIndex = nullptr;

    // get section Properties
    Reference<XPropertySet> xSectionPropSet(rSection, UNO_QUERY);

    // then check if this section happens to be inside an index
    if (xSectionPropSet->getPropertySetInfo()->
                                    hasPropertyByName(u"DocumentIndex"_ustr))
    {
        Any aAny = xSectionPropSet->getPropertyValue(u"DocumentIndex"_ustr);
        Reference<XDocumentIndex> xDocumentIndex;
        aAny >>= xDocumentIndex;

        // OK, are we inside of an index
        if (xDocumentIndex.is())
        {
            // is the enclosing index identical with "our" section?
            Reference<XPropertySet> xIndexPropSet(xDocumentIndex, UNO_QUERY);
            aAny = xIndexPropSet->getPropertyValue(u"ContentSection"_ustr);
            Reference<XTextSection> xEnclosingSection;
            aAny >>= xEnclosingSection;

            // if the enclosing section is "our" section, then we are an index!
            if (rSection == xEnclosingSection)
            {
                rIndex = std::move(xDocumentIndex);
                bRet = true;
            }
            // else: index header or regular section

            // is the enclosing index identical with the header section?
            aAny = xIndexPropSet->getPropertyValue(u"HeaderSection"_ustr);
            // now mis-named: contains header section
            aAny >>= xEnclosingSection;

            // if the enclosing section is "our" section, then we are an index!
            if (rSection == xEnclosingSection)
            {
                bRet = true;
            }
            // else: regular section
        }
        // else: we aren't even inside of an index
    }
    // else: we don't even know what an index is.

    return bRet;
}


void XMLSectionExport::ExportSectionEnd(
    const Reference<XTextSection> & rSection,
    bool bAutoStyles)
{
    // no end section for styles
    if (bAutoStyles)
        return;

    enum XMLTokenEnum eElement = XML_TOKEN_INVALID;

    // export index or regular section end
    Reference<XDocumentIndex> xIndex;
    if (GetIndex(rSection, xIndex))
    {
        if (xIndex.is())
        {
            // index end: close index body element
            GetExport().EndElement( XML_NAMESPACE_TEXT, XML_INDEX_BODY,
                                    true );
            GetExport().IgnorableWhitespace();

            switch (MapSectionType(xIndex->getServiceName()))
            {
                case TEXT_SECTION_TYPE_TOC:
                    eElement = XML_TABLE_OF_CONTENT;
                    break;

                case TEXT_SECTION_TYPE_ILLUSTRATION:
                    eElement = XML_ILLUSTRATION_INDEX;
                    break;

                case TEXT_SECTION_TYPE_ALPHABETICAL:
                    eElement = XML_ALPHABETICAL_INDEX;
                    break;

                case TEXT_SECTION_TYPE_TABLE:
                    eElement = XML_TABLE_INDEX;
                    break;

                case TEXT_SECTION_TYPE_OBJECT:
                    eElement = XML_OBJECT_INDEX;
                    break;

                case TEXT_SECTION_TYPE_USER:
                    eElement = XML_USER_INDEX;
                    break;

                case TEXT_SECTION_TYPE_BIBLIOGRAPHY:
                    eElement = XML_BIBLIOGRAPHY;
                    break;

                default:
                    OSL_FAIL("unknown index type");
                    // default: skip index!
                    break;
            }
        }
        else
        {
            eElement = XML_INDEX_TITLE;
        }
    }
    else
    {
        eElement = XML_SECTION;
    }

    if (XML_TOKEN_INVALID != eElement)
    {
        // any old attributes?
        GetExport().CheckAttrList();

        // element surrounded by whitespace
        GetExport().EndElement( XML_NAMESPACE_TEXT, eElement, true);
        GetExport().IgnorableWhitespace();
    }
    else
    {
        OSL_FAIL("Need element name!");
    }
    // else: autostyles -> ignore
}

void XMLSectionExport::ExportIndexStart(
    const Reference<XDocumentIndex> & rIndex)
{
    // get PropertySet
    Reference<XPropertySet> xPropertySet(rIndex, UNO_QUERY);

    switch (MapSectionType(rIndex->getServiceName()))
    {
        case TEXT_SECTION_TYPE_TOC:
            ExportTableOfContentStart(xPropertySet);
            break;

        case TEXT_SECTION_TYPE_ILLUSTRATION:
            ExportIllustrationIndexStart(xPropertySet);
            break;

        case TEXT_SECTION_TYPE_ALPHABETICAL:
            ExportAlphabeticalIndexStart(xPropertySet);
            break;

        case TEXT_SECTION_TYPE_TABLE:
            ExportTableIndexStart(xPropertySet);
            break;

        case TEXT_SECTION_TYPE_OBJECT:
            ExportObjectIndexStart(xPropertySet);
            break;

        case TEXT_SECTION_TYPE_USER:
            ExportUserIndexStart(xPropertySet);
            break;

        case TEXT_SECTION_TYPE_BIBLIOGRAPHY:
            ExportBibliographyStart(xPropertySet);
            break;

        default:
            // skip index
            OSL_FAIL("unknown index type");
            break;
    }
}

void XMLSectionExport::ExportIndexHeaderStart(
    const Reference<XTextSection> & rSection)
{
    // export name, dammit!
    Reference<XNamed> xName(rSection, UNO_QUERY);
    GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_NAME, xName->getName());
    Reference<XPropertySet> xPropSet(rSection, UNO_QUERY);
    Any aAny = xPropSet->getPropertyValue(u"IsProtected"_ustr);
    if (*o3tl::doAccess<bool>(aAny))
    {
        GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_PROTECTED, XML_TRUE);
    }

    // format already handled -> export only start element
    GetExport().StartElement( XML_NAMESPACE_TEXT, XML_INDEX_TITLE, true );
    GetExport().IgnorableWhitespace();
}


SvXMLEnumStringMapEntry<SectionTypeEnum> const aIndexTypeMap[] =
{
    ENUM_STRING_MAP_ENTRY( "com.sun.star.text.ContentIndex", TEXT_SECTION_TYPE_TOC ),
    ENUM_STRING_MAP_ENTRY( "com.sun.star.text.DocumentIndex", TEXT_SECTION_TYPE_ALPHABETICAL ),
    ENUM_STRING_MAP_ENTRY( "com.sun.star.text.TableIndex", TEXT_SECTION_TYPE_TABLE ),
    ENUM_STRING_MAP_ENTRY( "com.sun.star.text.ObjectIndex", TEXT_SECTION_TYPE_OBJECT ),
    ENUM_STRING_MAP_ENTRY( "com.sun.star.text.Bibliography", TEXT_SECTION_TYPE_BIBLIOGRAPHY ),
    ENUM_STRING_MAP_ENTRY( "com.sun.star.text.UserIndex", TEXT_SECTION_TYPE_USER ),
    ENUM_STRING_MAP_ENTRY( "com.sun.star.text.IllustrationsIndex", TEXT_SECTION_TYPE_ILLUSTRATION ),
    { nullptr, 0, SectionTypeEnum(0) }
};

enum SectionTypeEnum XMLSectionExport::MapSectionType(
    std::u16string_view rServiceName)
{
    enum SectionTypeEnum eType = TEXT_SECTION_TYPE_UNKNOWN;

    SvXMLUnitConverter::convertEnum(eType, rServiceName, aIndexTypeMap);

    // TODO: index header section types, etc.

    return eType;
}

void XMLSectionExport::ExportRegularSectionStart(
    const Reference<XTextSection> & rSection)
{
    // style name already handled in ExportSectionStart(...)

    Reference<XNamed> xName(rSection, UNO_QUERY);
    GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_NAME, xName->getName());

    // get XPropertySet for other values
    Reference<XPropertySet> xPropSet(rSection, UNO_QUERY);

    // condition and display
    Any aAny = xPropSet->getPropertyValue(u"Condition"_ustr);
    OUString sCond;
    aAny >>= sCond;
    enum XMLTokenEnum eDisplay = XML_TOKEN_INVALID;
    if (!sCond.isEmpty())
    {
        OUString sQValue =
            GetExport().GetNamespaceMap().GetQNameByKey( XML_NAMESPACE_OOOW,
                                                         sCond, false );
        GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_CONDITION, sQValue);
        eDisplay = XML_CONDITION;

        // #97450# store hidden-status (of conditional sections only)
        aAny = xPropSet->getPropertyValue(u"IsCurrentlyVisible"_ustr);
        if (! *o3tl::doAccess<bool>(aAny))
        {
            GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_IS_HIDDEN,
                                     XML_TRUE);
        }
    }
    else
    {
        eDisplay = XML_NONE;
    }
    aAny = xPropSet->getPropertyValue(u"IsVisible"_ustr);
    if (! *o3tl::doAccess<bool>(aAny))
    {
        GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_DISPLAY, eDisplay);
    }

    // protect + protection key
    aAny = xPropSet->getPropertyValue(u"IsProtected"_ustr);
    if (*o3tl::doAccess<bool>(aAny))
    {
        GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_PROTECTED, XML_TRUE);
    }
    Sequence<sal_Int8> aPassword;
    xPropSet->getPropertyValue(u"ProtectionKey"_ustr) >>= aPassword;
    if (aPassword.hasElements())
    {
        OUStringBuffer aBuffer;
        ::comphelper::Base64::encode(aBuffer, aPassword);
        // in ODF 1.0/1.1 the algorithm was left unspecified so we can write anything
        GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_PROTECTION_KEY,
                                 aBuffer.makeStringAndClear());
        if (aPassword.getLength() == 32 && GetExport().getSaneDefaultVersion() >= SvtSaveOptions::ODFSVER_012)
        {
            // attribute exists in ODF 1.2 or later; default is SHA1 so no need to write that
            GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_PROTECTION_KEY_DIGEST_ALGORITHM,
                    // write the URL from ODF 1.2, not the W3C one
                    u"http://www.w3.org/2000/09/xmldsig#sha256"_ustr);
        }
    }

    // export element
    GetExport().IgnorableWhitespace();
    GetExport().StartElement( XML_NAMESPACE_TEXT, XML_SECTION, true );

    // data source
    // unfortunately, we have to test all relevant strings for non-zero length
    aAny = xPropSet->getPropertyValue(u"FileLink"_ustr);
    SectionFileLink aFileLink;
    aAny >>= aFileLink;

    aAny = xPropSet->getPropertyValue(u"LinkRegion"_ustr);
    OUString sRegionName;
    aAny >>= sRegionName;

    if ( !aFileLink.FileURL.isEmpty() ||
         !aFileLink.FilterName.isEmpty() ||
         !sRegionName.isEmpty())
    {
        if (!aFileLink.FileURL.isEmpty())
        {
            GetExport().AddAttribute(XML_NAMESPACE_XLINK, XML_HREF,
                                     GetExport().GetRelativeReference( aFileLink.FileURL) );
        }

        if (!aFileLink.FilterName.isEmpty())
        {
            GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_FILTER_NAME,
                                     aFileLink.FilterName);
        }

        if (!sRegionName.isEmpty())
        {
            GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_SECTION_NAME,
                                     sRegionName);
        }

        SvXMLElementExport aElem(GetExport(),
                                 XML_NAMESPACE_TEXT, XML_SECTION_SOURCE,
                                 true, true);
    }
    else
    {
        // check for DDE first
        if (xPropSet->getPropertySetInfo()->hasPropertyByName(u"DDECommandFile"_ustr))
        {
            // data source DDE
            // unfortunately, we have to test all relevant strings for
            // non-zero length
            aAny = xPropSet->getPropertyValue(u"DDECommandFile"_ustr);
            OUString sApplication;
            aAny >>= sApplication;
            aAny = xPropSet->getPropertyValue(u"DDECommandType"_ustr);
            OUString sTopic;
            aAny >>= sTopic;
            aAny = xPropSet->getPropertyValue(u"DDECommandElement"_ustr);
            OUString sItem;
            aAny >>= sItem;

            if ( !sApplication.isEmpty() ||
                 !sTopic.isEmpty() ||
                 !sItem.isEmpty())
            {
                GetExport().AddAttribute(XML_NAMESPACE_OFFICE,
                                         XML_DDE_APPLICATION, sApplication);
                GetExport().AddAttribute(XML_NAMESPACE_OFFICE, XML_DDE_TOPIC,
                                         sTopic);
                GetExport().AddAttribute(XML_NAMESPACE_OFFICE, XML_DDE_ITEM,
                                         sItem);

                aAny = xPropSet->getPropertyValue(u"IsAutomaticUpdate"_ustr);
                if (*o3tl::doAccess<bool>(aAny))
                {
                    GetExport().AddAttribute(XML_NAMESPACE_OFFICE,
                                             XML_AUTOMATIC_UPDATE, XML_TRUE);
                }

                SvXMLElementExport aElem(GetExport(),
                                         XML_NAMESPACE_OFFICE,
                                         XML_DDE_SOURCE, true, true);
            }
            // else: no DDE data source
        }
        // else: no DDE on this system
    }
}

void XMLSectionExport::ExportTableOfContentStart(
    const Reference<XPropertySet> & rPropertySet)
{
    // export TOC element start
    ExportBaseIndexStart(XML_TABLE_OF_CONTENT, rPropertySet);

    // scope for table-of-content-source element
    {
        // TOC specific index source attributes:

        // outline-level: 1..10
        sal_Int16 nLevel = sal_Int16();
        if( rPropertySet->getPropertyValue(u"Level"_ustr) >>= nLevel )
        {
            GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                     XML_OUTLINE_LEVEL,
                                     OUString::number(nLevel));
        }

        // use outline level
        ExportBoolean(rPropertySet, u"CreateFromOutline"_ustr,
                          XML_USE_OUTLINE_LEVEL, true);

        // use index marks
        ExportBoolean(rPropertySet, u"CreateFromMarks"_ustr,
                      XML_USE_INDEX_MARKS, true);

        // use level styles
        ExportBoolean(rPropertySet, u"CreateFromLevelParagraphStyles"_ustr,
                      XML_USE_INDEX_SOURCE_STYLES, false);

        ExportBaseIndexSource(TEXT_SECTION_TYPE_TOC, rPropertySet);
    }

    ExportBaseIndexBody(TEXT_SECTION_TYPE_TOC, rPropertySet);
}

void XMLSectionExport::ExportObjectIndexStart(
    const Reference<XPropertySet> & rPropertySet)
{
    // export index start
    ExportBaseIndexStart(XML_OBJECT_INDEX, rPropertySet);

    // scope for index source element
    {
        ExportBoolean(rPropertySet, u"CreateFromOtherEmbeddedObjects"_ustr,
                      XML_USE_OTHER_OBJECTS, false);
        ExportBoolean(rPropertySet, u"CreateFromStarCalc"_ustr,
                      XML_USE_SPREADSHEET_OBJECTS, false);
        ExportBoolean(rPropertySet, u"CreateFromStarChart"_ustr,
                      XML_USE_CHART_OBJECTS, false);
        ExportBoolean(rPropertySet, u"CreateFromStarDraw"_ustr,
                      XML_USE_DRAW_OBJECTS, false);
        ExportBoolean(rPropertySet, u"CreateFromStarMath"_ustr,
                      XML_USE_MATH_OBJECTS, false);

        ExportBaseIndexSource(TEXT_SECTION_TYPE_OBJECT, rPropertySet);
    }

    ExportBaseIndexBody(TEXT_SECTION_TYPE_OBJECT, rPropertySet);
}

void XMLSectionExport::ExportIllustrationIndexStart(
    const Reference<XPropertySet> & rPropertySet)
{
    // export index start
    ExportBaseIndexStart(XML_ILLUSTRATION_INDEX, rPropertySet);

    // scope for index source element
    {
        // export common attributes for illustration and table indices
        ExportTableAndIllustrationIndexSourceAttributes(rPropertySet);

        ExportBaseIndexSource(TEXT_SECTION_TYPE_ILLUSTRATION, rPropertySet);
    }

    ExportBaseIndexBody(TEXT_SECTION_TYPE_ILLUSTRATION, rPropertySet);
}

void XMLSectionExport::ExportTableIndexStart(
    const Reference<XPropertySet> & rPropertySet)
{
    // export index start
    ExportBaseIndexStart(XML_TABLE_INDEX, rPropertySet);

    // scope for index source element
    {
        // export common attributes for illustration and table indices
        ExportTableAndIllustrationIndexSourceAttributes(rPropertySet);

        ExportBaseIndexSource(TEXT_SECTION_TYPE_TABLE, rPropertySet);
    }

    ExportBaseIndexBody(TEXT_SECTION_TYPE_TABLE, rPropertySet);
}

void XMLSectionExport::ExportAlphabeticalIndexStart(
    const Reference<XPropertySet> & rPropertySet)
{
    // export TOC element start
    ExportBaseIndexStart(XML_ALPHABETICAL_INDEX, rPropertySet);

    // scope for table-of-content-source element
    {

        // style name (if present)
        Any aAny = rPropertySet->getPropertyValue(u"MainEntryCharacterStyleName"_ustr);
        OUString sStyleName;
        aAny >>= sStyleName;
        if (!sStyleName.isEmpty())
        {
            GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                     XML_MAIN_ENTRY_STYLE_NAME,
                                     GetExport().EncodeStyleName( sStyleName ));
        }

        // other (boolean) attributes
        ExportBoolean(rPropertySet, u"IsCaseSensitive"_ustr, XML_IGNORE_CASE,
                      false, true);
        ExportBoolean(rPropertySet, u"UseAlphabeticalSeparators"_ustr,
                      XML_ALPHABETICAL_SEPARATORS, false);
        ExportBoolean(rPropertySet, u"UseCombinedEntries"_ustr, XML_COMBINE_ENTRIES,
                      true);
        ExportBoolean(rPropertySet, u"UseDash"_ustr, XML_COMBINE_ENTRIES_WITH_DASH,
                      false);
        ExportBoolean(rPropertySet, u"UseKeyAsEntry"_ustr, XML_USE_KEYS_AS_ENTRIES,
                      false);
        ExportBoolean(rPropertySet, u"UsePP"_ustr, XML_COMBINE_ENTRIES_WITH_PP,
                      true);
        ExportBoolean(rPropertySet, u"UseUpperCase"_ustr, XML_CAPITALIZE_ENTRIES,
                      false);
        ExportBoolean(rPropertySet, u"IsCommaSeparated"_ustr, XML_COMMA_SEPARATED,
                      false);

        // sort algorithm
        aAny = rPropertySet->getPropertyValue(u"SortAlgorithm"_ustr);
        OUString sAlgorithm;
        aAny >>= sAlgorithm;
        if (!sAlgorithm.isEmpty())
        {
            GetExport().AddAttribute( XML_NAMESPACE_TEXT, XML_SORT_ALGORITHM,
                                      sAlgorithm );
        }

        // locale
        aAny = rPropertySet->getPropertyValue(u"Locale"_ustr);
        Locale aLocale;
        aAny >>= aLocale;
        GetExport().AddLanguageTagAttributes( XML_NAMESPACE_FO, XML_NAMESPACE_STYLE, aLocale, true);

        ExportBaseIndexSource(TEXT_SECTION_TYPE_ALPHABETICAL, rPropertySet);
    }

    ExportBaseIndexBody(TEXT_SECTION_TYPE_ALPHABETICAL, rPropertySet);
}

void XMLSectionExport::ExportUserIndexStart(
    const Reference<XPropertySet> & rPropertySet)
{
    // export TOC element start
    ExportBaseIndexStart(XML_USER_INDEX, rPropertySet);

    // scope for table-of-content-source element
    {
        // bool attributes
        ExportBoolean(rPropertySet, u"CreateFromEmbeddedObjects"_ustr,
                      XML_USE_OBJECTS, false);
        ExportBoolean(rPropertySet, u"CreateFromGraphicObjects"_ustr,
                      XML_USE_GRAPHICS, false);
        ExportBoolean(rPropertySet, u"CreateFromMarks"_ustr,
                      XML_USE_INDEX_MARKS, false);
        ExportBoolean(rPropertySet, u"CreateFromTables"_ustr,
                      XML_USE_TABLES, false);
        ExportBoolean(rPropertySet, u"CreateFromTextFrames"_ustr,
                      XML_USE_FLOATING_FRAMES, false);
        ExportBoolean(rPropertySet, u"UseLevelFromSource"_ustr,
                      XML_COPY_OUTLINE_LEVELS, false);
        ExportBoolean(rPropertySet, u"CreateFromLevelParagraphStyles"_ustr,
                      XML_USE_INDEX_SOURCE_STYLES, false);

        Any aAny = rPropertySet->getPropertyValue( u"UserIndexName"_ustr );
        OUString sIndexName;
        aAny >>= sIndexName;
        GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_INDEX_NAME,
                                 sIndexName);

        ExportBaseIndexSource(TEXT_SECTION_TYPE_USER, rPropertySet);
    }

    ExportBaseIndexBody(TEXT_SECTION_TYPE_USER, rPropertySet);
}

void XMLSectionExport::ExportBibliographyStart(
    const Reference<XPropertySet> & rPropertySet)
{
    // export TOC element start
    ExportBaseIndexStart(XML_BIBLIOGRAPHY, rPropertySet);

    // scope for table-of-content-source element
    {
        // No attributes. Fine.

        ExportBaseIndexSource(TEXT_SECTION_TYPE_BIBLIOGRAPHY, rPropertySet);
    }

    ExportBaseIndexBody(TEXT_SECTION_TYPE_BIBLIOGRAPHY, rPropertySet);
}


void XMLSectionExport::ExportBaseIndexStart(
    XMLTokenEnum eElement,
    const Reference<XPropertySet> & rPropertySet)
{
    // protect + protection key
    Any aAny = rPropertySet->getPropertyValue(u"IsProtected"_ustr);
    if (*o3tl::doAccess<bool>(aAny))
    {
        GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_PROTECTED, XML_TRUE);
    }

    // index name
    OUString sIndexName;
    rPropertySet->getPropertyValue(u"Name"_ustr) >>= sIndexName;
    if ( !sIndexName.isEmpty() )
    {
        GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_NAME, sIndexName);
    }

    // index  Element start
    GetExport().IgnorableWhitespace();
    GetExport().StartElement( XML_NAMESPACE_TEXT, eElement, false );
}

const XMLTokenEnum aTypeSourceElementNameMap[] =
{
    XML_TABLE_OF_CONTENT_SOURCE,        // TOC
    XML_TABLE_INDEX_SOURCE,         // table index
    XML_ILLUSTRATION_INDEX_SOURCE,      // illustration index
    XML_OBJECT_INDEX_SOURCE,            // object index
    XML_USER_INDEX_SOURCE,              // user index
    XML_ALPHABETICAL_INDEX_SOURCE,      // alphabetical index
    XML_BIBLIOGRAPHY_SOURCE         // bibliography
};

void XMLSectionExport::ExportBaseIndexSource(
    SectionTypeEnum eType,
    const Reference<XPropertySet> & rPropertySet)
{
    // check type
    OSL_ENSURE(eType >= TEXT_SECTION_TYPE_TOC, "illegal index type");
    OSL_ENSURE(eType <= TEXT_SECTION_TYPE_BIBLIOGRAPHY, "illegal index type");

    Any aAny;

    // common attributes; not supported by bibliography
    if (eType != TEXT_SECTION_TYPE_BIBLIOGRAPHY)
    {
        // document or chapter index?
        aAny = rPropertySet->getPropertyValue(u"CreateFromChapter"_ustr);
        if (*o3tl::doAccess<bool>(aAny))
        {
            GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                     XML_INDEX_SCOPE, XML_CHAPTER);
        }

        // tab-stops relative to margin?
        aAny = rPropertySet->getPropertyValue(u"IsRelativeTabstops"_ustr);
        if (! *o3tl::doAccess<bool>(aAny))
        {
            GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                     XML_RELATIVE_TAB_STOP_POSITION,
                                     XML_FALSE);
        }
    }

    // the index source element (all indices)
    SvXMLElementExport aElem(GetExport(),
                             XML_NAMESPACE_TEXT,
                             GetXMLToken(
                                 aTypeSourceElementNameMap[
                                    eType - TEXT_SECTION_TYPE_TOC]),
                             true, true);

    // scope for title template (all indices)
    {
        // header style name
        aAny = rPropertySet->getPropertyValue(u"ParaStyleHeading"_ustr);
        OUString sStyleName;
        aAny >>= sStyleName;
        GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                 XML_STYLE_NAME,
                                 GetExport().EncodeStyleName( sStyleName ));

        // title template
        SvXMLElementExport aHeaderTemplate(GetExport(),
                                           XML_NAMESPACE_TEXT,
                                           XML_INDEX_TITLE_TEMPLATE,
                                           true, false);

        // title as element content
        aAny = rPropertySet->getPropertyValue(u"Title"_ustr);
        OUString sTitleString;
        aAny >>= sTitleString;
        GetExport().Characters(sTitleString);
    }

    // export level templates (all indices)
    aAny = rPropertySet->getPropertyValue(u"LevelFormat"_ustr);
    Reference<XIndexReplace> xLevelTemplates;
    aAny >>= xLevelTemplates;

    // iterate over level formats;
    // skip element 0 (empty template for title)
    sal_Int32 nLevelCount = xLevelTemplates->getCount();
    for(sal_Int32 i = 1; i<nLevelCount; i++)
    {
        // get sequence
        Sequence<PropertyValues> aTemplateSequence;
        aAny = xLevelTemplates->getByIndex(i);
        aAny >>= aTemplateSequence;

        // export the sequence (abort export if an error occurred; #91214#)
        bool bResult =
            ExportIndexTemplate(eType, i, rPropertySet, aTemplateSequence);
        if ( !bResult )
            break;
    }

    // only TOC and user index:
    // styles from which to build the index (LevelParagraphStyles)
    if ( (TEXT_SECTION_TYPE_TOC == eType) ||
         (TEXT_SECTION_TYPE_USER == eType)   )
    {
        aAny = rPropertySet->getPropertyValue(u"LevelParagraphStyles"_ustr);
        Reference<XIndexReplace> xLevelParagraphStyles;
        aAny >>= xLevelParagraphStyles;
        ExportLevelParagraphStyles(xLevelParagraphStyles);
    }
    else if (TEXT_SECTION_TYPE_ILLUSTRATION == eType
            || TEXT_SECTION_TYPE_OBJECT == eType
            || TEXT_SECTION_TYPE_TABLE == eType)
    {
        Any const any(rPropertySet->getPropertyValue(u"CreateFromParagraphStyle"_ustr));
        if (any.hasValue() &&
            (rExport.getSaneDefaultVersion() & SvtSaveOptions::ODFSVER_EXTENDED))
        {
            OUString const styleName(any.get<OUString>());
            GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_STYLE_NAME,
                         GetExport().EncodeStyleName(styleName));

            SvXMLElementExport const e(GetExport(),
                XML_NAMESPACE_LO_EXT, XML_INDEX_SOURCE_STYLE, true, false);
        }
    }
}


void XMLSectionExport::ExportBaseIndexBody(
    SectionTypeEnum eType,
    const Reference<XPropertySet> &)
{
    // type not used; checked anyway.
    OSL_ENSURE(eType >= TEXT_SECTION_TYPE_TOC, "illegal index type");
    OSL_ENSURE(eType <= TEXT_SECTION_TYPE_BIBLIOGRAPHY, "illegal index type");

    // export start only

    // any old attributes?
    GetExport().CheckAttrList();

    // start surrounded by whitespace
    GetExport().IgnorableWhitespace();
    GetExport().StartElement( XML_NAMESPACE_TEXT, XML_INDEX_BODY, true );
}

void XMLSectionExport::ExportTableAndIllustrationIndexSourceAttributes(
    const Reference<XPropertySet> & rPropertySet)
{
    // use caption
    Any aAny = rPropertySet->getPropertyValue(u"CreateFromLabels"_ustr);
    if (! *o3tl::doAccess<bool>(aAny))
    {
        GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                 XML_USE_CAPTION, XML_FALSE);
    }

    // sequence name
    aAny = rPropertySet->getPropertyValue(u"LabelCategory"_ustr);
    OUString sSequenceName;
    aAny >>= sSequenceName;
    GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                             XML_CAPTION_SEQUENCE_NAME,
                             sSequenceName);

    // caption format
    aAny = rPropertySet->getPropertyValue(u"LabelDisplayType"_ustr);
    sal_Int16 nType = 0;
    aAny >>= nType;
    GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                             XML_CAPTION_SEQUENCE_FORMAT,
                             XMLTextFieldExport::MapReferenceType(nType));
}


// map index of LevelFormats to attribute value;
// level 0 is always the header
const XMLTokenEnum aLevelNameTOCMap[] =
    { XML_TOKEN_INVALID, XML_1, XML_2, XML_3, XML_4, XML_5, XML_6, XML_7,
          XML_8, XML_9, XML_10, XML_TOKEN_INVALID };
const XMLTokenEnum aLevelNameTableMap[] =
    { XML_TOKEN_INVALID, XML__EMPTY, XML_TOKEN_INVALID };
const XMLTokenEnum aLevelNameAlphaMap[] =
    { XML_TOKEN_INVALID, XML_SEPARATOR, XML_1, XML_2, XML_3, XML_TOKEN_INVALID };
const XMLTokenEnum aLevelNameBibliographyMap[] =
    { XML_TOKEN_INVALID, XML_ARTICLE, XML_BOOK, XML_BOOKLET, XML_CONFERENCE,
          XML_CUSTOM1, XML_CUSTOM2, XML_CUSTOM3, XML_CUSTOM4,
          XML_CUSTOM5, XML_EMAIL, XML_INBOOK, XML_INCOLLECTION,
          XML_INPROCEEDINGS, XML_JOURNAL,
          XML_MANUAL, XML_MASTERSTHESIS, XML_MISC, XML_PHDTHESIS,
          XML_PROCEEDINGS, XML_TECHREPORT, XML_UNPUBLISHED, XML_WWW,
          XML_TOKEN_INVALID };

const XMLTokenEnum* const aTypeLevelNameMap[] =
{
    aLevelNameTOCMap,           // TOC
    aLevelNameTableMap,         // table index
    aLevelNameTableMap,         // illustration index
    aLevelNameTableMap,         // object index
    aLevelNameTOCMap,           // user index
    aLevelNameAlphaMap,         // alphabetical index
    aLevelNameBibliographyMap   // bibliography
};

constexpr OUString aLevelStylePropNameTOCMap[] =
    { u""_ustr, u"ParaStyleLevel1"_ustr, u"ParaStyleLevel2"_ustr, u"ParaStyleLevel3"_ustr,
          u"ParaStyleLevel4"_ustr, u"ParaStyleLevel5"_ustr, u"ParaStyleLevel6"_ustr,
          u"ParaStyleLevel7"_ustr, u"ParaStyleLevel8"_ustr, u"ParaStyleLevel9"_ustr,
          u"ParaStyleLevel10"_ustr, u""_ustr };
constexpr OUString aLevelStylePropNameTableMap[] =
    { u""_ustr, u"ParaStyleLevel1"_ustr, u""_ustr };
constexpr OUString aLevelStylePropNameAlphaMap[] =
    { u""_ustr, u"ParaStyleSeparator"_ustr, u"ParaStyleLevel1"_ustr, u"ParaStyleLevel2"_ustr,
          u"ParaStyleLevel3"_ustr, u""_ustr };
constexpr OUString aLevelStylePropNameBibliographyMap[] =
          // TODO: replace with real property names, when available
    { u""_ustr, u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr,
          u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr,
          u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr,
          u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr,
          u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr,u"ParaStyleLevel1"_ustr,
          u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr,
          u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr, u"ParaStyleLevel1"_ustr,
          u"ParaStyleLevel1"_ustr,
          u""_ustr };

constexpr const OUString* aTypeLevelStylePropNameMap[] =
{
    aLevelStylePropNameTOCMap,          // TOC
    aLevelStylePropNameTableMap,        // table index
    aLevelStylePropNameTableMap,        // illustration index
    aLevelStylePropNameTableMap,        // object index
    aLevelStylePropNameTOCMap,          // user index
    aLevelStylePropNameAlphaMap,        // alphabetical index
    aLevelStylePropNameBibliographyMap  // bibliography
};

const XMLTokenEnum aTypeLevelAttrMap[] =
{
    XML_OUTLINE_LEVEL,      // TOC
    XML_TOKEN_INVALID,      // table index
    XML_TOKEN_INVALID,      // illustration index
    XML_TOKEN_INVALID,      // object index
    XML_OUTLINE_LEVEL,      // user index
    XML_OUTLINE_LEVEL,      // alphabetical index
    XML_BIBLIOGRAPHY_TYPE   // bibliography
};

const XMLTokenEnum aTypeElementNameMap[] =
{
    XML_TABLE_OF_CONTENT_ENTRY_TEMPLATE,    // TOC
    XML_TABLE_INDEX_ENTRY_TEMPLATE,     // table index
    XML_ILLUSTRATION_INDEX_ENTRY_TEMPLATE,  // illustration index
    XML_OBJECT_INDEX_ENTRY_TEMPLATE,        // object index
    XML_USER_INDEX_ENTRY_TEMPLATE,          // user index
    XML_ALPHABETICAL_INDEX_ENTRY_TEMPLATE,  // alphabetical index
    XML_BIBLIOGRAPHY_ENTRY_TEMPLATE     // bibliography
};


bool XMLSectionExport::ExportIndexTemplate(
    SectionTypeEnum eType,
    sal_Int32 nOutlineLevel,
    const Reference<XPropertySet> & rPropertySet,
    const Sequence<Sequence<PropertyValue> > & rValues)
{
    OSL_ENSURE(eType >= TEXT_SECTION_TYPE_TOC, "illegal index type");
    OSL_ENSURE(eType <= TEXT_SECTION_TYPE_BIBLIOGRAPHY, "illegal index type");
    OSL_ENSURE(nOutlineLevel >= 0, "illegal outline level");

    if ( (eType >= TEXT_SECTION_TYPE_TOC) &&
         (eType <= TEXT_SECTION_TYPE_BIBLIOGRAPHY) &&
         (nOutlineLevel >= 0) )
    {
        // get level name and level attribute name from aLevelNameMap;
        const XMLTokenEnum eLevelAttrName(
            aTypeLevelAttrMap[eType-TEXT_SECTION_TYPE_TOC]);
        const XMLTokenEnum eLevelName(
            aTypeLevelNameMap[eType-TEXT_SECTION_TYPE_TOC][nOutlineLevel]);

        // #92124#: some old documents may be broken, then they have
        // too many template levels; we need to recognize this and
        // export only as many as is legal for the respective index
        // type. To do this, we simply return an error flag, which
        // will then abort further template level exports.
        OSL_ENSURE(XML_TOKEN_INVALID != eLevelName, "can't find level name");
        if ( XML_TOKEN_INVALID == eLevelName )
        {
            // output level not found? Then end of templates! #91214#
            return false;
        }

        // output level name
        if ((XML_TOKEN_INVALID != eLevelName) && (XML_TOKEN_INVALID != eLevelAttrName))
        {
            GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                          GetXMLToken(eLevelAttrName),
                                          GetXMLToken(eLevelName));
        }

        // paragraph level style name
        const OUString pPropName(
            aTypeLevelStylePropNameMap[eType-TEXT_SECTION_TYPE_TOC][nOutlineLevel]);
        OSL_ENSURE(!pPropName.isEmpty(), "can't find property name");
        if (!pPropName.isEmpty())
        {
            Any aAny = rPropertySet->getPropertyValue(pPropName);
            OUString sParaStyleName;
            aAny >>= sParaStyleName;
            GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                     XML_STYLE_NAME,
                                     GetExport().EncodeStyleName( sParaStyleName ));
        }

        // template element
        const XMLTokenEnum eElementName(
            aTypeElementNameMap[eType - TEXT_SECTION_TYPE_TOC]);
        SvXMLElementExport aLevelTemplate(GetExport(),
                                          XML_NAMESPACE_TEXT,
                                          GetXMLToken(eElementName),
                                          true, true);

        // export sequence
        for(auto& rValue : rValues)
        {
            ExportIndexTemplateElement(
                eType,  //i90246
                rValue);
        }
    }

    return true;
}

namespace {

enum TemplateTypeEnum
{
    TOK_TTYPE_ENTRY_NUMBER,
    TOK_TTYPE_ENTRY_TEXT,
    TOK_TTYPE_TAB_STOP,
    TOK_TTYPE_TEXT,
    TOK_TTYPE_PAGE_NUMBER,
    TOK_TTYPE_CHAPTER_INFO,
    TOK_TTYPE_HYPERLINK_START,
    TOK_TTYPE_HYPERLINK_END,
    TOK_TTYPE_BIBLIOGRAPHY,
    TOK_TTYPE_INVALID
};

enum TemplateParamEnum
{
    TOK_TPARAM_TOKEN_TYPE,
    TOK_TPARAM_CHAR_STYLE,
    TOK_TPARAM_TAB_RIGHT_ALIGNED,
    TOK_TPARAM_TAB_POSITION,
    TOK_TPARAM_TAB_WITH_TAB, // #i21237#
    TOK_TPARAM_TAB_FILL_CHAR,
    TOK_TPARAM_TEXT,
    TOK_TPARAM_CHAPTER_FORMAT,
    TOK_TPARAM_CHAPTER_LEVEL,//i53420
    TOK_TPARAM_BIBLIOGRAPHY_DATA
};

}

SvXMLEnumStringMapEntry<TemplateTypeEnum> const aTemplateTypeMap[] =
{
    ENUM_STRING_MAP_ENTRY( "TokenEntryNumber",  TOK_TTYPE_ENTRY_NUMBER ),
    ENUM_STRING_MAP_ENTRY( "TokenEntryText",    TOK_TTYPE_ENTRY_TEXT ),
    ENUM_STRING_MAP_ENTRY( "TokenTabStop",      TOK_TTYPE_TAB_STOP ),
    ENUM_STRING_MAP_ENTRY( "TokenText",         TOK_TTYPE_TEXT ),
    ENUM_STRING_MAP_ENTRY( "TokenPageNumber",   TOK_TTYPE_PAGE_NUMBER ),
    ENUM_STRING_MAP_ENTRY( "TokenChapterInfo",  TOK_TTYPE_CHAPTER_INFO ),
    ENUM_STRING_MAP_ENTRY( "TokenHyperlinkStart", TOK_TTYPE_HYPERLINK_START ),
    ENUM_STRING_MAP_ENTRY( "TokenHyperlinkEnd", TOK_TTYPE_HYPERLINK_END ),
    ENUM_STRING_MAP_ENTRY( "TokenBibliographyDataField", TOK_TTYPE_BIBLIOGRAPHY ),
    { nullptr, 0, TemplateTypeEnum(0)}
};

SvXMLEnumStringMapEntry<TemplateParamEnum> const aTemplateParamMap[] =
{
    ENUM_STRING_MAP_ENTRY( "TokenType",             TOK_TPARAM_TOKEN_TYPE ),
    ENUM_STRING_MAP_ENTRY( "CharacterStyleName",    TOK_TPARAM_CHAR_STYLE ),
    ENUM_STRING_MAP_ENTRY( "TabStopRightAligned",   TOK_TPARAM_TAB_RIGHT_ALIGNED ),
    ENUM_STRING_MAP_ENTRY( "TabStopPosition",       TOK_TPARAM_TAB_POSITION ),
    ENUM_STRING_MAP_ENTRY( "TabStopFillCharacter",  TOK_TPARAM_TAB_FILL_CHAR ),
    // #i21237#
    ENUM_STRING_MAP_ENTRY( "WithTab",               TOK_TPARAM_TAB_WITH_TAB ),
    ENUM_STRING_MAP_ENTRY( "Text",                  TOK_TPARAM_TEXT ),
    ENUM_STRING_MAP_ENTRY( "ChapterFormat",         TOK_TPARAM_CHAPTER_FORMAT ),
    ENUM_STRING_MAP_ENTRY( "ChapterLevel",          TOK_TPARAM_CHAPTER_LEVEL ),//i53420
    ENUM_STRING_MAP_ENTRY( "BibliographyDataField", TOK_TPARAM_BIBLIOGRAPHY_DATA ),
    { nullptr, 0, TemplateParamEnum(0)}
};

SvXMLEnumMapEntry<sal_Int16> const aBibliographyDataFieldMap[] =
{
    { XML_ADDRESS,              BibliographyDataField::ADDRESS },
    { XML_ANNOTE,               BibliographyDataField::ANNOTE },
    { XML_AUTHOR,               BibliographyDataField::AUTHOR },
    { XML_BIBLIOGRAPHY_TYPE,    BibliographyDataField::BIBILIOGRAPHIC_TYPE },
    { XML_BOOKTITLE,            BibliographyDataField::BOOKTITLE },
    { XML_CHAPTER,              BibliographyDataField::CHAPTER },
    { XML_CUSTOM1,              BibliographyDataField::CUSTOM1 },
    { XML_CUSTOM2,              BibliographyDataField::CUSTOM2 },
    { XML_CUSTOM3,              BibliographyDataField::CUSTOM3 },
    { XML_CUSTOM4,              BibliographyDataField::CUSTOM4 },
    { XML_CUSTOM5,              BibliographyDataField::CUSTOM5 },
    { XML_EDITION,              BibliographyDataField::EDITION },
    { XML_EDITOR,               BibliographyDataField::EDITOR },
    { XML_HOWPUBLISHED,         BibliographyDataField::HOWPUBLISHED },
    { XML_IDENTIFIER,           BibliographyDataField::IDENTIFIER },
    { XML_INSTITUTION,          BibliographyDataField::INSTITUTION },
    { XML_ISBN,                 BibliographyDataField::ISBN },
    { XML_JOURNAL,              BibliographyDataField::JOURNAL },
    { XML_MONTH,                BibliographyDataField::MONTH },
    { XML_NOTE,                 BibliographyDataField::NOTE },
    { XML_NUMBER,               BibliographyDataField::NUMBER },
    { XML_ORGANIZATIONS,        BibliographyDataField::ORGANIZATIONS },
    { XML_PAGES,                BibliographyDataField::PAGES },
    { XML_PUBLISHER,            BibliographyDataField::PUBLISHER },
    { XML_REPORT_TYPE,          BibliographyDataField::REPORT_TYPE },
    { XML_SCHOOL,               BibliographyDataField::SCHOOL },
    { XML_SERIES,               BibliographyDataField::SERIES },
    { XML_TITLE,                BibliographyDataField::TITLE },
    { XML_URL,                  BibliographyDataField::URL },
    { XML_VOLUME,               BibliographyDataField::VOLUME },
    { XML_YEAR,                 BibliographyDataField::YEAR },
    { XML_TOKEN_INVALID, 0 }
};

void XMLSectionExport::ExportIndexTemplateElement(
    SectionTypeEnum eType,  //i90246
    const Sequence<PropertyValue> & rValues)
{
    // variables for template values

    // char style
    OUString sCharStyle;
    bool bCharStyleOK = false;

    // text
    OUString sText;
    bool bTextOK = false;

    // tab position
    bool bRightAligned = false;

    // tab position
    sal_Int32 nTabPosition = 0;
    bool bTabPositionOK = false;

    // fill character
    OUString sFillChar;
    bool bFillCharOK = false;

    // chapter format
    sal_Int16 nChapterFormat = 0;
    bool bChapterFormatOK = false;

    // outline max level
    sal_Int16 nLevel = 0;
    bool bLevelOK = false;

    // Bibliography Data
    sal_Int16 nBibliographyData = 0;
    bool bBibliographyDataOK = false;

    // With Tab Stop #i21237#
    bool bWithTabStop = false;
    bool bWithTabStopOK = false;

    //i90246, the ODF version being written to is:
    const SvtSaveOptions::ODFSaneDefaultVersion aODFVersion = rExport.getSaneDefaultVersion();
    //the above version cannot be used for old OOo (OOo 1.0) formats!

    // token type
    enum TemplateTypeEnum nTokenType = TOK_TTYPE_INVALID;

    for(const auto& rValue : rValues)
    {
        TemplateParamEnum nToken;
        if ( SvXMLUnitConverter::convertEnum( nToken, rValue.Name,
                                              aTemplateParamMap ) )
        {
            // Only use direct and default values.
            // Wrong. no property states, so ignore.
            // if ( (beans::PropertyState_DIRECT_VALUE == rValues[i].State) ||
            //      (beans::PropertyState_DEFAULT_VALUE == rValues[i].State)  )

            switch (nToken)
            {
                case TOK_TPARAM_TOKEN_TYPE:
                {
                    OUString sVal;
                    rValue.Value >>= sVal;
                    SvXMLUnitConverter::convertEnum( nTokenType, sVal, aTemplateTypeMap);
                    break;
                }

                case TOK_TPARAM_CHAR_STYLE:
                    // only valid, if not empty
                    rValue.Value >>= sCharStyle;
                    bCharStyleOK = !sCharStyle.isEmpty();
                    break;

                case TOK_TPARAM_TEXT:
                    rValue.Value >>= sText;
                    bTextOK = true;
                    break;

                case TOK_TPARAM_TAB_RIGHT_ALIGNED:
                    bRightAligned =
                        *o3tl::doAccess<bool>(rValue.Value);
                    break;

                case TOK_TPARAM_TAB_POSITION:
                    rValue.Value >>= nTabPosition;
                    bTabPositionOK = true;
                    break;

                // #i21237#
                case TOK_TPARAM_TAB_WITH_TAB:
                    bWithTabStop = *o3tl::doAccess<bool>(rValue.Value);
                    bWithTabStopOK = true;
                    break;

                case TOK_TPARAM_TAB_FILL_CHAR:
                    rValue.Value >>= sFillChar;
                    bFillCharOK = true;
                    break;

                case TOK_TPARAM_CHAPTER_FORMAT:
                    rValue.Value >>= nChapterFormat;
                    bChapterFormatOK = true;
                    break;
//---> i53420
                case TOK_TPARAM_CHAPTER_LEVEL:
                    rValue.Value >>= nLevel;
                    bLevelOK = true;
                    break;
                case TOK_TPARAM_BIBLIOGRAPHY_DATA:
                    rValue.Value >>= nBibliographyData;
                    bBibliographyDataOK = true;
                    break;
            }
        }
    }

    // convert type to token (and check validity) ...
    XMLTokenEnum eElement(XML_TOKEN_INVALID);
    sal_uInt16 nNamespace(XML_NAMESPACE_TEXT);
    switch(nTokenType)
    {
        case TOK_TTYPE_ENTRY_TEXT:
            eElement = XML_INDEX_ENTRY_TEXT;
            break;
        case TOK_TTYPE_TAB_STOP:
            // test validity
            if ( bRightAligned || bTabPositionOK || bFillCharOK )
            {
                eElement = XML_INDEX_ENTRY_TAB_STOP;
            }
            break;
        case TOK_TTYPE_TEXT:
            // test validity
            if (bTextOK)
            {
                eElement = XML_INDEX_ENTRY_SPAN;
            }
            break;
        case TOK_TTYPE_PAGE_NUMBER:
            eElement = XML_INDEX_ENTRY_PAGE_NUMBER;
            break;
        case TOK_TTYPE_CHAPTER_INFO:    // keyword index
            eElement = XML_INDEX_ENTRY_CHAPTER;
            break;
        case TOK_TTYPE_ENTRY_NUMBER:    // table of content
            eElement = XML_INDEX_ENTRY_CHAPTER;
            break;
        case TOK_TTYPE_HYPERLINK_START:
            eElement = XML_INDEX_ENTRY_LINK_START;
            break;
        case TOK_TTYPE_HYPERLINK_END:
            eElement = XML_INDEX_ENTRY_LINK_END;
            break;
        case TOK_TTYPE_BIBLIOGRAPHY:
            if (bBibliographyDataOK)
            {
                eElement = XML_INDEX_ENTRY_BIBLIOGRAPHY;
            }
            break;
        default:
            ; // unknown/unimplemented template
            break;
    }

    if (eType != TEXT_SECTION_TYPE_TOC)
    {
        switch (nTokenType)
        {
            case TOK_TTYPE_HYPERLINK_START:
            case TOK_TTYPE_HYPERLINK_END:
                if (SvtSaveOptions::ODFSVER_012 < aODFVersion)
                {
                    assert(eType == TEXT_SECTION_TYPE_ILLUSTRATION
                        || eType == TEXT_SECTION_TYPE_OBJECT
                        || eType == TEXT_SECTION_TYPE_TABLE
                        || eType == TEXT_SECTION_TYPE_USER);
                    // ODF 1.3 OFFICE-3941
                    nNamespace = (SvtSaveOptions::ODFSVER_013 <= aODFVersion)
                        ? XML_NAMESPACE_TEXT
                        : XML_NAMESPACE_LO_EXT;
                }
                else
                {
                    eElement = XML_TOKEN_INVALID; // not allowed in ODF <= 1.2
                }
                break;
            default:
                break;
        }
    }

    //--->i90246
    //check the ODF version being exported
    if (aODFVersion == SvtSaveOptions::ODFSVER_011
        || aODFVersion == SvtSaveOptions::ODFSVER_010)
    {
        bLevelOK = false;
        if (TOK_TTYPE_CHAPTER_INFO == nTokenType)
        {
            //if we are emitting for ODF 1.1 or 1.0, this information can be used for alphabetical index only
            //it's not permitted in other indexes
            if (eType != TEXT_SECTION_TYPE_ALPHABETICAL)
            {
                eElement = XML_TOKEN_INVALID; //not permitted, invalidate the element
            }
            else //maps format for 1.1 & 1.0
            {
                // a few word here: OOo up to 2.4 uses the field chapter info in Alphabetical index
                // in a way different from the ODF 1.1/1.0 specification:

                // ODF1.1/1.0         OOo display in chapter info                       ODF1.2
                //                    (used in alphabetical index only

                // number             chapter number without pre/postfix                plain-number
                // number-and-name    chapter number without pre/postfix plus title     plain-number-and-name

                // with issue i89791 the reading of ODF 1.1 and 1.0 was corrected
                // this one corrects the writing back from ODF 1.2 to ODF 1.1/1.0
                // unfortunately if there is another application which interprets correctly ODF1.1/1.0,
                // the resulting alphabetical index will be rendered wrong by OOo 2.4 version

                switch( nChapterFormat )
                {
                case ChapterFormat::DIGIT:
                    nChapterFormat = ChapterFormat::NUMBER;
                    break;
                case ChapterFormat::NO_PREFIX_SUFFIX:
                    nChapterFormat = ChapterFormat::NAME_NUMBER;
                    break;
                }
            }
        }
        else if (TOK_TTYPE_ENTRY_NUMBER == nTokenType)
        {
            //in case of ODF 1.1 or 1.0 the only allowed number format is "number"
            //so, force it...
            // The only expected 'foreign' nChapterFormat is
            // ' ChapterFormat::DIGIT', forced to 'none, since the
            // 'value allowed in ODF 1.1 and 1.0 is 'number' the default
            // this can be obtained by simply disabling the chapter format
            bChapterFormatOK = false;
        }
    }

    // ... and write Element
    if (eElement == XML_TOKEN_INVALID)
        return;

    // character style (for most templates)
    if (bCharStyleOK)
    {
        switch (nTokenType)
        {
            case TOK_TTYPE_ENTRY_TEXT:
            case TOK_TTYPE_TEXT:
            case TOK_TTYPE_PAGE_NUMBER:
            case TOK_TTYPE_ENTRY_NUMBER:
            case TOK_TTYPE_HYPERLINK_START:
            case TOK_TTYPE_HYPERLINK_END:
            case TOK_TTYPE_BIBLIOGRAPHY:
            case TOK_TTYPE_CHAPTER_INFO:
            case TOK_TTYPE_TAB_STOP:
                GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                         XML_STYLE_NAME,
                             GetExport().EncodeStyleName( sCharStyle) );
                break;
            default:
                ; // nothing: no character style
                break;
        }
    }

    // tab properties
    if (TOK_TTYPE_TAB_STOP == nTokenType)
    {
        // tab type
        GetExport().AddAttribute(XML_NAMESPACE_STYLE, XML_TYPE,
                                 bRightAligned ? XML_RIGHT : XML_LEFT);

        if (bTabPositionOK && (! bRightAligned))
        {
            // position for left tabs (convert to measure)
            OUStringBuffer sBuf;
            GetExport().GetMM100UnitConverter().convertMeasureToXML(sBuf,
                                                             nTabPosition);
            GetExport().AddAttribute(XML_NAMESPACE_STYLE,
                                     XML_POSITION,
                                     sBuf.makeStringAndClear());
        }

        // fill char ("leader char")
        if (bFillCharOK && !sFillChar.isEmpty())
        {
            GetExport().AddAttribute(XML_NAMESPACE_STYLE,
                                     XML_LEADER_CHAR, sFillChar);
        }

        // #i21237#
        if (bWithTabStopOK && ! bWithTabStop)
        {
               GetExport().AddAttribute(XML_NAMESPACE_STYLE,
                                     XML_WITH_TAB,
                                     XML_FALSE);
        }
    }

    // bibliography data
    if (TOK_TTYPE_BIBLIOGRAPHY == nTokenType)
    {
        OSL_ENSURE(bBibliographyDataOK, "need bibl data");
        OUStringBuffer sBuf;
        if (SvXMLUnitConverter::convertEnum( sBuf, nBibliographyData,
                                             aBibliographyDataFieldMap ) )
        {
            GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                     XML_BIBLIOGRAPHY_DATA_FIELD,
                                     sBuf.makeStringAndClear());
        }
    }

    // chapter info
    if (TOK_TTYPE_CHAPTER_INFO == nTokenType)
    {
        OSL_ENSURE(bChapterFormatOK, "need chapter info");
        GetExport().AddAttribute(
            XML_NAMESPACE_TEXT, XML_DISPLAY,
            XMLTextFieldExport::MapChapterDisplayFormat(nChapterFormat));
//---> i53420
        if (bLevelOK)
            GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_OUTLINE_LEVEL,
                                 OUString::number(nLevel));
    }

//--->i53420
    if (TOK_TTYPE_ENTRY_NUMBER == nTokenType)
    {
        if (bChapterFormatOK)
            GetExport().AddAttribute(
                XML_NAMESPACE_TEXT, XML_DISPLAY,
                XMLTextFieldExport::MapChapterDisplayFormat(nChapterFormat));

        if (bLevelOK)
            GetExport().AddAttribute(XML_NAMESPACE_TEXT, XML_OUTLINE_LEVEL,
                                 OUString::number(nLevel));
    }
    // export template
    SvXMLElementExport aTemplateElement(GetExport(), nNamespace,
                                        GetXMLToken(eElement),
                                        true, false)
        ;

    // entry text or span element: write text
    if (TOK_TTYPE_TEXT == nTokenType)
    {
        GetExport().Characters(sText);
    }
}

void XMLSectionExport::ExportLevelParagraphStyles(
    Reference<XIndexReplace> const & xLevelParagraphStyles)
{
    // iterate over levels
    sal_Int32 nPLevelCount = xLevelParagraphStyles->getCount();
    for(sal_Int32 nLevel = 0; nLevel < nPLevelCount; nLevel++)
    {
        Any aAny = xLevelParagraphStyles->getByIndex(nLevel);
        Sequence<OUString> aStyleNames;
        aAny >>= aStyleNames;

        // export only if at least one style is contained
        if (aStyleNames.hasElements())
        {
            // level attribute; we count 1..10; API 0..9
            sal_Int32 nLevelPlusOne = nLevel + 1;
            GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                     XML_OUTLINE_LEVEL,
                                     OUString::number(nLevelPlusOne));

            // source styles element
            SvXMLElementExport aParaStyles(GetExport(),
                                           XML_NAMESPACE_TEXT,
                                           XML_INDEX_SOURCE_STYLES,
                                           true, true);

            // iterate over styles in this level
            for (const auto& rStyleName : aStyleNames)
            {
                // stylename attribute
                GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                         XML_STYLE_NAME,
                             GetExport().EncodeStyleName(rStyleName) );

                // element
                SvXMLElementExport aParaStyle(GetExport(),
                                              XML_NAMESPACE_TEXT,
                                              XML_INDEX_SOURCE_STYLE,
                                              true, false);
            }
        }
    }
}

void XMLSectionExport::ExportBoolean(
    const Reference<XPropertySet> & rPropSet,
    const OUString& sPropertyName,
    enum XMLTokenEnum eAttributeName,
    bool bDefault,
    bool bInvert)
{
    OSL_ENSURE(eAttributeName != XML_TOKEN_INVALID, "Need attribute name");

    Any aAny = rPropSet->getPropertyValue(sPropertyName);
    bool bTmp = *o3tl::doAccess<bool>(aAny);

    // value = value ^ bInvert
    // omit if value == default
    if ( (bTmp != bInvert) != bDefault )
    {
        // export non-default value (since default is omitted)
        GetExport().AddAttribute(XML_NAMESPACE_TEXT,
                                 eAttributeName,
                                 bDefault ? XML_FALSE : XML_TRUE);
    }
}

void XMLSectionExport::ExportBibliographyConfiguration(SvXMLExport& rExport)
{
    // first: get field master (via text field supplier)
    Reference<XTextFieldsSupplier> xTextFieldsSupp( rExport.GetModel(),
                                                    UNO_QUERY );
    if ( !xTextFieldsSupp.is() )
        return;

    static constexpr OUString sFieldMaster_Bibliography(u"com.sun.star.text.FieldMaster.Bibliography"_ustr);

    // get bibliography field master
    Reference<XNameAccess> xMasters =
        xTextFieldsSupp->getTextFieldMasters();
    if ( !xMasters->hasByName(sFieldMaster_Bibliography) )
        return;

    Any aAny =
        xMasters->getByName(sFieldMaster_Bibliography);
    Reference<XPropertySet> xPropSet;
    aAny >>= xPropSet;

    OSL_ENSURE( xPropSet.is(), "field master must have XPropSet" );

    OUString sTmp;

    aAny = xPropSet->getPropertyValue(u"BracketBefore"_ustr);
    aAny >>= sTmp;
    rExport.AddAttribute(XML_NAMESPACE_TEXT, XML_PREFIX, sTmp);

    aAny = xPropSet->getPropertyValue(u"BracketAfter"_ustr);
    aAny >>= sTmp;
    rExport.AddAttribute(XML_NAMESPACE_TEXT, XML_SUFFIX, sTmp);

    aAny = xPropSet->getPropertyValue(u"IsNumberEntries"_ustr);
    if (*o3tl::doAccess<bool>(aAny))
    {
        rExport.AddAttribute(XML_NAMESPACE_TEXT,
                             XML_NUMBERED_ENTRIES, XML_TRUE);
    }

    aAny = xPropSet->getPropertyValue(u"IsSortByPosition"_ustr);
    if (! *o3tl::doAccess<bool>(aAny))
    {
        rExport.AddAttribute(XML_NAMESPACE_TEXT,
                             XML_SORT_BY_POSITION, XML_FALSE);
    }

    // sort algorithm
    aAny = xPropSet->getPropertyValue(u"SortAlgorithm"_ustr);
    OUString sAlgorithm;
    aAny >>= sAlgorithm;
    if( !sAlgorithm.isEmpty() )
    {
        rExport.AddAttribute( XML_NAMESPACE_TEXT,
                              XML_SORT_ALGORITHM, sAlgorithm );
    }

    // locale
    aAny = xPropSet->getPropertyValue(u"Locale"_ustr);
    Locale aLocale;
    aAny >>= aLocale;
    rExport.AddLanguageTagAttributes( XML_NAMESPACE_FO, XML_NAMESPACE_STYLE, aLocale, true);

    // configuration element
    SvXMLElementExport aElement(rExport, XML_NAMESPACE_TEXT,
                                XML_BIBLIOGRAPHY_CONFIGURATION,
                                true, true);

    // sort keys
    aAny = xPropSet->getPropertyValue(u"SortKeys"_ustr);
    Sequence<Sequence<PropertyValue> > aKeys;
    aAny >>= aKeys;
    for (const Sequence<PropertyValue>& rKey : aKeys)
    {
        for(const PropertyValue& rValue : rKey)
        {
            if (rValue.Name == "SortKey")
            {
                sal_Int16 nKey = 0;
                rValue.Value >>= nKey;
                OUStringBuffer sBuf;
                if (SvXMLUnitConverter::convertEnum( sBuf, nKey,
                                         aBibliographyDataFieldMap ) )
                {
                    rExport.AddAttribute(XML_NAMESPACE_TEXT, XML_KEY,
                                         sBuf.makeStringAndClear());
                }
            }
            else if (rValue.Name == "IsSortAscending")
            {
                bool bTmp = *o3tl::doAccess<bool>(rValue.Value);
                rExport.AddAttribute(XML_NAMESPACE_TEXT,
                                     XML_SORT_ASCENDING,
                                     bTmp ? XML_TRUE : XML_FALSE);
            }
        }

        SvXMLElementExport aKeyElem(rExport,
                                    XML_NAMESPACE_TEXT, XML_SORT_KEY,
                                    true, true);
    }
}


bool XMLSectionExport::IsMuteSection(
    const Reference<XTextSection> & rSection) const
{
    bool bRet = false;

    // a section is mute if
    // 1) it exists
    // 2) the SaveLinkedSections flag (at the export) is false
    // 3) the IsGlobalDocumentSection property is true
    // 4) it is not an Index

    if ( (!rExport.IsSaveLinkedSections()) && rSection.is() )
    {
        // walk the section chain and set bRet if any is linked
        for(Reference<XTextSection> aSection(rSection);
            aSection.is();
            aSection = aSection->getParentSection())
        {
            // check if it is a global document section (linked or index)
            Reference<XPropertySet> xPropSet(aSection, UNO_QUERY);
            if (xPropSet.is())
            {
                Any aAny = xPropSet->getPropertyValue(u"IsGlobalDocumentSection"_ustr);

                if ( *o3tl::doAccess<bool>(aAny) )
                {
                    Reference<XDocumentIndex> xIndex;
                    if (! GetIndex(rSection, xIndex))
                    {
                        bRet = true;

                        // early out if result is known
                        break;
                    }
                }
            }
            // section has no properties: ignore
        }
    }
    // else: no section, or always save sections: default (false)

    return bRet;
}

bool XMLSectionExport::IsMuteSection(
    const Reference<XTextContent> & rSection,
    bool bDefault) const
{
    // default: like default argument
    bool bRet = bDefault;

    Reference<XPropertySet> xPropSet(rSection->getAnchor(), UNO_QUERY);
    if (xPropSet.is())
    {
        if (xPropSet->getPropertySetInfo()->hasPropertyByName(u"TextSection"_ustr))
        {
            Any aAny = xPropSet->getPropertyValue(u"TextSection"_ustr);
            Reference<XTextSection> xSection;
            aAny >>= xSection;

            bRet = IsMuteSection(xSection);
        }
        // else: return default
    }
    // else: return default

    return bRet;
}

bool XMLSectionExport::IsInSection(
    const Reference<XTextSection> & rEnclosingSection,
    const Reference<XTextContent> & rContent,
    bool bDefault)
{
    // default: like default argument
    bool bRet = bDefault;
    OSL_ENSURE(rEnclosingSection.is(), "enclosing section expected");

    Reference<XPropertySet> xPropSet(rContent, UNO_QUERY);
    if (xPropSet.is())
    {
        if (xPropSet->getPropertySetInfo()->hasPropertyByName(u"TextSection"_ustr))
        {
            Any aAny = xPropSet->getPropertyValue(u"TextSection"_ustr);
            Reference<XTextSection> xSection;
            aAny >>= xSection;

            // now walk chain of text sections (if we have one)
            if (xSection.is())
            {
                do
                {
                    bRet = (rEnclosingSection == xSection);
                    xSection = xSection->getParentSection();
                }
                while (!bRet && xSection.is());
            }
            else
                bRet = false;   // no section -> can't be inside
        }
        // else: no TextSection property -> return default
    }
    // else: no XPropertySet -> return default

    return bRet;
}


void XMLSectionExport::ExportMasterDocHeadingDummies()
{
    if( bHeadingDummiesExported )
        return;

    Reference< XChapterNumberingSupplier > xCNSupplier( rExport.GetModel(),
                                                        UNO_QUERY );

    Reference< XIndexReplace > xChapterNumbering;
    if( xCNSupplier.is() )
        xChapterNumbering = xCNSupplier->getChapterNumberingRules();

    if( !xChapterNumbering.is() )
        return;

    sal_Int32 nCount = xChapterNumbering->getCount();
    for( sal_Int32 nLevel = 0; nLevel < nCount; nLevel++ )
    {
        OUString sStyle;
        Sequence<PropertyValue> aProperties;
        xChapterNumbering->getByIndex( nLevel ) >>= aProperties;
        auto pProp = std::find_if(std::cbegin(aProperties), std::cend(aProperties),
            [](const PropertyValue& rProp) { return rProp.Name == "HeadingStyleName"; });
        if (pProp != std::cend(aProperties))
            pProp->Value >>= sStyle;

        if( !sStyle.isEmpty() )
        {
            GetExport().AddAttribute( XML_NAMESPACE_TEXT, XML_STYLE_NAME,
                                      GetExport().EncodeStyleName( sStyle ) );

            GetExport().AddAttribute( XML_NAMESPACE_TEXT, XML_LEVEL,
                                        OUString::number( nLevel + 1 ) );
            SvXMLElementExport aElem( GetExport(), XML_NAMESPACE_TEXT, XML_H,
                                        true, false );
        }
    }

    bHeadingDummiesExported  = true;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
