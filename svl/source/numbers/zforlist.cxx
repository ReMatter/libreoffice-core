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

#include <sal/log.hxx>
#include <officecfg/Office/Common.hxx>
#include <svl/zforlist.hxx>
#include <svl/currencytable.hxx>

#include <comphelper/lok.hxx>
#include <comphelper/string.hxx>
#include <o3tl/string_view.hxx>
#include <tools/debug.hxx>
#include <unotools/charclass.hxx>
#include <unotools/configmgr.hxx>
#include <i18nlangtag/mslangid.hxx>
#include <unotools/localedatawrapper.hxx>
#include <com/sun/star/i18n/KNumberFormatUsage.hpp>
#include <com/sun/star/i18n/KNumberFormatType.hpp>
#include <com/sun/star/i18n/FormatElement.hpp>
#include <com/sun/star/i18n/Currency2.hpp>
#include <com/sun/star/i18n/NumberFormatCode.hpp>
#include <com/sun/star/i18n/XNumberFormatCode.hpp>
#include <com/sun/star/i18n/NumberFormatMapper.hpp>
#include <comphelper/processfactory.hxx>

#include <osl/mutex.hxx>

#include "zforscan.hxx"
#include "zforfind.hxx"
#include <svl/zformat.hxx>
#include <i18npool/reservedconstants.hxx>

#include <unotools/syslocaleoptions.hxx>
#include <unotools/digitgroupingiterator.hxx>
#include <rtl/strbuf.hxx>
#include <rtl/math.hxx>

#include <atomic>
#include <limits>
#include <memory>
#include <set>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::i18n;
using namespace ::com::sun::star::lang;

// Constants for type offsets per Country/Language (CL)
#define ZF_STANDARD              0
#define ZF_STANDARD_PERCENT     10
#define ZF_STANDARD_CURRENCY    20
#define ZF_STANDARD_DATE        30
#define ZF_STANDARD_TIME        60
#define ZF_STANDARD_DURATION    (ZF_STANDARD_TIME + 4)
#define ZF_STANDARD_DATETIME    70
#define ZF_STANDARD_SCIENTIFIC  80
#define ZF_STANDARD_FRACTION    85

// Additional builtin formats, historically not fitting into the first 10 of a
// category. Make sure it doesn't spill over to the next category.
#define ZF_STANDARD_DATE_SYS_DMMMYYYY       (ZF_STANDARD_DATE + 10)
#define ZF_STANDARD_DATE_SYS_DMMMMYYYY      (ZF_STANDARD_DATE + 11)
#define ZF_STANDARD_DATE_SYS_NNDMMMYY       (ZF_STANDARD_DATE + 12)
#define ZF_STANDARD_DATE_SYS_NNDMMMMYYYY    (ZF_STANDARD_DATE + 13)
#define ZF_STANDARD_DATE_SYS_NNNNDMMMMYYYY  (ZF_STANDARD_DATE + 14)
#define ZF_STANDARD_DATE_DIN_DMMMYYYY       (ZF_STANDARD_DATE + 15)
#define ZF_STANDARD_DATE_DIN_DMMMMYYYY      (ZF_STANDARD_DATE + 16)
#define ZF_STANDARD_DATE_DIN_MMDD           (ZF_STANDARD_DATE + 17)
#define ZF_STANDARD_DATE_DIN_YYMMDD         (ZF_STANDARD_DATE + 18)
#define ZF_STANDARD_DATE_DIN_YYYYMMDD       (ZF_STANDARD_DATE + 19)
#define ZF_STANDARD_DATE_WW                 (ZF_STANDARD_DATE + 20)

#define ZF_STANDARD_LOGICAL     SV_MAX_COUNT_STANDARD_FORMATS-1 //  99
#define ZF_STANDARD_TEXT        SV_MAX_COUNT_STANDARD_FORMATS   // 100

static_assert( ZF_STANDARD_TEXT == NF_STANDARD_FORMAT_TEXT, "definition mismatch" );

static_assert( NF_INDEX_TABLE_RESERVED_START == i18npool::nStopPredefinedFormatIndex,
        "NfIndexTableOffset does not match i18npool's locale data predefined format code index bounds.");

static_assert( NF_INDEX_TABLE_ENTRIES <= i18npool::nFirstFreeFormatIndex,
        "NfIndexTableOffset crosses i18npool's locale data reserved format code index bounds.\n"
        "You will need to adapt all locale data files defining index values "
        "(formatIndex=\"...\") in that range and increment those and when done "
        "adjust nFirstFreeFormatIndex in include/i18npool/reservedconstants.hxx");

/* Locale that is set if an unknown locale (from another system) is loaded of
 * legacy documents. Can not be SYSTEM because else, for example, a German "DM"
 * (old currency) is recognized as a date (#53155#). */
#define UNKNOWN_SUBSTITUTE      LANGUAGE_ENGLISH_US

// Same order as in include/svl/zforlist.hxx enum NfIndexTableOffset
sal_uInt32 const indexTable[NF_INDEX_TABLE_ENTRIES] = {
    ZF_STANDARD, // NF_NUMBER_STANDARD
    ZF_STANDARD + 1, // NF_NUMBER_INT
    ZF_STANDARD + 2, // NF_NUMBER_DEC2
    ZF_STANDARD + 3, // NF_NUMBER_1000INT
    ZF_STANDARD + 4, // NF_NUMBER_1000DEC2
    ZF_STANDARD + 5, // NF_NUMBER_SYSTEM
    ZF_STANDARD_SCIENTIFIC, // NF_SCIENTIFIC_000E000
    ZF_STANDARD_SCIENTIFIC + 1, // NF_SCIENTIFIC_000E00
    ZF_STANDARD_PERCENT, // NF_PERCENT_INT
    ZF_STANDARD_PERCENT + 1, // NF_PERCENT_DEC2
    ZF_STANDARD_FRACTION, // NF_FRACTION_1D
    ZF_STANDARD_FRACTION + 1, // NF_FRACTION_2D
    ZF_STANDARD_CURRENCY, // NF_CURRENCY_1000INT
    ZF_STANDARD_CURRENCY + 1, // NF_CURRENCY_1000DEC2
    ZF_STANDARD_CURRENCY + 2, // NF_CURRENCY_1000INT_RED
    ZF_STANDARD_CURRENCY + 3, // NF_CURRENCY_1000DEC2_RED
    ZF_STANDARD_CURRENCY + 4, // NF_CURRENCY_1000DEC2_CCC
    ZF_STANDARD_CURRENCY + 5, // NF_CURRENCY_1000DEC2_DASHED
    ZF_STANDARD_DATE, // NF_DATE_SYSTEM_SHORT
    ZF_STANDARD_DATE + 8, // NF_DATE_SYSTEM_LONG
    ZF_STANDARD_DATE + 7, // NF_DATE_SYS_DDMMYY
    ZF_STANDARD_DATE + 6, // NF_DATE_SYS_DDMMYYYY
    ZF_STANDARD_DATE + 9, // NF_DATE_SYS_DMMMYY
    ZF_STANDARD_DATE_SYS_DMMMYYYY, // NF_DATE_SYS_DMMMYYYY
    ZF_STANDARD_DATE_DIN_DMMMYYYY, // NF_DATE_DIN_DMMMYYYY
    ZF_STANDARD_DATE_SYS_DMMMMYYYY, // NF_DATE_SYS_DMMMMYYYY
    ZF_STANDARD_DATE_DIN_DMMMMYYYY, // NF_DATE_DIN_DMMMMYYYY
    ZF_STANDARD_DATE_SYS_NNDMMMYY, // NF_DATE_SYS_NNDMMMYY
    ZF_STANDARD_DATE + 1, // NF_DATE_DEF_NNDDMMMYY
    ZF_STANDARD_DATE_SYS_NNDMMMMYYYY, // NF_DATE_SYS_NNDMMMMYYYY
    ZF_STANDARD_DATE_SYS_NNNNDMMMMYYYY, // NF_DATE_SYS_NNNNDMMMMYYYY
    ZF_STANDARD_DATE_DIN_MMDD, // NF_DATE_DIN_MMDD
    ZF_STANDARD_DATE_DIN_YYMMDD, // NF_DATE_DIN_YYMMDD
    ZF_STANDARD_DATE_DIN_YYYYMMDD, // NF_DATE_DIN_YYYYMMDD
    ZF_STANDARD_DATE + 2, // NF_DATE_SYS_MMYY
    ZF_STANDARD_DATE + 3, // NF_DATE_SYS_DDMMM
    ZF_STANDARD_DATE + 4, // NF_DATE_MMMM
    ZF_STANDARD_DATE + 5, // NF_DATE_QQJJ
    ZF_STANDARD_DATE_WW, // NF_DATE_WW
    ZF_STANDARD_TIME, // NF_TIME_HHMM
    ZF_STANDARD_TIME + 1, // NF_TIME_HHMMSS
    ZF_STANDARD_TIME + 2, // NF_TIME_HHMMAMPM
    ZF_STANDARD_TIME + 3, // NF_TIME_HHMMSSAMPM
    ZF_STANDARD_TIME + 4, // NF_TIME_HH_MMSS
    ZF_STANDARD_TIME + 5, // NF_TIME_MMSS00
    ZF_STANDARD_TIME + 6, // NF_TIME_HH_MMSS00
    ZF_STANDARD_DATETIME, // NF_DATETIME_SYSTEM_SHORT_HHMM
    ZF_STANDARD_DATETIME + 1, // NF_DATETIME_SYS_DDMMYYYY_HHMMSS
    ZF_STANDARD_LOGICAL, // NF_BOOLEAN
    ZF_STANDARD_TEXT, // NF_TEXT
    ZF_STANDARD_DATETIME + 2, // NF_DATETIME_SYS_DDMMYYYY_HHMM
    ZF_STANDARD_FRACTION + 2, // NF_FRACTION_3D
    ZF_STANDARD_FRACTION + 3, // NF_FRACTION_2
    ZF_STANDARD_FRACTION + 4, // NF_FRACTION_4
    ZF_STANDARD_FRACTION + 5, // NF_FRACTION_8
    ZF_STANDARD_FRACTION + 6, // NF_FRACTION_16
    ZF_STANDARD_FRACTION + 7, // NF_FRACTION_10
    ZF_STANDARD_FRACTION + 8, // NF_FRACTION_100
    ZF_STANDARD_DATETIME + 3, // NF_DATETIME_ISO_YYYYMMDD_HHMMSS
    ZF_STANDARD_DATETIME + 4, // NF_DATETIME_ISO_YYYYMMDD_HHMMSS000
    ZF_STANDARD_DATETIME + 5, // NF_DATETIME_ISO_YYYYMMDDTHHMMSS
    ZF_STANDARD_DATETIME + 6  // NF_DATETIME_ISO_YYYYMMDDTHHMMSS000
};

/**
    instead of every number formatter being a listener we have a registry which
    also handles one instance of the SysLocale options
 */

class SvNumberFormatterRegistry_Impl : public utl::ConfigurationListener
{
    std::vector< SvNumberFormatter* >
                                aFormatters;
    SvtSysLocaleOptions         aSysLocaleOptions;
    LanguageType                eSysLanguage;

public:
                            SvNumberFormatterRegistry_Impl();
    virtual                 ~SvNumberFormatterRegistry_Impl() override;

    void                    Insert( SvNumberFormatter* pThis )
                                { aFormatters.push_back( pThis ); }

    void                    Remove( SvNumberFormatter const * pThis );

    size_t                  Count() const
                                { return aFormatters.size(); }

    virtual void            ConfigurationChanged( utl::ConfigurationBroadcaster*, ConfigurationHints ) override;
};

SvNumberFormatterRegistry_Impl::SvNumberFormatterRegistry_Impl()
    : eSysLanguage(MsLangId::getRealLanguage( LANGUAGE_SYSTEM ))
{
    aSysLocaleOptions.AddListener( this );
}


SvNumberFormatterRegistry_Impl::~SvNumberFormatterRegistry_Impl()
{
    aSysLocaleOptions.RemoveListener( this );
}


void SvNumberFormatterRegistry_Impl::Remove( SvNumberFormatter const * pThis )
{
    auto it = std::find(aFormatters.begin(), aFormatters.end(), pThis);
    if (it != aFormatters.end())
        aFormatters.erase( it );
}

void SvNumberFormatterRegistry_Impl::ConfigurationChanged( utl::ConfigurationBroadcaster*,
                                                           ConfigurationHints nHint)
{
    ::osl::MutexGuard aGuard( SvNumberFormatter::GetGlobalMutex() );

    if ( nHint & ConfigurationHints::Locale )
    {
        for(SvNumberFormatter* pFormatter : aFormatters)
            pFormatter->ReplaceSystemCL( eSysLanguage );
        eSysLanguage = MsLangId::getRealLanguage( LANGUAGE_SYSTEM );
    }
    if ( nHint & ConfigurationHints::Currency )
    {
        for(SvNumberFormatter* pFormatter : aFormatters)
            pFormatter->ResetDefaultSystemCurrency();
    }
    if ( nHint & ConfigurationHints::DatePatterns )
    {
        for(SvNumberFormatter* pFormatter : aFormatters)
            pFormatter->InvalidateDateAcceptancePatterns();
    }
}

static std::atomic<bool> g_CurrencyTableInitialized;

SvNumberFormatterRegistry_Impl* SvNumberFormatter::pFormatterRegistry = nullptr;
namespace
{
    NfCurrencyTable& theCurrencyTable()
    {
        static NfCurrencyTable SINGLETON;
        return SINGLETON;
    }

    NfCurrencyTable& theLegacyOnlyCurrencyTable()
    {
        static NfCurrencyTable SINGLETON;
        return SINGLETON;
    }

    /** THE set of installed locales. */
    std::set< LanguageType > theInstalledLocales;

}
sal_uInt16 SvNumberFormatter::nSystemCurrencyPosition = 0;

// Whether BankSymbol (not CurrencySymbol!) is always at the end (1 $;-1 $) or
// language dependent.
#define NF_BANKSYMBOL_FIX_POSITION 1

const sal_uInt16 SvNumberFormatter::UNLIMITED_PRECISION   = ::std::numeric_limits<sal_uInt16>::max();
const sal_uInt16 SvNumberFormatter::INPUTSTRING_PRECISION = ::std::numeric_limits<sal_uInt16>::max()-1;

void SvNFEngine::ChangeIntl(SvNFLanguageData& rCurrentLanguage, LanguageType eLnge)
{
    rCurrentLanguage.ChangeIntl(eLnge);
}

void SvNFEngine::ChangeNullDate(const SvNFLanguageData& rCurrentLanguage, sal_uInt16 nDay, sal_uInt16 nMonth, sal_Int16 nYear)
{
    rCurrentLanguage.pFormatScanner->ChangeNullDate(nDay, nMonth, nYear);
    rCurrentLanguage.pStringScanner->ChangeNullDate(nDay, nMonth, nYear);
}

SvNFLanguageData::SvNFLanguageData(const Reference<XComponentContext>& rxContext, LanguageType eLang,
                                   const SvNumberFormatter& rColorCallback)
    : xContext(rxContext)
    , IniLnge(eLang)
    , ActLnge(eLang)
    , aLanguageTag(eLang)
    , eEvalDateFormat(NfEvalDateFormat::International)
{
    xCharClass.changeLocale(xContext, aLanguageTag);
    xLocaleData.init(aLanguageTag);
    xCalendar.init(xContext, aLanguageTag.getLocale());
    xTransliteration.init(xContext, ActLnge);

    // cached locale data items
    const LocaleDataWrapper* pLoc = GetLocaleData();
    aDecimalSep = pLoc->getNumDecimalSep();
    aDecimalSepAlt = pLoc->getNumDecimalSepAlt();
    aThousandSep = pLoc->getNumThousandSep();
    aDateSep = pLoc->getDateSep();

    pStringScanner.reset(new ImpSvNumberInputScan(*this));
    pFormatScanner.reset(new ImpSvNumberformatScan(*this, rColorCallback));
}

SvNFLanguageData::SvNFLanguageData(const SvNFLanguageData& rOther)
    : xContext(rOther.xContext)
    , IniLnge(rOther.IniLnge)
    , ActLnge(rOther.ActLnge)
    , aLanguageTag(rOther.aLanguageTag)
    , aDecimalSep(rOther.aDecimalSep)
    , aDecimalSepAlt(rOther.aDecimalSepAlt)
    , aThousandSep(rOther.aThousandSep)
    , aDateSep(rOther.aDateSep)
    , eEvalDateFormat(rOther.eEvalDateFormat)
{
    xCharClass.changeLocale(xContext, aLanguageTag);
    xLocaleData.init(aLanguageTag);
    xCalendar.init(xContext, aLanguageTag.getLocale());
    xTransliteration.init(xContext, ActLnge);

    pStringScanner.reset(new ImpSvNumberInputScan(*this));
    pFormatScanner.reset(new ImpSvNumberformatScan(*this, rOther.pFormatScanner->getColorCallback(),
                                                   rOther.pFormatScanner->GetNullDate()));
}

SvNFLanguageData::~SvNFLanguageData()
{
}

const LocaleDataWrapper* SvNFLanguageData::GetLocaleData() const { return xLocaleData.get(); }

const CharClass* SvNFLanguageData::GetCharClass() const { return xCharClass.get(); }

CalendarWrapper* SvNFLanguageData::GetCalendar() const { return xCalendar.get(); }

const ::utl::TransliterationWrapper* SvNFLanguageData::GetTransliteration() const
{
    return xTransliteration.get();
}

const LanguageTag& SvNFLanguageData::GetLanguageTag() const { return aLanguageTag; }

const ImpSvNumberformatScan* SvNFLanguageData::GetFormatScanner() const { return pFormatScanner.get(); }

const OUString& SvNFLanguageData::GetNumDecimalSep() const { return aDecimalSep; }

const OUString& SvNFLanguageData::GetNumDecimalSepAlt() const { return aDecimalSepAlt; }

const OUString& SvNFLanguageData::GetNumThousandSep() const { return aThousandSep; }

const OUString& SvNFLanguageData::GetDateSep() const { return aDateSep; }

bool SvNFLanguageData::IsDecimalSep( std::u16string_view rStr ) const
{
    if (rStr == GetNumDecimalSep())
        return true;
    if (GetNumDecimalSepAlt().isEmpty())
        return false;
    return rStr == GetNumDecimalSepAlt();
}

OUString SvNFLanguageData::GetLangDecimalSep( LanguageType nLang )
{
    if (nLang == ActLnge)
    {
        return GetNumDecimalSep();
    }
    OUString aRet;
    LanguageType eSaveLang = xLocaleData.getCurrentLanguage();
    if (nLang == eSaveLang)
    {
        aRet = xLocaleData->getNumDecimalSep();
    }
    else
    {
        LanguageTag aSaveLocale( xLocaleData->getLanguageTag() );
        xLocaleData.changeLocale( LanguageTag( nLang));
        aRet = xLocaleData->getNumDecimalSep();
        xLocaleData.changeLocale( aSaveLocale );
    }
    return aRet;
}

void SvNFLanguageData::ChangeIntl(LanguageType eLnge)
{
    if (ActLnge == eLnge)
        return;

    ActLnge = eLnge;

    aLanguageTag.reset( eLnge );
    xCharClass.changeLocale( xContext, aLanguageTag );
    xLocaleData.changeLocale( aLanguageTag );
    xCalendar.changeLocale( aLanguageTag.getLocale() );
    xTransliteration.changeLocale( eLnge );

    // cached locale data items, initialize BEFORE calling ChangeIntl below
    const LocaleDataWrapper* pLoc = GetLocaleData();
    aDecimalSep = pLoc->getNumDecimalSep();
    aDecimalSepAlt = pLoc->getNumDecimalSepAlt();
    aThousandSep = pLoc->getNumThousandSep();
    aDateSep = pLoc->getDateSep();

    pFormatScanner->ChangeIntl();
    pStringScanner->ChangeIntl();
}

SvNumberFormatter::SvNumberFormatter( const Reference< XComponentContext >& rxContext,
                                      LanguageType eLang )
    : m_xContext( rxContext )
    , IniLnge(eLang != LANGUAGE_DONTKNOW ? eLang : UNKNOWN_SUBSTITUTE)
    , m_aRWPolicy(SvNFEngine::GetRWPolicy(m_aFormatData))
    , m_aCurrentLanguage(rxContext, IniLnge, *this)
    , m_xNatNum(m_xContext)
{

    // 0 .. 999 for initialized language formats
    m_aFormatData.ImpGenerateFormats(m_aCurrentLanguage, GetNatNum(), 0, false);

    ::osl::MutexGuard aGuard( GetGlobalMutex() );
    GetFormatterRegistry().Insert( this );
}

SvNumberFormatter::~SvNumberFormatter()
{
    {
        ::osl::MutexGuard aGuard( GetGlobalMutex() );
        pFormatterRegistry->Remove( this );
        if ( !pFormatterRegistry->Count() )
        {
            delete pFormatterRegistry;
            pFormatterRegistry = nullptr;
        }
    }

    m_aFormatData.aFTable.clear();
    ClearMergeTable();
}

void SvNumberFormatter::ChangeIntl(LanguageType eLnge)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    SvNFEngine::ChangeIntl(m_aCurrentLanguage, eLnge);
}

// static
::osl::Mutex& SvNumberFormatter::GetGlobalMutex()
{
    // #i77768# Due to a static reference in the toolkit lib
    // we need a mutex that lives longer than the svl library.
    // Otherwise the dtor would use a destructed mutex!!
    static osl::Mutex* persistentMutex(new osl::Mutex);

    return *persistentMutex;
}


// static
SvNumberFormatterRegistry_Impl& SvNumberFormatter::GetFormatterRegistry()
{
    ::osl::MutexGuard aGuard( GetGlobalMutex() );
    if ( !pFormatterRegistry )
    {
        pFormatterRegistry = new SvNumberFormatterRegistry_Impl;
    }
    return *pFormatterRegistry;
}

void SvNumberFormatter::SetColorLink( const Link<sal_uInt16,Color*>& rColorTableCallBack )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    aColorLink = rColorTableCallBack;
}

Color* SvNumberFormatter::GetUserDefColor(sal_uInt16 nIndex) const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    if( aColorLink.IsSet() )
    {
        return aColorLink.Call(nIndex);
    }
    else
    {
        return nullptr;
    }
}

void SvNumberFormatter::ChangeNullDate(sal_uInt16 nDay,
                                       sal_uInt16 nMonth,
                                       sal_Int16 nYear)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    SvNFEngine::ChangeNullDate(m_aCurrentLanguage, nDay, nMonth, nYear);
}

const Date& SvNFLanguageData::GetNullDate() const
{
    return pFormatScanner->GetNullDate();
}

void SvNFLanguageData::ChangeStandardPrec(short nPrec)
{
    pFormatScanner->ChangeStandardPrec(nPrec);
}

const Date& SvNumberFormatter::GetNullDate() const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return m_aCurrentLanguage.GetNullDate();
}

void SvNumberFormatter::ChangeStandardPrec(short nPrec)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    m_aCurrentLanguage.ChangeStandardPrec(nPrec);
}

void SvNumberFormatter::SetNoZero(bool bNZ)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    m_aFormatData.SetNoZero(bNZ);
}

sal_uInt16 SvNumberFormatter::GetStandardPrec() const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return m_aCurrentLanguage.pFormatScanner->GetStandardPrec();
}

bool SvNumberFormatter::GetNoZero() const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return m_aFormatData.GetNoZero();
}

void SvNumberFormatter::ReplaceSystemCL( LanguageType eOldLanguage )
{
    sal_uInt32 nCLOffset = m_aFormatData.ImpGetCLOffset( LANGUAGE_SYSTEM );
    if ( nCLOffset > m_aFormatData.MaxCLOffset )
    {
        return ;    // no SYSTEM entries to replace
    }
    const sal_uInt32 nMaxBuiltin = nCLOffset + SV_MAX_COUNT_STANDARD_FORMATS;
    const sal_uInt32 nNextCL = nCLOffset + SV_COUNTRY_LANGUAGE_OFFSET;
    sal_uInt32 nKey;

    // remove old builtin formats
    auto it = m_aFormatData.aFTable.find( nCLOffset );
    while ( it != m_aFormatData.aFTable.end() && (nKey = it->first) >= nCLOffset && nKey <= nMaxBuiltin )
    {
        it = m_aFormatData.aFTable.erase(it);
    }

    // move additional and user defined to temporary table
    SvNumberFormatTable aOldTable;
    while ( it != m_aFormatData.aFTable.end() && (nKey = it->first) >= nCLOffset && nKey < nNextCL )
    {
        aOldTable[ nKey ] = it->second.release();
        it = m_aFormatData.aFTable.erase(it);
    }

    // generate new old builtin formats
    // reset m_aCurrentLanguage.ActLnge otherwise ChangeIntl() wouldn't switch if already LANGUAGE_SYSTEM
    m_aCurrentLanguage.ActLnge = LANGUAGE_DONTKNOW;
    ChangeIntl( LANGUAGE_SYSTEM );
    m_aFormatData.ImpGenerateFormats(m_aCurrentLanguage, GetNatNum(), nCLOffset, true);

    // convert additional and user defined from old system to new system
    SvNumberformat* pStdFormat = m_aFormatData.GetFormatEntry( nCLOffset + ZF_STANDARD );
    sal_uInt32 nLastKey = nMaxBuiltin;
    m_aCurrentLanguage.pFormatScanner->SetConvertMode( eOldLanguage, LANGUAGE_SYSTEM, true , true);
    while ( !aOldTable.empty() )
    {
        nKey = aOldTable.begin()->first;
        if ( nLastKey < nKey )
        {
            nLastKey = nKey;
        }
        std::unique_ptr<SvNumberformat> pOldEntry(aOldTable.begin()->second);
        aOldTable.erase( nKey );
        OUString aString( pOldEntry->GetFormatstring() );

        // Same as PutEntry() but assures key position even if format code is
        // a duplicate. Also won't mix up any LastInsertKey.
        ChangeIntl( eOldLanguage );
        LanguageType eLge = eOldLanguage;   // ConvertMode changes this
        bool bCheck = false;
        sal_Int32 nCheckPos = -1;
        std::unique_ptr<SvNumberformat> pNewEntry(new SvNumberformat( aString, m_aCurrentLanguage.pFormatScanner.get(),
                                                                      m_aCurrentLanguage.pStringScanner.get(), GetNatNum(), nCheckPos, eLge ));
        if ( nCheckPos == 0 )
        {
            SvNumFormatType eCheckType = pNewEntry->GetType();
            if ( eCheckType != SvNumFormatType::UNDEFINED )
            {
                pNewEntry->SetType( eCheckType | SvNumFormatType::DEFINED );
            }
            else
            {
                pNewEntry->SetType( SvNumFormatType::DEFINED );
            }

            if ( m_aFormatData.aFTable.emplace( nKey, std::move(pNewEntry) ).second )
            {
                bCheck = true;
            }
        }
        DBG_ASSERT( bCheck, "SvNumberFormatter::ReplaceSystemCL: couldn't convert" );
    }
    m_aCurrentLanguage.pFormatScanner->SetConvertMode(false);
    pStdFormat->SetLastInsertKey( sal_uInt16(nLastKey - nCLOffset), SvNumberformat::FormatterPrivateAccess() );

    // append new system additional formats
    css::uno::Reference< css::i18n::XNumberFormatCode > xNFC = i18n::NumberFormatMapper::create( m_xContext );
    ImpGenerateAdditionalFormats( nCLOffset, xNFC );
}

const NativeNumberWrapper& SvNumberFormatter::GetNatNum() const { return m_xNatNum.get(); }

bool SvNFFormatData::IsTextFormat(sal_uInt32 F_Index) const
{
    const SvNumberformat* pFormat = GetFormatEntry(F_Index);
    return pFormat && pFormat->IsTextFormat();
}

bool SvNumberFormatter::IsTextFormat(sal_uInt32 F_Index) const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return m_aFormatData.IsTextFormat(F_Index);
}

bool SvNFFormatData::PutEntry(SvNFLanguageData& rCurrentLanguage,
                                 const NativeNumberWrapper& rNatNum,
                                 OUString& rString,
                                 sal_Int32& nCheckPos,
                                 SvNumFormatType& nType,
                                 sal_uInt32& nKey,      // format key
                                 LanguageType eLnge,
                                 bool bReplaceBooleanEquivalent)
{
    nKey = 0;
    if (rString.isEmpty())                             // empty string
    {
        nCheckPos = 1;                                  // -> Error
        return false;
    }
    eLnge = rCurrentLanguage.ImpResolveLanguage(eLnge);
    rCurrentLanguage.ChangeIntl(eLnge);                 // change locale if necessary
    LanguageType eLge = eLnge;                          // non-const for ConvertMode
    bool bCheck = false;
    std::unique_ptr<SvNumberformat> p_Entry(new SvNumberformat(rString,
                                                               rCurrentLanguage.pFormatScanner.get(),
                                                               rCurrentLanguage.pStringScanner.get(),
                                                               rNatNum,
                                                               nCheckPos,
                                                               eLge,
                                                               bReplaceBooleanEquivalent));

    if (nCheckPos == 0)                         // Format ok
    {                                           // Type comparison:
        SvNumFormatType eCheckType = p_Entry->GetType();
        if ( eCheckType != SvNumFormatType::UNDEFINED)
        {
            p_Entry->SetType(eCheckType | SvNumFormatType::DEFINED);
            nType = eCheckType;
        }
        else
        {
            p_Entry->SetType(SvNumFormatType::DEFINED);
            nType = SvNumFormatType::DEFINED;
        }

        sal_uInt32 CLOffset = ImpGenerateCL(rCurrentLanguage, rNatNum, eLge);  // create new standard formats if necessary

        nKey = ImpIsEntry(p_Entry->GetFormatstring(),CLOffset, eLge);
        if (nKey == NUMBERFORMAT_ENTRY_NOT_FOUND) // only in not yet present
        {
            SvNumberformat* pStdFormat = GetFormatEntry(CLOffset + ZF_STANDARD);
            sal_uInt32 nPos = CLOffset + pStdFormat->GetLastInsertKey( SvNumberformat::FormatterPrivateAccess() );
            if (nPos+1 - CLOffset >= SV_COUNTRY_LANGUAGE_OFFSET)
            {
                SAL_WARN( "svl.numbers", "SvNumberFormatter::PutEntry: too many formats for CL");
            }
            else if (!aFTable.emplace( nPos+1, std::move(p_Entry)).second)
            {
                SAL_WARN( "svl.numbers", "SvNumberFormatter::PutEntry: dup position");
            }
            else
            {
                bCheck = true;
                nKey = nPos+1;
                pStdFormat->SetLastInsertKey(static_cast<sal_uInt16>(nKey-CLOffset), SvNumberformat::FormatterPrivateAccess());
            }
        }
    }
    return bCheck;
}

bool SvNumberFormatter::PutEntry(OUString& rString,
                                 sal_Int32& nCheckPos,
                                 SvNumFormatType& nType,
                                 sal_uInt32& nKey,      // format key
                                 LanguageType eLnge,
                                 bool bReplaceBooleanEquivalent)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return m_aFormatData.PutEntry(m_aCurrentLanguage, GetNatNum(), rString, nCheckPos, nType, nKey, eLnge, bReplaceBooleanEquivalent);
}

bool SvNumberFormatter::PutandConvertEntry(OUString& rString,
                                           sal_Int32& nCheckPos,
                                           SvNumFormatType& nType,
                                           sal_uInt32& nKey,
                                           LanguageType eLnge,
                                           LanguageType eNewLnge,
                                           bool bConvertDateOrder,
                                           bool bReplaceBooleanEquivalent )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    bool bRes;
    if (eNewLnge == LANGUAGE_DONTKNOW)
    {
        eNewLnge = IniLnge;
    }
    m_aCurrentLanguage.pFormatScanner->SetConvertMode(eLnge, eNewLnge, false, bConvertDateOrder);
    bRes = PutEntry(rString, nCheckPos, nType, nKey, eLnge, bReplaceBooleanEquivalent);
    m_aCurrentLanguage.pFormatScanner->SetConvertMode(false);

    if (bReplaceBooleanEquivalent && nCheckPos == 0 && nType == SvNumFormatType::DEFINED
            && nKey != NUMBERFORMAT_ENTRY_NOT_FOUND)
    {
        // The boolean string formats are always "user defined" without any
        // other type.
        const SvNumberformat* pEntry = m_aFormatData.GetFormatEntry(nKey);
        if (pEntry && pEntry->GetType() == SvNumFormatType::DEFINED)
        {
            // Replace boolean string format with Boolean in target locale, in
            // case the source strings are the target locale's.
            const OUString aSaveString = rString;
            ChangeIntl(eNewLnge);
            if (m_aCurrentLanguage.pFormatScanner->ReplaceBooleanEquivalent( rString))
            {
                const sal_Int32 nSaveCheckPos = nCheckPos;
                const SvNumFormatType nSaveType = nType;
                const sal_uInt32 nSaveKey = nKey;
                const bool bTargetRes = PutEntry(rString, nCheckPos, nType, nKey, eNewLnge, false);
                if (nCheckPos == 0 && nType == SvNumFormatType::LOGICAL && nKey != NUMBERFORMAT_ENTRY_NOT_FOUND)
                {
                    bRes = bTargetRes;
                }
                else
                {
                    SAL_WARN("svl.numbers", "SvNumberFormatter::PutandConvertEntry: can't scan boolean replacement");
                    // Live with the source boolean string format.
                    rString = aSaveString;
                    nCheckPos = nSaveCheckPos;
                    nType = nSaveType;
                    nKey = nSaveKey;
                }
            }
        }
    }
    return bRes;
}

bool SvNumberFormatter::PutandConvertEntrySystem(OUString& rString,
                                                 sal_Int32& nCheckPos,
                                                 SvNumFormatType& nType,
                                                 sal_uInt32& nKey,
                                                 LanguageType eLnge,
                                                 LanguageType eNewLnge)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    bool bRes;
    if (eNewLnge == LANGUAGE_DONTKNOW)
    {
        eNewLnge = IniLnge;
    }
    m_aCurrentLanguage.pFormatScanner->SetConvertMode(eLnge, eNewLnge, true, true);
    bRes = PutEntry(rString, nCheckPos, nType, nKey, eLnge);
    m_aCurrentLanguage.pFormatScanner->SetConvertMode(false);
    return bRes;
}

sal_uInt32 SvNumberFormatter::GetIndexPuttingAndConverting( OUString & rString, LanguageType eLnge,
                                                            LanguageType eSysLnge, SvNumFormatType & rType,
                                                            bool & rNewInserted, sal_Int32 & rCheckPos )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    sal_uInt32 nKey = NUMBERFORMAT_ENTRY_NOT_FOUND;
    rNewInserted = false;
    rCheckPos = 0;

    // #62389# empty format string (of Writer) => General standard format
    if (rString.isEmpty())
    {
        // nothing
    }
    else if (eLnge == LANGUAGE_SYSTEM && eSysLnge != SvtSysLocale().GetLanguageTag().getLanguageType())
    {
        sal_uInt32 nOrig = GetEntryKey( rString, eSysLnge );
        if (nOrig == NUMBERFORMAT_ENTRY_NOT_FOUND)
        {
            nKey = nOrig;   // none available, maybe user-defined
        }
        else
        {
            nKey = GetFormatForLanguageIfBuiltIn( nOrig, SvtSysLocale().GetLanguageTag().getLanguageType() );
        }
        if (nKey == nOrig)
        {
            // Not a builtin format, convert.
            // The format code string may get modified and adapted to the real
            // language and wouldn't match eSysLnge anymore, do that on a copy.
            OUString aTmp( rString);
            rNewInserted = PutandConvertEntrySystem( aTmp, rCheckPos, rType,
                                                     nKey, eLnge, SvtSysLocale().GetLanguageTag().getLanguageType());
            if (rCheckPos > 0)
            {
                SAL_WARN( "svl.numbers", "SvNumberFormatter::GetIndexPuttingAndConverting: bad format code string for current locale");
                nKey = NUMBERFORMAT_ENTRY_NOT_FOUND;
            }
        }
    }
    else
    {
        nKey = GetEntryKey( rString, eLnge);
        if (nKey == NUMBERFORMAT_ENTRY_NOT_FOUND)
        {
            rNewInserted = PutEntry( rString, rCheckPos, rType, nKey, eLnge);
            if (rCheckPos > 0)
            {
                SAL_WARN( "svl.numbers", "SvNumberFormatter::GetIndexPuttingAndConverting: bad format code string for specified locale");
                nKey = NUMBERFORMAT_ENTRY_NOT_FOUND;
            }
        }
    }
    if (nKey == NUMBERFORMAT_ENTRY_NOT_FOUND)
    {
        nKey = GetStandardIndex( eLnge);
    }
    rType = GetType( nKey);
    // Convert any (!) old "automatic" currency format to new fixed currency
    // default format.
    if (rType & SvNumFormatType::CURRENCY)
    {
        const SvNumberformat* pFormat = GetEntry( nKey);
        if (!pFormat->HasNewCurrency())
        {
            if (rNewInserted)
            {
                DeleteEntry( nKey);     // don't leave trails of rubbish
                rNewInserted = false;
            }
            nKey = GetStandardFormat( SvNumFormatType::CURRENCY, eLnge);
        }
    }
    return nKey;
}

void SvNumberFormatter::DeleteEntry(sal_uInt32 nKey)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    m_aFormatData.aFTable.erase(nKey);
}

void SvNumberFormatter::GetUsedLanguages( std::vector<LanguageType>& rList )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    rList.clear();

    sal_uInt32 nOffset = 0;
    while (nOffset <= m_aFormatData.MaxCLOffset)
    {
        SvNumberformat* pFormat = m_aFormatData.GetFormatEntry(nOffset);
        if (pFormat)
        {
            rList.push_back( pFormat->GetLanguage() );
        }
        nOffset += SV_COUNTRY_LANGUAGE_OFFSET;
    }
}


void SvNumberFormatter::FillKeywordTable( NfKeywordTable& rKeywords,
                                          LanguageType eLang )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    ChangeIntl( eLang );
    const NfKeywordTable & rTable = m_aCurrentLanguage.pFormatScanner->GetKeywords();
    for ( sal_uInt16 i = 0; i < NF_KEYWORD_ENTRIES_COUNT; ++i )
    {
        rKeywords[i] = rTable[i];
    }
}


void SvNumberFormatter::FillKeywordTableForExcel( NfKeywordTable& rKeywords )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    FillKeywordTable( rKeywords, LANGUAGE_ENGLISH_US );

    // Replace upper case "GENERAL" with proper case "General".
    rKeywords[ NF_KEY_GENERAL ] = GetStandardName( LANGUAGE_ENGLISH_US );

    // Excel or OOXML do not specify format code keywords case sensitivity,
    // but given and writes them lower case. Using upper case even lead to an
    // odd misrepresentation in iOS viewer and OSX Quicklook viewer that
    // strangely use "D" and "DD" for "days since beginning of year", which is
    // nowhere defined. See tdf#126773
    // Use lower case for all date and time keywords where known. See OOXML
    // ECMA-376-1:2016 18.8.31 numFmts (Number Formats)
    rKeywords[ NF_KEY_MI ]    = "m";
    rKeywords[ NF_KEY_MMI ]   = "mm";
    rKeywords[ NF_KEY_M ]     = "m";
    rKeywords[ NF_KEY_MM ]    = "mm";
    rKeywords[ NF_KEY_MMM ]   = "mmm";
    rKeywords[ NF_KEY_MMMM ]  = "mmmm";
    rKeywords[ NF_KEY_MMMMM ] = "mmmmm";
    rKeywords[ NF_KEY_H ]     = "h";
    rKeywords[ NF_KEY_HH ]    = "hh";
    rKeywords[ NF_KEY_S ]     = "s";
    rKeywords[ NF_KEY_SS ]    = "ss";
    /* XXX: not defined in OOXML: rKeywords[ NF_KEY_Q ]     = "q"; */
    /* XXX: not defined in OOXML: rKeywords[ NF_KEY_QQ ]    = "qq"; */
    rKeywords[ NF_KEY_D ]     = "d";
    rKeywords[ NF_KEY_DD ]    = "dd";
    rKeywords[ NF_KEY_DDD ]   = "ddd";
    rKeywords[ NF_KEY_DDDD ]  = "dddd";
    rKeywords[ NF_KEY_YY ]    = "yy";
    rKeywords[ NF_KEY_YYYY ]  = "yyyy";
    /* XXX: not defined in OOXML: rKeywords[ NF_KEY_AAA ]   = "aaa"; */
    /* XXX: not defined in OOXML: rKeywords[ NF_KEY_AAAA ]  = "aaaa"; */
    rKeywords[ NF_KEY_EC ]    = "e";
    rKeywords[ NF_KEY_EEC ]   = "ee";
    rKeywords[ NF_KEY_G ]     = "g";
    rKeywords[ NF_KEY_GG ]    = "gg";
    rKeywords[ NF_KEY_GGG ]   = "ggg";
    rKeywords[ NF_KEY_R ]     = "r";
    rKeywords[ NF_KEY_RR ]    = "rr";
    /* XXX: not defined in OOXML: rKeywords[ NF_KEY_WW ]    = "ww"; */

    // Remap codes unknown to Excel.
    rKeywords[ NF_KEY_NN ] = "ddd";
    rKeywords[ NF_KEY_NNN ] = "dddd";
    // NNNN gets a separator appended in SvNumberformat::GetMappedFormatString()
    rKeywords[ NF_KEY_NNNN ] = "dddd";
    // Export the Thai T NatNum modifier. This must be uppercase for internal reasons.
    rKeywords[ NF_KEY_THAI_T ] = "T";
}

static OUString lcl_buildBooleanStringFormat(const SvNumberformat* pEntry, const NativeNumberWrapper& rNatNum, const SvNFLanguageData& rCurrentLang)
{
    // Build Boolean number format, which needs non-zero and zero subformat
    // codes with TRUE and FALSE strings.
    const Color* pColor = nullptr;
    OUString aFormatStr, aTemp;
    pEntry->GetOutputString( 1.0, aTemp, &pColor, rNatNum, rCurrentLang );
    aFormatStr += "\"" + aTemp + "\";\"" + aTemp + "\";\"";
    pEntry->GetOutputString( 0.0, aTemp, &pColor, rNatNum, rCurrentLang );
    aFormatStr += aTemp + "\"";
    return aFormatStr;
}

OUString SvNumberFormatter::GetFormatStringForExcel( sal_uInt32 nKey, const NfKeywordTable& rKeywords,
        SvNumberFormatter& rTempFormatter ) const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    OUString aFormatStr;
    if (const SvNumberformat* pEntry = GetEntry( nKey))
    {
        if (pEntry->GetType() == SvNumFormatType::LOGICAL)
        {
            // Build a source locale dependent string boolean. This is
            // expected when loading the document in the same locale or if
            // several locales are used, but not for other system/default
            // locales. You can't have both. We could force to English for all
            // locales like below, but Excel would display English strings then
            // even for the system locale matching this locale. YMMV.
            aFormatStr = lcl_buildBooleanStringFormat(pEntry, GetNatNum(), m_aCurrentLanguage);
        }
        else
        {
            bool bIsLOK = comphelper::LibreOfficeKit::isActive();
            bool bSystemLanguage = false;
            LanguageType nLang = pEntry->GetLanguage();
            if (nLang == LANGUAGE_SYSTEM)
            {
                bSystemLanguage = true;
                nLang = SvtSysLocale().GetLanguageTag().getLanguageType();
            }
            if (!bIsLOK && nLang != LANGUAGE_ENGLISH_US)
            {
                sal_Int32 nCheckPos;
                SvNumFormatType nType = SvNumFormatType::DEFINED;
                sal_uInt32 nTempKey;
                OUString aTemp( pEntry->GetFormatstring());
                /* TODO: do we want bReplaceBooleanEquivalent=true in any case
                 * to write it as English string boolean? */
                rTempFormatter.PutandConvertEntry( aTemp, nCheckPos, nType, nTempKey, nLang, LANGUAGE_ENGLISH_US,
                        false /*bConvertDateOrder*/, false /*bReplaceBooleanEquivalent*/);
                SAL_WARN_IF( nCheckPos != 0, "svl.numbers",
                        "SvNumberFormatter::GetFormatStringForExcel - format code not convertible");
                if (nTempKey != NUMBERFORMAT_ENTRY_NOT_FOUND)
                    pEntry = rTempFormatter.GetEntry( nTempKey);
            }

            if (pEntry)
            {
                if (pEntry->GetType() == SvNumFormatType::LOGICAL)
                {
                    // This would be reached if bReplaceBooleanEquivalent was
                    // true and the source format is a string boolean like
                    // >"VRAI";"VRAI";"FAUX"< recognized as real boolean and
                    // properly converted. Then written as
                    // >"TRUE";"TRUE";"FALSE"<
                    aFormatStr = lcl_buildBooleanStringFormat(pEntry, GetNatNum(), m_aCurrentLanguage);
                }
                else
                {
                    // m_aCurrentLanguage.GetLocaleData() returns the current locale's data, so switch
                    // before (which doesn't do anything if it was the same locale
                    // already).
                    rTempFormatter.ChangeIntl( LANGUAGE_ENGLISH_US);
                    aFormatStr = pEntry->GetMappedFormatstring( rKeywords, *rTempFormatter.m_aCurrentLanguage.GetLocaleData(), nLang,
                            bSystemLanguage);
                }
            }
        }
    }
    else
    {
        SAL_WARN("svl.numbers","SvNumberFormatter::GetFormatStringForExcel - format not found: " << nKey);
    }

    if (aFormatStr.isEmpty())
        aFormatStr = "General";
    return aFormatStr;
}


OUString SvNumberFormatter::GetKeyword( LanguageType eLnge, sal_uInt16 nIndex )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    ChangeIntl(eLnge);
    const NfKeywordTable & rTable = m_aCurrentLanguage.pFormatScanner->GetKeywords();
    if ( nIndex < NF_KEYWORD_ENTRIES_COUNT )
    {
        return rTable[nIndex];
    }
    SAL_WARN( "svl.numbers", "GetKeyword: invalid index");
    return OUString();
}


OUString SvNumberFormatter::GetStandardName( LanguageType eLnge )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    ChangeIntl( eLnge );
    return m_aCurrentLanguage.pFormatScanner->GetStandardName();
}

SvNFFormatData::SvNFFormatData()
    : MaxCLOffset(0)
    , nDefaultSystemCurrencyFormat(NUMBERFORMAT_ENTRY_NOT_FOUND)
    , bNoZero(false)
{
}

SvNFFormatData::~SvNFFormatData() = default;

sal_uInt32 SvNFFormatData::ImpGetCLOffset(LanguageType eLnge) const
{
    sal_uInt32 nOffset = 0;
    while (nOffset <= MaxCLOffset)
    {
        const SvNumberformat* pFormat = GetFormatEntry(nOffset);
        if (pFormat && pFormat->GetLanguage() == eLnge)
        {
            return nOffset;
        }
        nOffset += SV_COUNTRY_LANGUAGE_OFFSET;
    }
    return nOffset;
}

sal_uInt32 SvNFFormatData::ImpIsEntry(std::u16string_view rString,
                                      sal_uInt32 nCLOffset,
                                      LanguageType eLnge) const
{
    sal_uInt32 res = NUMBERFORMAT_ENTRY_NOT_FOUND;
    auto it = aFTable.find( nCLOffset);
    while ( res == NUMBERFORMAT_ENTRY_NOT_FOUND &&
            it != aFTable.end() && it->second->GetLanguage() == eLnge )
    {
        if ( rString == it->second->GetFormatstring() )
        {
            res = it->first;
        }
        else
        {
            ++it;
        }
    }
    return res;
}

sal_uInt32 SvNumberFormatter::ImpIsEntry(std::u16string_view rString,
                                         sal_uInt32 nCLOffset,
                                         LanguageType eLnge) const
{
    return m_aFormatData.ImpIsEntry(rString, nCLOffset, eLnge);
}

SvNumberFormatTable& SvNumberFormatter::GetFirstEntryTable(
                                                      SvNumFormatType& eType,
                                                      sal_uInt32& FIndex,
                                                      LanguageType& rLnge)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    SvNumFormatType eTypetmp = eType;
    if (eType == SvNumFormatType::ALL)                  // empty cell or don't care
    {
        rLnge = IniLnge;
    }
    else
    {
        SvNumberformat* pFormat = m_aFormatData.GetFormatEntry(FIndex);
        if (!pFormat)
        {
            rLnge = IniLnge;
            eType = SvNumFormatType::ALL;
            eTypetmp = eType;
        }
        else
        {
            rLnge = pFormat->GetLanguage();
            eType = pFormat->GetMaskedType();
            if (eType == SvNumFormatType::ALL)
            {
                eType = SvNumFormatType::DEFINED;
                eTypetmp = eType;
            }
            else if (eType == SvNumFormatType::DATETIME)
            {
                eTypetmp = eType;
                eType = SvNumFormatType::DATE;
            }
            else
            {
                eTypetmp = eType;
            }
        }
    }
    ChangeIntl(rLnge);
    return GetEntryTable(eTypetmp, FIndex, rLnge);
}

sal_uInt32 SvNFFormatData::ImpGenerateCL(SvNFLanguageData& rCurrentLanguage, const NativeNumberWrapper& rNatNum, LanguageType eLnge)
{
    rCurrentLanguage.ChangeIntl(eLnge);
    sal_uInt32 CLOffset = ImpGetCLOffset(rCurrentLanguage.ActLnge);
    if (CLOffset > MaxCLOffset)
    {
        // new CL combination
        if (LocaleDataWrapper::areChecksEnabled())
        {
            const LanguageTag aLoadedLocale = rCurrentLanguage.xLocaleData->getLoadedLanguageTag();
            if ( !aLoadedLocale.equals( rCurrentLanguage.aLanguageTag ) )
            {
                LocaleDataWrapper::outputCheckMessage( rCurrentLanguage.xLocaleData->appendLocaleInfo( u"SvNumberFormatter::ImpGenerateCL: locales don't match:" ));
            }
            // test XML locale data FormatElement entries
            {
                uno::Sequence< i18n::FormatElement > xSeq = rCurrentLanguage.xLocaleData->getAllFormats();
                // A test for completeness of formatindex="0" ...
                // formatindex="47" is not needed here since it is done in
                // ImpGenerateFormats().

                // Test for dupes of formatindex="..."
                for ( sal_Int32 j = 0; j < xSeq.getLength(); j++ )
                {
                    sal_Int16 nIdx = xSeq[j].formatIndex;
                    OUStringBuffer aDupes;
                    for ( sal_Int32 i = 0; i < xSeq.getLength(); i++ )
                    {
                        if ( i != j && xSeq[i].formatIndex == nIdx )
                        {
                            aDupes.append( OUString::number(i) + "(" + xSeq[i].formatKey + ") ");
                        }
                    }
                    if ( !aDupes.isEmpty() )
                    {
                        OUString aMsg = "XML locale data FormatElement formatindex dupe: "
                                      + OUString::number(nIdx)
                                      + "\nFormatElements: "
                                      + OUString::number( j )
                                      + "("
                                      + xSeq[j].formatKey
                                      + ") "
                                      + aDupes;
                        LocaleDataWrapper::outputCheckMessage( rCurrentLanguage.xLocaleData->appendLocaleInfo( aMsg ));
                    }
                }
            }
        }

        MaxCLOffset += SV_COUNTRY_LANGUAGE_OFFSET;
        ImpGenerateFormats(rCurrentLanguage, rNatNum, MaxCLOffset, false/*bNoAdditionalFormats*/ );
        CLOffset = MaxCLOffset;
    }
    return CLOffset;
}

SvNumberFormatTable& SvNumberFormatter::ChangeCL(SvNumFormatType eType,
                                                 sal_uInt32& FIndex,
                                                 LanguageType eLnge)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    m_aFormatData.ImpGenerateCL(m_aCurrentLanguage, GetNatNum(), eLnge);
    return GetEntryTable(eType, FIndex, m_aCurrentLanguage.ActLnge);
}

SvNumberFormatTable& SvNumberFormatter::GetEntryTable(
                                                    SvNumFormatType eType,
                                                    sal_uInt32& FIndex,
                                                    LanguageType eLnge)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    if ( pFormatTable )
    {
        pFormatTable->clear();
    }
    else
    {
        pFormatTable.reset( new SvNumberFormatTable );
    }
    ChangeIntl(eLnge);
    sal_uInt32 CLOffset = m_aFormatData.ImpGetCLOffset(m_aCurrentLanguage.ActLnge);

    // Might generate and insert a default format for the given type
    // (e.g. currency) => has to be done before collecting formats.
    sal_uInt32 nDefaultIndex = GetStandardFormat( eType, m_aCurrentLanguage.ActLnge );

    auto it = m_aFormatData.aFTable.find( CLOffset);

    if (eType == SvNumFormatType::ALL)
    {
        while (it != m_aFormatData.aFTable.end() && it->second->GetLanguage() == m_aCurrentLanguage.ActLnge)
        {   // copy all entries to output table
            (*pFormatTable)[ it->first ] = it->second.get();
            ++it;
        }
    }
    else
    {
        while (it != m_aFormatData.aFTable.end() && it->second->GetLanguage() == m_aCurrentLanguage.ActLnge)
        {   // copy entries of queried type to output table
            if ((it->second->GetType()) & eType)
                (*pFormatTable)[ it->first ] = it->second.get();
            ++it;
        }
    }
    if ( !pFormatTable->empty() )
    {   // select default if queried format doesn't exist or queried type or
        // language differ from existing format
        SvNumberformat* pEntry = m_aFormatData.GetFormatEntry(FIndex);
        if ( !pEntry || !(pEntry->GetType() & eType) || pEntry->GetLanguage() != m_aCurrentLanguage.ActLnge )
        {
            FIndex = nDefaultIndex;
        }
    }
    return *pFormatTable;
}

namespace {

const SvNumberformat* ImpSubstituteEntry(SvNFLanguageData& rCurrentLanguage, const SvNFFormatData& rFormatData,
                                         const NativeNumberWrapper& rNatNum, const SvNFEngine::Accessor& rFuncs,
                                         const SvNumberformat* pFormat, sal_uInt32* o_pRealKey)
{
    if (!pFormat || !pFormat->IsSubstituted())
        return pFormat;

    // XXX NOTE: substitution can not be done in GetFormatEntry() as otherwise
    // to be substituted formats would "vanish", i.e. from the number formatter
    // dialog or when exporting to Excel.

    sal_uInt32 nKey;
    if (pFormat->IsSystemTimeFormat())
    {
        /* TODO: should we have NF_TIME_SYSTEM for consistency? */
        nKey = SvNFEngine::GetStandardFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs,
                                             SvNumFormatType::TIME, LANGUAGE_SYSTEM);
    }
    else if (pFormat->IsSystemLongDateFormat())
        /* TODO: either that above, or have a long option for GetStandardFormat() */
    {
        nKey = SvNFEngine::GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum,
                                          NF_DATE_SYSTEM_LONG, LANGUAGE_SYSTEM);
    }
    else
        return pFormat;

    if (o_pRealKey)
        *o_pRealKey = nKey;
    return rFormatData.GetFormatEntry(nKey);
}

}

bool SvNFEngine::IsNumberFormat(SvNFLanguageData& rCurrentLanguage,
                                const SvNFFormatData& rFormatData,
                                const NativeNumberWrapper& rNatNum, const Accessor& rFuncs,
                                const OUString& sString,
                                sal_uInt32& F_Index,
                                double& fOutNumber,
                                SvNumInputOptions eInputOptions)
{
    SvNumFormatType FType;
    // For the 0 General format directly use the init/system locale and avoid
    // all overhead that is associated with a format passed to the scanner.
    const SvNumberformat* pFormat = (F_Index == 0 ? nullptr :
            ::ImpSubstituteEntry(rCurrentLanguage, rFormatData, rNatNum, rFuncs, rFormatData.GetFormatEntry(F_Index), nullptr));
    if (!pFormat)
    {
        rCurrentLanguage.ChangeIntl(rCurrentLanguage.IniLnge);
        FType = SvNumFormatType::NUMBER;
    }
    else
    {
        FType = pFormat->GetMaskedType();
        if (FType == SvNumFormatType::ALL)
        {
            FType = SvNumFormatType::DEFINED;
        }
        rCurrentLanguage.ChangeIntl(pFormat->GetLanguage());
        // Avoid scanner overhead with the General format of any locale.
        // These are never substituted above so safe to ignore.
        if ((F_Index % SV_COUNTRY_LANGUAGE_OFFSET) == 0)
        {
            assert(FType == SvNumFormatType::NUMBER);
            pFormat = nullptr;
        }
    }

    bool res;
    SvNumFormatType RType = FType;
    if (RType == SvNumFormatType::TEXT)
    {
        res = false;        // type text preset => no conversion to number
    }
    else
    {
        res = rCurrentLanguage.pStringScanner->IsNumberFormat(sString, RType, fOutNumber, pFormat, rNatNum, eInputOptions);
    }
    if (res && !SvNumberFormatter::IsCompatible(FType, RType))     // non-matching type
    {
        switch ( RType )
        {
        case SvNumFormatType::DATE :
            // Preserve ISO 8601 input.
            if (rCurrentLanguage.pStringScanner->CanForceToIso8601( DateOrder::Invalid))
            {
                F_Index = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATE_DIN_YYYYMMDD, rCurrentLanguage.ActLnge );
            }
            else
            {
                F_Index = GetStandardFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, RType, rCurrentLanguage.ActLnge);
            }
            break;
        case SvNumFormatType::TIME :
            if ( rCurrentLanguage.pStringScanner->GetDecPos() )
            {
                // 100th seconds
                if ( rCurrentLanguage.pStringScanner->GetNumericsCount() > 3 || fOutNumber < 0.0 )
                {
                    F_Index = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_TIME_HH_MMSS00, rCurrentLanguage.ActLnge);
                }
                else
                {
                    F_Index = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_TIME_MMSS00, rCurrentLanguage.ActLnge);
                }
            }
            else if ( fOutNumber >= 1.0 || fOutNumber < 0.0 )
            {
                F_Index = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_TIME_HH_MMSS, rCurrentLanguage.ActLnge);
            }
            else
            {
                F_Index = GetStandardFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, RType, rCurrentLanguage.ActLnge );
            }
            break;
        case SvNumFormatType::DATETIME :
            // Preserve ISO 8601 input.
            if (rCurrentLanguage.pStringScanner->HasIso8601Tsep())
            {
                if (rCurrentLanguage.pStringScanner->GetDecPos())
                    F_Index = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDDTHHMMSS000, rCurrentLanguage.ActLnge);
                else
                    F_Index = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDDTHHMMSS, rCurrentLanguage.ActLnge);
            }
            else if (rCurrentLanguage.pStringScanner->CanForceToIso8601( DateOrder::Invalid))
            {
                if (rCurrentLanguage.pStringScanner->GetDecPos())
                    F_Index = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDD_HHMMSS000, rCurrentLanguage.ActLnge);
                else
                    F_Index = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDD_HHMMSS, rCurrentLanguage.ActLnge);
            }
            else
            {
                F_Index = GetStandardFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, RType, rCurrentLanguage.ActLnge);
            }
            break;
        default:
            F_Index = GetStandardFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, RType, rCurrentLanguage.ActLnge);
        }
    }
    return res;
}

bool SvNumberFormatter::IsNumberFormat(const OUString& sString,
                                       sal_uInt32& F_Index,
                                       double& fOutNumber,
                                       SvNumInputOptions eInputOptions)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );

    return SvNFEngine::IsNumberFormat(m_aCurrentLanguage, m_aFormatData, GetNatNum(),
                                      m_aRWPolicy, sString, F_Index, fOutNumber,
                                      eInputOptions);
}

LanguageType SvNumberFormatter::GetLanguage() const
{
    return IniLnge; // immutable
}

// static
bool SvNumberFormatter::IsCompatible(SvNumFormatType eOldType, SvNumFormatType eNewType)
{
    if (eOldType == eNewType)
    {
        return true;
    }
    else if (eOldType == SvNumFormatType::DEFINED)
    {
        return true;
    }
    else
    {
        switch (eNewType)
        {
        case SvNumFormatType::NUMBER:
            switch (eOldType)
            {
            case SvNumFormatType::PERCENT:
            case SvNumFormatType::CURRENCY:
            case SvNumFormatType::SCIENTIFIC:
            case SvNumFormatType::FRACTION:
            case SvNumFormatType::DEFINED:
                return true;
            case SvNumFormatType::LOGICAL:
            default:
                return false;
            }
            break;
        case SvNumFormatType::DATE:
            switch (eOldType)
            {
            case SvNumFormatType::DATETIME:
                return true;
            default:
                return false;
            }
            break;
        case SvNumFormatType::TIME:
            switch (eOldType)
            {
            case SvNumFormatType::DATETIME:
                return true;
            default:
                return false;
            }
            break;
        case SvNumFormatType::DATETIME:
            switch (eOldType)
            {
            case SvNumFormatType::TIME:
            case SvNumFormatType::DATE:
                return true;
            default:
                return false;
            }
            break;
        case SvNumFormatType::DURATION:
            return false;
        default:
            return false;
        }
    }
}

static sal_uInt32 ImpGetSearchOffset(SvNumFormatType nType, sal_uInt32 CLOffset)
{
    sal_uInt32 nSearch;
    switch( nType )
    {
    case SvNumFormatType::DATE:
        nSearch = CLOffset + ZF_STANDARD_DATE;
        break;
    case SvNumFormatType::TIME:
        nSearch = CLOffset + ZF_STANDARD_TIME;
        break;
    case SvNumFormatType::DATETIME:
        nSearch = CLOffset + ZF_STANDARD_DATETIME;
        break;
    case SvNumFormatType::DURATION:
        nSearch = CLOffset + ZF_STANDARD_DURATION;
        break;
    case SvNumFormatType::PERCENT:
        nSearch = CLOffset + ZF_STANDARD_PERCENT;
        break;
    case SvNumFormatType::SCIENTIFIC:
        nSearch = CLOffset + ZF_STANDARD_SCIENTIFIC;
        break;
    default:
        nSearch = CLOffset + ZF_STANDARD;
    }
    return nSearch;
}

// static
sal_uInt32 SvNFEngine::ImpGetDefaultFormat(const SvNFFormatData& rFormatData,
                                           const SvNFEngine::Accessor& rFuncs,
                                           SvNumFormatType nType, sal_uInt32 CLOffset)
{
    sal_uInt32 nSearch = ImpGetSearchOffset(nType, CLOffset);

    sal_uInt32 nDefaultFormat = rFuncs.mFindFormat(nSearch);
    if (nDefaultFormat != NUMBERFORMAT_ENTRY_NOT_FOUND)
        return nDefaultFormat;

    nDefaultFormat = ImpGetDefaultFormat(rFormatData, nType, CLOffset);
    rFuncs.mCacheFormat(nSearch, nDefaultFormat);
    return nDefaultFormat;
}

namespace {

    sal_uInt32 ImpGetFormatIndex(NfIndexTableOffset nTabOff, sal_uInt32 nCLOffset)
    {
        if (nTabOff >= NF_INDEX_TABLE_ENTRIES)
            return NUMBERFORMAT_ENTRY_NOT_FOUND;

        if (indexTable[nTabOff] == NUMBERFORMAT_ENTRY_NOT_FOUND)
            return NUMBERFORMAT_ENTRY_NOT_FOUND;

        return nCLOffset + indexTable[nTabOff];
    }

    bool ImpIsSpecialStandardFormat(sal_uInt32 nFIndex, sal_uInt32 nCLOffset)
    {
        return
            nFIndex == ImpGetFormatIndex( NF_TIME_MMSS00, nCLOffset ) ||
            nFIndex == ImpGetFormatIndex( NF_TIME_HH_MMSS00, nCLOffset ) ||
            nFIndex == ImpGetFormatIndex( NF_TIME_HH_MMSS, nCLOffset )
            ;
    }

    bool ImpIsSpecialStandardFormat(SvNFLanguageData& rCurrentLanguage,
                                    const NativeNumberWrapper& rNatNum, const SvNFEngine::Accessor& rFuncs,
                                    sal_uInt32 nFIndex, LanguageType eLnge)
    {
        eLnge = rCurrentLanguage.ImpResolveLanguage(eLnge);
        sal_uInt32 nCLOffset = rFuncs.mGetCLOffset(rCurrentLanguage, rNatNum, eLnge); // create new standard formats if necessary (and allowed)
        return ::ImpIsSpecialStandardFormat(nFIndex, nCLOffset);
    }
}

sal_uInt32 SvNFEngine::ImpGetStandardFormat(SvNFLanguageData& rCurrentLanguage,
                                            const SvNFFormatData& rFormatData,
                                            const NativeNumberWrapper& rNatNum,
                                            const SvNFEngine::Accessor& rFuncs,
                                            SvNumFormatType eType,
                                            sal_uInt32 CLOffset,
                                            LanguageType eLnge)
{
    switch(eType)
    {
    case SvNumFormatType::CURRENCY:
        return rFuncs.mGetDefaultCurrency(rCurrentLanguage, rNatNum, CLOffset, eLnge);
    case SvNumFormatType::DURATION :
        return ImpGetFormatIndex(NF_TIME_HH_MMSS, CLOffset);
    case SvNumFormatType::DATE:
    case SvNumFormatType::TIME:
    case SvNumFormatType::DATETIME:
    case SvNumFormatType::PERCENT:
    case SvNumFormatType::SCIENTIFIC:
        return ImpGetDefaultFormat(rFormatData, rFuncs, eType, CLOffset);
    case SvNumFormatType::FRACTION:
        return CLOffset + ZF_STANDARD_FRACTION;
    case SvNumFormatType::LOGICAL:
        return CLOffset + ZF_STANDARD_LOGICAL;
    case SvNumFormatType::TEXT:
        return CLOffset + ZF_STANDARD_TEXT;
    case SvNumFormatType::ALL:
    case SvNumFormatType::DEFINED:
    case SvNumFormatType::NUMBER:
    case SvNumFormatType::UNDEFINED:
    default:
        return CLOffset + ZF_STANDARD;
    }
}

sal_uInt32 SvNFEngine::GetStandardFormat(SvNFLanguageData& rCurrentLanguage,
                                         const SvNFFormatData& rFormatData,
                                         const NativeNumberWrapper& rNatNum, const Accessor& rFuncs,
                                         SvNumFormatType eType,
                                         LanguageType eLnge)
{
    eLnge = rCurrentLanguage.ImpResolveLanguage(eLnge);

    sal_uInt32 nCLOffset = rFuncs.mGetCLOffset(rCurrentLanguage, rNatNum, eLnge); // create new standard formats if necessary (and allowed)

    return ImpGetStandardFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, eType, nCLOffset, eLnge);
}

sal_uInt32 SvNumberFormatter::GetStandardFormat( SvNumFormatType eType, LanguageType eLnge )
{
    ::osl::MutexGuard aGuard(GetInstanceMutex());
    return SvNFEngine::GetStandardFormat(m_aCurrentLanguage, m_aFormatData, GetNatNum(), m_aRWPolicy, eType, eLnge);
}

bool SvNumberFormatter::ImpIsSpecialStandardFormat(sal_uInt32 nFIndex, LanguageType eLnge)
{
    ::osl::MutexGuard aGuard(GetInstanceMutex());
    return ::ImpIsSpecialStandardFormat(m_aCurrentLanguage, GetNatNum(), m_aRWPolicy, nFIndex, eLnge);
}

sal_uInt32 SvNFEngine::GetStandardFormat(SvNFLanguageData& rCurrentLanguage,
                                         const SvNFFormatData& rFormatData,
                                         const NativeNumberWrapper& rNatNum, const Accessor& rFuncs,
                                         sal_uInt32 nFIndex, SvNumFormatType eType, LanguageType eLnge)
{
    eLnge = rCurrentLanguage.ImpResolveLanguage(eLnge);

    sal_uInt32 nCLOffset = rFuncs.mGetCLOffset(rCurrentLanguage, rNatNum, eLnge); // create new standard formats if necessary (and allowed)

    if (::ImpIsSpecialStandardFormat(nFIndex, nCLOffset))
        return nFIndex;

    return ImpGetStandardFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, eType, nCLOffset, eLnge);
}

namespace
{
    sal_uInt32 FindCachedDefaultFormat(const SvNFFormatData::DefaultFormatKeysMap& rDefaultFormatKeys, sal_uInt32 nSearch)
    {
        auto it = rDefaultFormatKeys.find(nSearch);
        sal_uInt32 nDefaultFormat = (it != rDefaultFormatKeys.end() ?
                                     it->second : NUMBERFORMAT_ENTRY_NOT_FOUND);
        return nDefaultFormat;
    }
}

sal_uInt32 SvNFFormatData::FindCachedDefaultFormat(sal_uInt32 nSearch) const
{
    return ::FindCachedDefaultFormat(aDefaultFormatKeys, nSearch);
}

// static
sal_uInt32 SvNFEngine::ImpGetDefaultFormat(const SvNFFormatData& rFormatData, SvNumFormatType nType, sal_uInt32 CLOffset)
{
    sal_uInt32 nDefaultFormat = NUMBERFORMAT_ENTRY_NOT_FOUND;

    // look for a defined standard
    sal_uInt32 nStopKey = CLOffset + SV_COUNTRY_LANGUAGE_OFFSET;
    sal_uInt32 nKey(0);
    auto it2 = rFormatData.aFTable.find( CLOffset );
    while ( it2 != rFormatData.aFTable.end() && (nKey = it2->first ) >= CLOffset && nKey < nStopKey )
    {
        const SvNumberformat* pEntry = it2->second.get();
        if ( pEntry->IsStandard() && (pEntry->GetMaskedType() == nType) )
        {
            nDefaultFormat = nKey;
            break;  // while
        }
        ++it2;
    }

    if ( nDefaultFormat == NUMBERFORMAT_ENTRY_NOT_FOUND )
    {   // none found, use old fixed standards
        switch( nType )
        {
        case SvNumFormatType::DATE:
            nDefaultFormat = CLOffset + ZF_STANDARD_DATE;
            break;
        case SvNumFormatType::TIME:
            nDefaultFormat = CLOffset + ZF_STANDARD_TIME+1;
            break;
        case SvNumFormatType::DATETIME:
            nDefaultFormat = CLOffset + ZF_STANDARD_DATETIME;
            break;
        case SvNumFormatType::DURATION:
            nDefaultFormat = CLOffset + ZF_STANDARD_DURATION;
            break;
        case SvNumFormatType::PERCENT:
            nDefaultFormat = CLOffset + ZF_STANDARD_PERCENT+1;
            break;
        case SvNumFormatType::SCIENTIFIC:
            nDefaultFormat = CLOffset + ZF_STANDARD_SCIENTIFIC;
            break;
        default:
            nDefaultFormat = CLOffset + ZF_STANDARD;
        }
    }

    return nDefaultFormat;
}

sal_uInt32 SvNumberFormatter::GetStandardFormat( sal_uInt32 nFIndex, SvNumFormatType eType,
                                                 LanguageType eLnge )
{
    ::osl::MutexGuard aGuard(GetInstanceMutex());

    return SvNFEngine::GetStandardFormat(m_aCurrentLanguage, m_aFormatData, GetNatNum(),
                                         m_aRWPolicy, nFIndex, eType, eLnge);
}

sal_uInt32 SvNFEngine::GetTimeFormat(SvNFLanguageData& rCurrentLanguage,
                                     const SvNFFormatData& rFormatData,
                                     const NativeNumberWrapper& rNatNum, const Accessor& rFuncs,
                                     double fNumber, LanguageType eLnge, bool bForceDuration)
{
    bool bSign;
    if ( fNumber < 0.0 )
    {
        bSign = true;
        fNumber = -fNumber;
    }
    else
        bSign = false;
    double fSeconds = fNumber * 86400;
    if ( floor( fSeconds + 0.5 ) * 100 != floor( fSeconds * 100 + 0.5 ) )
    {   // with 100th seconds
        if ( bForceDuration || bSign || fSeconds >= 3600 )
            return GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_TIME_HH_MMSS00, eLnge);
        else
            return GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_TIME_MMSS00, eLnge);
    }
    else
    {
        if ( bForceDuration || bSign || fNumber >= 1.0 )
            return GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_TIME_HH_MMSS, eLnge);
        else
            return GetStandardFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, SvNumFormatType::TIME, eLnge);
    }
}

sal_uInt32 SvNumberFormatter::GetTimeFormat( double fNumber, LanguageType eLnge, bool bForceDuration )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );

    return SvNFEngine::GetTimeFormat(m_aCurrentLanguage, m_aFormatData, GetNatNum(), m_aRWPolicy,
                                     fNumber, eLnge, bForceDuration);
}

sal_uInt32 SvNFEngine::GetStandardFormat(SvNFLanguageData& rCurrentLanguage,
                                        const SvNFFormatData& rFormatData,
                                        const NativeNumberWrapper& rNatNum, const Accessor& rFuncs,
                                        double fNumber, sal_uInt32 nFIndex,
                                        SvNumFormatType eType, LanguageType eLnge)
{
    if (::ImpIsSpecialStandardFormat(rCurrentLanguage, rNatNum, rFuncs, nFIndex, eLnge))
        return nFIndex;

    switch( eType )
    {
        case SvNumFormatType::DURATION :
            return GetTimeFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, fNumber, eLnge, true);
        case SvNumFormatType::TIME :
            return GetTimeFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, fNumber, eLnge, false);
        default:
            return GetStandardFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, eType, eLnge );
    }
}

sal_uInt32 SvNumberFormatter::GetStandardFormat( double fNumber, sal_uInt32 nFIndex,
                                                 SvNumFormatType eType, LanguageType eLnge )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    if ( ImpIsSpecialStandardFormat( nFIndex, eLnge ) )
        return nFIndex;

    switch( eType )
    {
        case SvNumFormatType::DURATION :
            return GetTimeFormat( fNumber, eLnge, true);
        case SvNumFormatType::TIME :
            return GetTimeFormat( fNumber, eLnge, false);
        default:
            return GetStandardFormat( eType, eLnge );
    }
}

sal_uInt32 SvNumberFormatter::GuessDateTimeFormat( SvNumFormatType& rType, double fNumber, LanguageType eLnge )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    // Categorize the format according to the implementation of
    // SvNumberFormatter::GetEditFormat(), making assumptions about what
    // would be time only.
    sal_uInt32 nRet;
    if (0.0 <= fNumber && fNumber < 1.0)
    {
        // Clearly a time.
        rType = SvNumFormatType::TIME;
        nRet = GetTimeFormat( fNumber, eLnge, false);
    }
    else if (fabs( fNumber) * 24 < 0x7fff)
    {
        // Assuming duration within 32k hours or 3.7 years.
        // This should be SvNumFormatType::DURATION instead, but the outer
        // world can't cope with that.
        rType = SvNumFormatType::TIME;
        nRet = GetTimeFormat( fNumber, eLnge, true);
    }
    else if (rtl::math::approxFloor( fNumber) != fNumber)
    {
        // Date+Time.
        rType = SvNumFormatType::DATETIME;
        nRet = GetFormatIndex( NF_DATETIME_SYS_DDMMYYYY_HHMMSS, eLnge);
    }
    else
    {
        // Date only.
        rType = SvNumFormatType::DATE;
        nRet = GetFormatIndex( NF_DATE_SYS_DDMMYYYY, eLnge);
    }
    return nRet;
}

sal_uInt32 SvNFEngine::GetEditFormat(SvNFLanguageData& rCurrentLanguage,
                                     const SvNFFormatData& rFormatData,
                                     const NativeNumberWrapper& rNatNum, const Accessor& rFuncs,
                                     double fNumber, sal_uInt32 nFIndex,
                                     SvNumFormatType eType,
                                     const SvNumberformat* pFormat,
                                     LanguageType eForLocale)
{
    const LanguageType eLang = (pFormat ? pFormat->GetLanguage() : LANGUAGE_SYSTEM);
    if (eForLocale == LANGUAGE_DONTKNOW)
        eForLocale = eLang;
    sal_uInt32 nKey = nFIndex;
    switch ( eType )
    {
    // #61619# always edit using 4-digit year
    case SvNumFormatType::DATE :
        {
            // Preserve ISO 8601 format.
            bool bIsoDate =
                nFIndex == GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATE_DIN_YYYYMMDD, eLang) ||
                nFIndex == GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATE_DIN_YYMMDD, eLang) ||
                nFIndex == GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATE_DIN_MMDD, eLang) ||
                (pFormat && pFormat->IsIso8601( 0 ));
            if (rtl::math::approxFloor( fNumber) != fNumber)
            {
                // fdo#34977 preserve time when editing even if only date was
                // displayed.
                if (bIsoDate)
                    nKey = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDD_HHMMSS, eForLocale);
                else
                    nKey = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_SYS_DDMMYYYY_HHMMSS, eForLocale);
            }
            else
            {
                if (bIsoDate)
                    nKey = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATE_ISO_YYYYMMDD, eForLocale);
                else
                    nKey = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATE_SYS_DDMMYYYY, eForLocale);
            }
        }
        break;
    case SvNumFormatType::TIME :
        if (fNumber < 0.0 || fNumber >= 1.0)
        {
            /* XXX NOTE: this is a purely arbitrary value within the limits
             * of a signed 16-bit. 32k hours are 3.7 years ... or
             * 1903-09-26 if date. */
            if (fabs( fNumber) * 24 < 0x7fff)
                nKey = GetTimeFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, fNumber, eForLocale, true);
            // Preserve duration, use [HH]:MM:SS instead of time.
            else
                nKey = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_SYS_DDMMYYYY_HHMMSS, eForLocale);
            // Assume that a large value is a datetime with only time
            // displayed.
        }
        else
            nKey = GetStandardFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, fNumber, nFIndex, eType, eForLocale);
        break;
    case SvNumFormatType::DURATION :
        nKey = GetTimeFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, fNumber, eForLocale, true);
        break;
    case SvNumFormatType::DATETIME :
        if (nFIndex == GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDDTHHMMSS, eLang))
            nKey = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDDTHHMMSS, eForLocale );
        else if (nFIndex == GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDDTHHMMSS000, eLang))
            nKey = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDDTHHMMSS000, eForLocale );
        else if (nFIndex == GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDD_HHMMSS000, eLang))
            nKey = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDD_HHMMSS000, eForLocale );
        else if (nFIndex == GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDD_HHMMSS, eLang) || (pFormat && pFormat->IsIso8601( 0 )))
            nKey = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_ISO_YYYYMMDD_HHMMSS, eForLocale );
        else
            nKey = GetFormatIndex(rCurrentLanguage, rFuncs, rNatNum, NF_DATETIME_SYS_DDMMYYYY_HHMMSS, eForLocale );
        break;
    case SvNumFormatType::NUMBER:
        nKey = GetStandardFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, eType, eForLocale);
        break;
    default:
        nKey = GetStandardFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs, fNumber, nFIndex, eType, eForLocale );
    }
    return nKey;
}

sal_uInt32 SvNumberFormatter::GetEditFormat( double fNumber, sal_uInt32 nFIndex,
                                             SvNumFormatType eType,
                                             SvNumberformat const * pFormat,
                                             LanguageType eForLocale )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return SvNFEngine::GetEditFormat(m_aCurrentLanguage, m_aFormatData, GetNatNum(),
                                     m_aRWPolicy, fNumber, nFIndex,
                                     eType, pFormat, eForLocale);
}

OUString SvNFEngine::GetInputLineString(SvNFLanguageData& rCurrentLanguage,
                                    const SvNFFormatData& rFormatData,
                                    const NativeNumberWrapper& rNatNum, const Accessor& rFuncs,
                                    const double& fOutNumber,
                                    sal_uInt32 nFIndex,
                                    bool bFiltering, bool bForceSystemLocale)
{
    OUString sOutString;
    const Color* pColor;
    sal_uInt32 nRealKey = nFIndex;

    const SvNumberformat* pFormat = ImpSubstituteEntry(rCurrentLanguage, rFormatData,
                                         rNatNum, rFuncs,
                                         rFormatData.GetFormatEntry(nFIndex), &nRealKey);
    if (!pFormat)
        pFormat = rFormatData.GetFormatEntry(ZF_STANDARD);

    LanguageType eLang = pFormat->GetLanguage();
    rCurrentLanguage.ChangeIntl(eLang);

    SvNumFormatType eType = pFormat->GetMaskedType();
    if (eType == SvNumFormatType::ALL)
    {
        // Mixed types in subformats, use first.
        /* XXX we could choose a subformat according to fOutNumber and
         * subformat conditions, but they may exist to suppress 0 or negative
         * numbers so wouldn't be a safe bet. */
        eType = pFormat->GetNumForInfoScannedType(0);
    }
    const SvNumFormatType eTypeOrig = eType;

    sal_uInt16 nOldPrec = rCurrentLanguage.pFormatScanner->GetStandardPrec();
    bool bPrecChanged = false;
    if (eType == SvNumFormatType::NUMBER ||
        eType == SvNumFormatType::PERCENT ||
        eType == SvNumFormatType::CURRENCY ||
        eType == SvNumFormatType::SCIENTIFIC ||
        eType == SvNumFormatType::FRACTION)
    {
        if (eType != SvNumFormatType::PERCENT)  // special treatment of % later
        {
            eType = SvNumFormatType::NUMBER;
        }
        rCurrentLanguage.ChangeStandardPrec(SvNumberFormatter::INPUTSTRING_PRECISION);
        bPrecChanged = true;
    }

    // if bFiltering true keep the nRealKey format
    if (!bFiltering)
    {
        sal_uInt32 nKey = GetEditFormat(rCurrentLanguage, rFormatData, rNatNum, rFuncs,
                                        fOutNumber, nRealKey, eType, pFormat,
                                        bForceSystemLocale ? LANGUAGE_SYSTEM : LANGUAGE_DONTKNOW);
        if (nKey != nRealKey)
        {
            pFormat = rFormatData.GetFormatEntry( nKey );
        }
    }
    assert(pFormat);
    if (pFormat)
    {
        if ( eType == SvNumFormatType::TIME && pFormat->GetFormatPrecision() )
        {
            rCurrentLanguage.ChangeStandardPrec(SvNumberFormatter::INPUTSTRING_PRECISION);
            bPrecChanged = true;
        }
        pFormat->GetOutputString(fOutNumber, sOutString, &pColor, rNatNum, rCurrentLanguage);

        // The #FMT error string must not be used for input as it would lead to
        // data loss. This can happen for at least date(+time). Fall back to a
        // last resort of plain number in the locale the formatter was
        // constructed with.
        if (eTypeOrig != SvNumFormatType::NUMBER && sOutString == ImpSvNumberformatScan::sErrStr)
        {
            pFormat = rFormatData.GetFormatEntry(ZF_STANDARD);
            assert(pFormat);
            if (pFormat)
            {
                rCurrentLanguage.ChangeStandardPrec(SvNumberFormatter::INPUTSTRING_PRECISION);
                bPrecChanged = true;
                pFormat->GetOutputString(fOutNumber, sOutString, &pColor, rNatNum, rCurrentLanguage);
            }
        }
        assert(sOutString != ImpSvNumberformatScan::sErrStr);
    }
    if (bPrecChanged)
    {
        rCurrentLanguage.ChangeStandardPrec(nOldPrec);
    }
    return sOutString;
}

OUString SvNumberFormatter::GetInputLineString(const double& fOutNumber,
                                           sal_uInt32 nFIndex,
                                           bool bFiltering, bool bForceSystemLocale)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return SvNFEngine::GetInputLineString(m_aCurrentLanguage, m_aFormatData, GetNatNum(),
                                   m_aRWPolicy, fOutNumber, nFIndex,
                                   bFiltering, bForceSystemLocale);
}

void SvNFEngine::GetOutputString(SvNFLanguageData& rCurrentLanguage,
                                 const SvNFFormatData& rFormatData,
                                 const OUString& sString, sal_uInt32 nFIndex, OUString& sOutString,
                                 const Color** ppColor, bool bUseStarFormat)
{
    const SvNumberformat* pFormat = rFormatData.GetFormatEntry( nFIndex );
    // ImpSubstituteEntry() is unnecessary here because so far only numeric
    // (time and date) are substituted.
    if (!pFormat)
    {
        pFormat = rFormatData.GetFormatEntry(ZF_STANDARD_TEXT);
    }
    if (!pFormat->IsTextFormat() && !pFormat->HasTextFormat())
    {
        *ppColor = nullptr;
        sOutString = sString;
    }
    else
    {
        rCurrentLanguage.ChangeIntl(pFormat->GetLanguage());
        pFormat->GetOutputString(sString, sOutString, ppColor, bUseStarFormat);
    }
}

void SvNumberFormatter::GetOutputString(const OUString& sString,
                                        sal_uInt32 nFIndex,
                                        OUString& sOutString,
                                        const Color** ppColor,
                                        bool bUseStarFormat )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    SvNFEngine::GetOutputString(m_aCurrentLanguage, m_aFormatData,
                                sString, nFIndex, sOutString,
                                ppColor, bUseStarFormat);
}

void SvNFEngine::GetOutputString(SvNFLanguageData& rCurrentLanguage,
                                 const SvNFFormatData& rFormatData,
                                 const NativeNumberWrapper& rNatNum, const Accessor& rFuncs,
                                 const double& fOutNumber, sal_uInt32 nFIndex,
                                 OUString& sOutString, const Color** ppColor,
                                 bool bUseStarFormat)
{
    if (rFormatData.GetNoZero() && fOutNumber == 0.0)
    {
        sOutString.clear();
        return;
    }
    const SvNumberformat* pFormat = ImpSubstituteEntry(rCurrentLanguage, rFormatData,
                                         rNatNum, rFuncs,
                                         rFormatData.GetFormatEntry(nFIndex), nullptr);
    if (!pFormat)
        pFormat = rFormatData.GetFormatEntry(ZF_STANDARD);
    rCurrentLanguage.ChangeIntl(pFormat->GetLanguage());
    pFormat->GetOutputString(fOutNumber, sOutString, ppColor, rNatNum, rCurrentLanguage, bUseStarFormat);
}

void SvNumberFormatter::GetOutputString(const double& fOutNumber,
                                        sal_uInt32 nFIndex,
                                        OUString& sOutString,
                                        const Color** ppColor,
                                        bool bUseStarFormat )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    SvNFEngine::GetOutputString(m_aCurrentLanguage, m_aFormatData, GetNatNum(),
                                m_aRWPolicy, fOutNumber, nFIndex, sOutString,
                                ppColor, bUseStarFormat);
}

bool SvNFEngine::GetPreviewString(SvNFLanguageData& rCurrentLanguage,
                                  const SvNFFormatData& rFormatData,
                                  const NativeNumberWrapper& rNatNum,
                                  const Accessor& rFuncs,
                                  const OUString& sFormatString,
                                  double fPreviewNumber,
                                  OUString& sOutString,
                                  const Color** ppColor,
                                  LanguageType eLnge,
                                  bool bUseStarFormat)
{
    if (sFormatString.isEmpty())                       // no empty string
        return false;
    eLnge = rCurrentLanguage.ImpResolveLanguage(eLnge);
    rCurrentLanguage.ChangeIntl(eLnge);                // change locale if necessary
    eLnge = rCurrentLanguage.ActLnge;
    sal_Int32 nCheckPos = -1;
    OUString sTmpString = sFormatString;
    SvNumberformat aEntry(sTmpString,
                          rCurrentLanguage.pFormatScanner.get(),
                          rCurrentLanguage.pStringScanner.get(),
                          rNatNum,
                          nCheckPos,
                          eLnge);
    if (nCheckPos == 0)                                 // String ok
    {
        sal_uInt32 nCLOffset = rFuncs.mGetCLOffset(rCurrentLanguage, rNatNum, eLnge); // create new standard formats if necessary (and allowed)
        sal_uInt32 nKey = rFormatData.ImpIsEntry(aEntry.GetFormatstring(), nCLOffset, eLnge);
        if (nKey != NUMBERFORMAT_ENTRY_NOT_FOUND)       // already present
        {
            GetOutputString(rCurrentLanguage, rFormatData, rNatNum, rFuncs, fPreviewNumber, nKey, sOutString, ppColor, bUseStarFormat);
        }
        else
        {
            aEntry.GetOutputString(fPreviewNumber, sOutString, ppColor, rNatNum, rCurrentLanguage, bUseStarFormat);
        }
        return true;
    }
    else
    {
        return false;
    }
}

bool SvNumberFormatter::GetPreviewString(const OUString& sFormatString,
                                         double fPreviewNumber,
                                         OUString& sOutString,
                                         const Color** ppColor,
                                         LanguageType eLnge,
                                         bool bUseStarFormat )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return SvNFEngine::GetPreviewString(m_aCurrentLanguage, m_aFormatData,
                                        GetNatNum(), m_aRWPolicy,
                                        sFormatString, fPreviewNumber,
                                        sOutString, ppColor, eLnge,
                                        bUseStarFormat);
}

bool SvNFEngine::GetPreviewStringGuess(SvNFLanguageData& rCurrentLanguage,
                                       const SvNFFormatData& rFormatData,
                                       const NativeNumberWrapper& rNatNum,
                                       const Accessor& rFuncs,
                                       const OUString& sFormatString,
                                       double fPreviewNumber,
                                       OUString& sOutString,
                                       const Color** ppColor,
                                       LanguageType eLnge)
{
    if (sFormatString.isEmpty())                       // no empty string
        return false;
    eLnge = rCurrentLanguage.ImpResolveLanguage(eLnge);
    rCurrentLanguage.ChangeIntl(eLnge);
    eLnge = rCurrentLanguage.ActLnge;
    bool bEnglish = (eLnge == LANGUAGE_ENGLISH_US);

    OUString aFormatStringUpper( rCurrentLanguage.xCharClass->uppercase( sFormatString ) );
    sal_uInt32 nCLOffset = rFuncs.mGetCLOffset(rCurrentLanguage, rNatNum, eLnge); // create new standard formats if necessary (and allowed)
    sal_uInt32 nKey = rFormatData.ImpIsEntry(aFormatStringUpper, nCLOffset, eLnge);
    if ( nKey != NUMBERFORMAT_ENTRY_NOT_FOUND )
    {
        // Target format present
        GetOutputString(rCurrentLanguage, rFormatData, rNatNum, rFuncs,
                        fPreviewNumber, nKey, sOutString, ppColor, false);
        return true;
    }

    std::optional<SvNumberformat> pEntry;
    sal_Int32 nCheckPos = -1;
    OUString sTmpString;

    if ( bEnglish )
    {
        sTmpString = sFormatString;
        pEntry.emplace( sTmpString, rCurrentLanguage.pFormatScanner.get(),
                        rCurrentLanguage.pStringScanner.get(), rNatNum, nCheckPos, eLnge );
    }
    else
    {
        nCLOffset = rFuncs.mGetCLOffset(rCurrentLanguage, rNatNum, LANGUAGE_ENGLISH_US);
        nKey = rFormatData.ImpIsEntry(aFormatStringUpper, nCLOffset, LANGUAGE_ENGLISH_US);
        bool bEnglishFormat = (nKey != NUMBERFORMAT_ENTRY_NOT_FOUND);

        // Try English -> other or convert english to other
        LanguageType eFormatLang = LANGUAGE_ENGLISH_US;
        rCurrentLanguage.pFormatScanner->SetConvertMode( LANGUAGE_ENGLISH_US, eLnge, false, false);
        sTmpString = sFormatString;
        pEntry.emplace( sTmpString, rCurrentLanguage.pFormatScanner.get(),
                        rCurrentLanguage.pStringScanner.get(), rNatNum, nCheckPos, eFormatLang );
        rCurrentLanguage.pFormatScanner->SetConvertMode( false );
        rCurrentLanguage.ChangeIntl(eLnge);

        if ( !bEnglishFormat )
        {
            if ( nCheckPos != 0 || rCurrentLanguage.xTransliteration->isEqual( sFormatString,
                                                              pEntry->GetFormatstring() ) )
            {
                // other Format
                // Force locale's keywords.
                rCurrentLanguage.pFormatScanner->ChangeIntl( ImpSvNumberformatScan::KeywordLocalization::LocaleLegacy );
                sTmpString = sFormatString;
                pEntry.emplace( sTmpString, rCurrentLanguage.pFormatScanner.get(),
                                rCurrentLanguage.pStringScanner.get(), rNatNum, nCheckPos, eLnge );
            }
            else
            {
                // verify english
                sal_Int32 nCheckPos2 = -1;
                // try other --> english
                eFormatLang = eLnge;
                rCurrentLanguage.pFormatScanner->SetConvertMode( eLnge, LANGUAGE_ENGLISH_US, false, false);
                sTmpString = sFormatString;
                SvNumberformat aEntry2( sTmpString, rCurrentLanguage.pFormatScanner.get(),
                                        rCurrentLanguage.pStringScanner.get(), rNatNum, nCheckPos2, eFormatLang );
                rCurrentLanguage.pFormatScanner->SetConvertMode( false );
                rCurrentLanguage.ChangeIntl(eLnge);
                if ( nCheckPos2 == 0 && !rCurrentLanguage.xTransliteration->isEqual( sFormatString,
                                                                    aEntry2.GetFormatstring() ) )
                {
                    // other Format
                    // Force locale's keywords.
                    rCurrentLanguage.pFormatScanner->ChangeIntl( ImpSvNumberformatScan::KeywordLocalization::LocaleLegacy );
                    sTmpString = sFormatString;
                    pEntry.emplace( sTmpString, rCurrentLanguage.pFormatScanner.get(),
                                    rCurrentLanguage.pStringScanner.get(), rNatNum, nCheckPos, eLnge );
                }
            }
        }
    }

    if (nCheckPos == 0)                                 // String ok
    {
        rFuncs.mGetCLOffset(rCurrentLanguage, rNatNum, eLnge); // create new standard formats if necessary
        pEntry->GetOutputString( fPreviewNumber, sOutString, ppColor, rNatNum, rCurrentLanguage );
        return true;
    }
    return false;
}

bool SvNumberFormatter::GetPreviewStringGuess( const OUString& sFormatString,
                                               double fPreviewNumber,
                                               OUString& sOutString,
                                               const Color** ppColor,
                                               LanguageType eLnge )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return SvNFEngine::GetPreviewStringGuess(m_aCurrentLanguage, m_aFormatData,
                                             GetNatNum(), m_aRWPolicy,
                                             sFormatString, fPreviewNumber,
                                             sOutString, ppColor, eLnge);
}

bool SvNFEngine::GetPreviewString(SvNFLanguageData& rCurrentLanguage,
                                  const SvNFFormatData& rFormatData,
                                  const NativeNumberWrapper& rNatNum,
                                  const Accessor& rFuncs,
                                  const OUString& sFormatString,
                                  const OUString& sPreviewString,
                                  OUString& sOutString,
                                  const Color** ppColor,
                                  LanguageType eLnge)
{
    if (sFormatString.isEmpty())               // no empty string
        return false;
    eLnge = rCurrentLanguage.ImpResolveLanguage(eLnge);
    rCurrentLanguage.ChangeIntl(eLnge);        // switch if needed
    eLnge = rCurrentLanguage.ActLnge;
    sal_Int32 nCheckPos = -1;
    OUString sTmpString = sFormatString;
    SvNumberformat aEntry( sTmpString,
                           rCurrentLanguage.pFormatScanner.get(),
                           rCurrentLanguage.pStringScanner.get(),
                           rNatNum,
                           nCheckPos,
                           eLnge);
    if (nCheckPos == 0)                          // String ok
    {
        // May have to create standard formats for this locale.
        sal_uInt32 nCLOffset = rFuncs.mGetCLOffset(rCurrentLanguage, rNatNum, eLnge);
        sal_uInt32 nKey = rFormatData.ImpIsEntry( aEntry.GetFormatstring(), nCLOffset, eLnge);
        if (nKey != NUMBERFORMAT_ENTRY_NOT_FOUND)       // already present
        {
            GetOutputString(rCurrentLanguage, rFormatData,
                            sPreviewString, nKey, sOutString, ppColor, false);
        }
        else
        {
            // If the format is valid but not a text format and does not
            // include a text subformat, an empty string would result. Same as
            // in SvNumberFormatter::GetOutputString()
            if (aEntry.IsTextFormat() || aEntry.HasTextFormat())
            {
                aEntry.GetOutputString( sPreviewString, sOutString, ppColor);
            }
            else
            {
                *ppColor = nullptr;
                sOutString = sPreviewString;
            }
        }
        return true;
    }
    else
    {
        return false;
    }
}

bool SvNumberFormatter::GetPreviewString( const OUString& sFormatString,
                                          const OUString& sPreviewString,
                                          OUString& sOutString,
                                          const Color** ppColor,
                                          LanguageType eLnge )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return SvNFEngine::GetPreviewString(m_aCurrentLanguage, m_aFormatData,
                                        GetNatNum(), m_aRWPolicy,
                                        sFormatString, sPreviewString,
                                        sOutString, ppColor, eLnge);
}

sal_uInt32 SvNumberFormatter::TestNewString(const OUString& sFormatString,
                                            LanguageType eLnge)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    if (sFormatString.isEmpty())                       // no empty string
    {
        return NUMBERFORMAT_ENTRY_NOT_FOUND;
    }
    if (eLnge == LANGUAGE_DONTKNOW)
    {
        eLnge = IniLnge;
    }
    ChangeIntl(eLnge);                                  // change locale if necessary
    eLnge = m_aCurrentLanguage.ActLnge;
    sal_uInt32 nRes;
    sal_Int32 nCheckPos = -1;
    OUString sTmpString = sFormatString;
    SvNumberformat aEntry(sTmpString,
                          m_aCurrentLanguage.pFormatScanner.get(),
                          m_aCurrentLanguage.pStringScanner.get(),
                          GetNatNum(),
                          nCheckPos,
                          eLnge);
    if (nCheckPos == 0)                                 // String ok
    {
        sal_uInt32 CLOffset = m_aFormatData.ImpGenerateCL(m_aCurrentLanguage, GetNatNum(), eLnge);     // create new standard formats if necessary
        nRes = ImpIsEntry(aEntry.GetFormatstring(),CLOffset, eLnge);
                                                        // already present?
    }
    else
    {
        nRes = NUMBERFORMAT_ENTRY_NOT_FOUND;
    }
    return nRes;
}

SvNumberformat* SvNFFormatData::ImpInsertFormat(SvNFLanguageData& rCurrentLanguage,
                                                const NativeNumberWrapper& rNatNum,
                                                const css::i18n::NumberFormatCode& rCode,
                                                sal_uInt32 nPos, bool bAfterChangingSystemCL,
                                                sal_Int16 nOrgIndex)
{
    SAL_WARN_IF( NF_INDEX_TABLE_RESERVED_START <= rCode.Index && rCode.Index < NF_INDEX_TABLE_ENTRIES,
            "svl.numbers", "i18npool locale '" << rCurrentLanguage.aLanguageTag.getBcp47() <<
            "' uses reserved formatIndex value " << rCode.Index << ", next free: " << NF_INDEX_TABLE_ENTRIES <<
            "  Please see description in include/svl/zforlist.hxx at end of enum NfIndexTableOffset");
    assert( (rCode.Index < NF_INDEX_TABLE_RESERVED_START || NF_INDEX_TABLE_ENTRIES <= rCode.Index) &&
            "reserved formatIndex, see warning above");

    OUString aCodeStr( rCode.Code );
    if ( rCode.Index < NF_INDEX_TABLE_RESERVED_START &&
            rCode.Usage == css::i18n::KNumberFormatUsage::CURRENCY &&
            rCode.Index != NF_CURRENCY_1000DEC2_CCC )
    {   // strip surrounding [$...] on automatic currency
        if ( aCodeStr.indexOf( "[$" ) >= 0)
            aCodeStr = SvNumberformat::StripNewCurrencyDelimiters( aCodeStr );
        else
        {
            if (LocaleDataWrapper::areChecksEnabled() &&
                    rCode.Index != NF_CURRENCY_1000DEC2_CCC )
            {
                OUString aMsg = "SvNumberFormatter::ImpInsertFormat: no [$...] on currency format code, index " +
                                OUString::number( rCode.Index) +
                                ":\n" +
                                rCode.Code;
                LocaleDataWrapper::outputCheckMessage( rCurrentLanguage.xLocaleData->appendLocaleInfo( aMsg));
            }
        }
    }
    sal_Int32 nCheckPos = 0;
    std::unique_ptr<SvNumberformat> pFormat(new SvNumberformat(aCodeStr,
                                                               rCurrentLanguage.pFormatScanner.get(),
                                                               rCurrentLanguage.pStringScanner.get(),
                                                               rNatNum,
                                                               nCheckPos,
                                                               rCurrentLanguage.ActLnge));
    if (nCheckPos != 0)
    {
        if (LocaleDataWrapper::areChecksEnabled())
        {
            OUString aMsg = "SvNumberFormatter::ImpInsertFormat: bad format code, index " +
                            OUString::number( rCode.Index ) +
                            "\n" +
                            rCode.Code;
            LocaleDataWrapper::outputCheckMessage( rCurrentLanguage.xLocaleData->appendLocaleInfo( aMsg));
        }
        return nullptr;
    }
    if ( rCode.Index >= NF_INDEX_TABLE_RESERVED_START )
    {
        sal_uInt32 nCLOffset = nPos - (nPos % SV_COUNTRY_LANGUAGE_OFFSET);
        sal_uInt32 nKey = ImpIsEntry( aCodeStr, nCLOffset, rCurrentLanguage.ActLnge );
        if ( nKey != NUMBERFORMAT_ENTRY_NOT_FOUND )
        {
            // If bAfterChangingSystemCL there will definitely be some dups,
            // don't cry then.
            if (LocaleDataWrapper::areChecksEnabled() && !bAfterChangingSystemCL)
            {
                // Test for duplicate indexes in locale data.
                switch ( nOrgIndex )
                {
                // These may be dups of integer versions for locales where
                // currencies have no decimals like Italian Lira.
                case NF_CURRENCY_1000DEC2 :         // NF_CURRENCY_1000INT
                case NF_CURRENCY_1000DEC2_RED :     // NF_CURRENCY_1000INT_RED
                case NF_CURRENCY_1000DEC2_DASHED :  // NF_CURRENCY_1000INT_RED
                    break;
                default:
                {
                    OUString aMsg = "SvNumberFormatter::ImpInsertFormat: dup format code, index "
                                  + OUString::number( rCode.Index )
                                  + "\n"
                                  + rCode.Code;
                    LocaleDataWrapper::outputCheckMessage( rCurrentLanguage.xLocaleData->appendLocaleInfo( aMsg));
                }
                }
            }
            return nullptr;
        }
        else if ( nPos - nCLOffset >= SV_COUNTRY_LANGUAGE_OFFSET )
        {
            if (LocaleDataWrapper::areChecksEnabled())
            {
                OUString aMsg =  "SvNumberFormatter::ImpInsertFormat: too many format codes, index "
                              + OUString::number( rCode.Index )
                              + "\n"
                              +  rCode.Code;
                LocaleDataWrapper::outputCheckMessage( rCurrentLanguage.xLocaleData->appendLocaleInfo( aMsg));
            }
            return nullptr;
        }
    }
    auto pFormat2 = pFormat.get();
    if ( !aFTable.emplace( nPos, std::move(pFormat) ).second )
    {
        if (LocaleDataWrapper::areChecksEnabled())
        {
            OUString aMsg = "ImpInsertFormat: can't insert number format key pos: "
                          + OUString::number( nPos )
                          + ", code index "
                          + OUString::number( rCode.Index )
                          + "\n"
                          + rCode.Code;
            LocaleDataWrapper::outputCheckMessage( rCurrentLanguage.xLocaleData->appendLocaleInfo( aMsg));
        }
        else
        {
            SAL_WARN( "svl.numbers", "SvNumberFormatter::ImpInsertFormat: dup position");
        }
        return nullptr;
    }
    if ( rCode.Default )
        pFormat2->SetStandard();
    if ( !rCode.DefaultName.isEmpty() )
        pFormat2->SetComment( rCode.DefaultName );
    return pFormat2;
}

void SvNumberFormatter::GetFormatSpecialInfo(sal_uInt32 nFormat,
                                             bool& bThousand,
                                             bool& IsRed,
                                             sal_uInt16& nPrecision,
                                             sal_uInt16& nLeadingCnt)

{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    SvNumberformat* pFormat = m_aFormatData.GetFormatEntry( nFormat );
    if (pFormat)
        pFormat->GetFormatSpecialInfo(bThousand, IsRed,
                                      nPrecision, nLeadingCnt);
    else
    {
        bThousand = false;
        IsRed = false;
        nPrecision = m_aCurrentLanguage.pFormatScanner->GetStandardPrec();
        nLeadingCnt = 0;
    }
}

sal_uInt16 SvNFEngine::GetFormatPrecision(const SvNFLanguageData& rCurrentLanguage,
                                          const SvNFFormatData& rFormatData,
                                          sal_uInt32 nFormat)
{
    const SvNumberformat* pFormat = rFormatData.GetFormatEntry(nFormat);
    if (pFormat)
        return pFormat->GetFormatPrecision();
    return rCurrentLanguage.pFormatScanner->GetStandardPrec();
}

sal_uInt16 SvNumberFormatter::GetFormatPrecision( sal_uInt32 nFormat )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return SvNFEngine::GetFormatPrecision(m_aCurrentLanguage, m_aFormatData, nFormat);
}

sal_uInt16 SvNumberFormatter::GetFormatIntegerDigits( sal_uInt32 nFormat ) const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    const SvNumberformat* pFormat = m_aFormatData.GetFormatEntry( nFormat );
    if ( pFormat )
        return pFormat->GetFormatIntegerDigits();
    else
        return 1;
}

OUString SvNFEngine::GetFormatDecimalSep(SvNFLanguageData& rCurrentLanguage,
                                         const SvNFFormatData& rFormatData,
                                         sal_uInt32 nFormat)
{
    const SvNumberformat* pFormat = rFormatData.GetFormatEntry(nFormat);
    if (!pFormat)
        return rCurrentLanguage.GetNumDecimalSep();
    return rCurrentLanguage.GetLangDecimalSep(pFormat->GetLanguage());
}

OUString SvNumberFormatter::GetFormatDecimalSep( sal_uInt32 nFormat )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return SvNFEngine::GetFormatDecimalSep(m_aCurrentLanguage,
                                           m_aFormatData,
                                           nFormat);
}

bool SvNumberFormatter::IsNatNum12( sal_uInt32 nFIndex ) const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    const SvNumberformat* pFormat = m_aFormatData.GetFormatEntry( nFIndex );

    return pFormat && pFormat->GetNatNumModifierString().startsWith( "[NatNum12" );
}

sal_uInt32 SvNumberFormatter::GetFormatSpecialInfo( const OUString& rFormatString,
                                                    bool& bThousand, bool& IsRed, sal_uInt16& nPrecision,
                                                    sal_uInt16& nLeadingCnt, LanguageType eLnge )

{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    if (eLnge == LANGUAGE_DONTKNOW)
    {
        eLnge = IniLnge;
    }
    ChangeIntl(eLnge);                                  // change locale if necessary
    eLnge = m_aCurrentLanguage.ActLnge;
    OUString aTmpStr( rFormatString );
    sal_Int32 nCheckPos = 0;
    SvNumberformat aFormat( aTmpStr, m_aCurrentLanguage.pFormatScanner.get(),
                            m_aCurrentLanguage.pStringScanner.get(), GetNatNum(), nCheckPos, eLnge );
    if ( nCheckPos == 0 )
    {
        aFormat.GetFormatSpecialInfo( bThousand, IsRed, nPrecision, nLeadingCnt );
    }
    else
    {
        bThousand = false;
        IsRed = false;
        nPrecision = m_aCurrentLanguage.pFormatScanner->GetStandardPrec();
        nLeadingCnt = 0;
    }
    return nCheckPos;
}

OUString SvNFFormatData::GetCalcCellReturn(sal_uInt32 nFormat) const
{
    const SvNumberformat* pFormat = GetFormatEntry(nFormat);
    if (!pFormat)
        return u"G"_ustr;

    OUString    aStr;
    bool        bAppendPrec = true;
    sal_uInt16  nPrec, nLeading;
    bool        bThousand, bIsRed;
    pFormat->GetFormatSpecialInfo( bThousand, bIsRed, nPrec, nLeading );

    switch (pFormat->GetMaskedType())
    {
        case SvNumFormatType::NUMBER:
            if (bThousand)
                aStr = ",";
            else
                aStr = "F";
        break;
        case SvNumFormatType::CURRENCY:
            aStr = "C";
        break;
        case SvNumFormatType::SCIENTIFIC:
            aStr = "S";
        break;
        case SvNumFormatType::PERCENT:
            aStr = "P";
        break;
        default:
            {
                bAppendPrec = false;
                switch (SvNumberFormatter::GetIndexTableOffset(nFormat))
                {
                    case NF_DATE_SYSTEM_SHORT:
                    case NF_DATE_SYS_DMMMYY:
                    case NF_DATE_SYS_DDMMYY:
                    case NF_DATE_SYS_DDMMYYYY:
                    case NF_DATE_SYS_DMMMYYYY:
                    case NF_DATE_DIN_DMMMYYYY:
                    case NF_DATE_SYS_DMMMMYYYY:
                    case NF_DATE_DIN_DMMMMYYYY: aStr = "D1"; break;
                    case NF_DATE_SYS_DDMMM:     aStr = "D2"; break;
                    case NF_DATE_SYS_MMYY:      aStr = "D3"; break;
                    case NF_DATETIME_SYSTEM_SHORT_HHMM:
                    case NF_DATETIME_SYS_DDMMYYYY_HHMM:
                    case NF_DATETIME_SYS_DDMMYYYY_HHMMSS:
                                                aStr = "D4"; break;
                    case NF_DATE_DIN_MMDD:      aStr = "D5"; break;
                    case NF_TIME_HHMMSSAMPM:    aStr = "D6"; break;
                    case NF_TIME_HHMMAMPM:      aStr = "D7"; break;
                    case NF_TIME_HHMMSS:        aStr = "D8"; break;
                    case NF_TIME_HHMM:          aStr = "D9"; break;
                    default:                    aStr = "G";
                }
            }
    }

    if (bAppendPrec)
        aStr += OUString::number(nPrec);

    if (pFormat->GetColor( 1 ))
        aStr += "-";    // negative color

    /* FIXME: this probably should not match on literal strings and only be
     * performed on number or currency formats, but it is what Calc originally
     * implemented. */
    if (pFormat->GetFormatstring().indexOf('(') != -1)
        aStr += "()";

    return aStr;
}

OUString SvNumberFormatter::GetCalcCellReturn( sal_uInt32 nFormat ) const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return m_aFormatData.GetCalcCellReturn(nFormat);
}

// Return the index in a sequence of format codes matching an enum of
// NfIndexTableOffset. If not found 0 is returned. If the sequence doesn't
// contain any format code elements a default element is created and inserted.
sal_Int32 SvNFFormatData::ImpGetFormatCodeIndex(const SvNFLanguageData& rCurrentLanguage,
                                                css::uno::Sequence<css::i18n::NumberFormatCode>& rSeq,
                                                const NfIndexTableOffset nTabOff)
{
    auto pSeq = std::find_if(std::cbegin(rSeq), std::cend(rSeq),
        [nTabOff](const css::i18n::NumberFormatCode& rCode) { return rCode.Index == nTabOff; });
    if (pSeq != std::cend(rSeq))
        return static_cast<sal_Int32>(std::distance(std::cbegin(rSeq), pSeq));
    if (LocaleDataWrapper::areChecksEnabled() && (nTabOff < NF_CURRENCY_START
                || NF_CURRENCY_END < nTabOff || nTabOff == NF_CURRENCY_1000INT
                || nTabOff == NF_CURRENCY_1000INT_RED
                || nTabOff == NF_CURRENCY_1000DEC2_CCC))
    {   // currency entries with decimals might not exist, e.g. Italian Lira
        OUString aMsg = "SvNumberFormatter::ImpGetFormatCodeIndex: not found: "
                      + OUString::number( nTabOff );
        LocaleDataWrapper::outputCheckMessage( rCurrentLanguage.xLocaleData->appendLocaleInfo(aMsg));
    }
    if ( rSeq.hasElements() )
    {
        // look for a preset default
        pSeq = std::find_if(std::cbegin(rSeq), std::cend(rSeq),
            [](const css::i18n::NumberFormatCode& rCode) { return rCode.Default; });
        if (pSeq != std::cend(rSeq))
            return static_cast<sal_Int32>(std::distance(std::cbegin(rSeq), pSeq));
        // currencies are special, not all format codes must exist, but all
        // builtin number format key index positions must have a format assigned
        if ( NF_CURRENCY_START <= nTabOff && nTabOff <= NF_CURRENCY_END )
        {
            // look for a format with decimals
            pSeq = std::find_if(std::cbegin(rSeq), std::cend(rSeq),
                [](const css::i18n::NumberFormatCode& rCode) { return rCode.Index == NF_CURRENCY_1000DEC2; });
            if (pSeq != std::cend(rSeq))
                return static_cast<sal_Int32>(std::distance(std::cbegin(rSeq), pSeq));
            // last resort: look for a format without decimals
            pSeq = std::find_if(std::cbegin(rSeq), std::cend(rSeq),
                [](const css::i18n::NumberFormatCode& rCode) { return rCode.Index == NF_CURRENCY_1000INT; });
            if (pSeq != std::cend(rSeq))
                return static_cast<sal_Int32>(std::distance(std::cbegin(rSeq), pSeq));
        }
    }
    else
    {   // we need at least _some_ format
        rSeq = { css::i18n::NumberFormatCode() };
        rSeq.getArray()[0].Code = "0" + rCurrentLanguage.GetNumDecimalSep() + "############";
    }
    return 0;
}

// Adjust a sequence of format codes to contain only one (THE) default
// instead of multiple defaults for short/medium/long types.
// If there is no medium but a short and a long default the long is taken.
// Non-PRODUCT version may check locale data for matching defaults in one
// FormatElement group.
void SvNFFormatData::ImpAdjustFormatCodeDefault(const SvNFLanguageData& rCurrentLanguage,
                                                css::i18n::NumberFormatCode * pFormatArr,
                                                sal_Int32 nCnt)
{
    if ( !nCnt )
        return;
    if (LocaleDataWrapper::areChecksEnabled())
    {
        // check the locale data for correctness
        OUStringBuffer aMsg;
        sal_Int32 nElem, nShort, nMedium, nLong, nShortDef, nMediumDef, nLongDef;
        nShort = nMedium = nLong = nShortDef = nMediumDef = nLongDef = -1;
        for ( nElem = 0; nElem < nCnt; nElem++ )
        {
            switch ( pFormatArr[nElem].Type )
            {
            case i18n::KNumberFormatType::SHORT :
                nShort = nElem;
                break;
            case i18n::KNumberFormatType::MEDIUM :
                nMedium = nElem;
                break;
            case i18n::KNumberFormatType::LONG :
                nLong = nElem;
                break;
            default:
                aMsg.append("unknown type");
            }
            if ( pFormatArr[nElem].Default )
            {
                switch ( pFormatArr[nElem].Type )
                {
                case i18n::KNumberFormatType::SHORT :
                    if ( nShortDef != -1 )
                        aMsg.append("dupe short type default");
                    nShortDef = nElem;
                    break;
                case i18n::KNumberFormatType::MEDIUM :
                    if ( nMediumDef != -1 )
                        aMsg.append("dupe medium type default");
                    nMediumDef = nElem;
                    break;
                case i18n::KNumberFormatType::LONG :
                    if ( nLongDef != -1 )
                        aMsg.append("dupe long type default");
                    nLongDef = nElem;
                    break;
                }
            }
            if (!aMsg.isEmpty())
            {
                aMsg.insert(0, "SvNumberFormatter::ImpAdjustFormatCodeDefault: ");
                aMsg.append("\nXML locale data FormatElement formatindex: "
                    + OUString::number(static_cast<sal_Int32>(pFormatArr[nElem].Index)));
                LocaleDataWrapper::outputCheckMessage(rCurrentLanguage.xLocaleData->appendLocaleInfo(aMsg));
                aMsg.setLength(0);
            }
        }
        if ( nShort != -1 && nShortDef == -1 )
            aMsg.append("no short type default  ");
        if ( nMedium != -1 && nMediumDef == -1 )
            aMsg.append("no medium type default  ");
        if ( nLong != -1 && nLongDef == -1 )
            aMsg.append("no long type default  ");
        if (!aMsg.isEmpty())
        {
            aMsg.insert(0, "SvNumberFormatter::ImpAdjustFormatCodeDefault: ");
            aMsg.append("\nXML locale data FormatElement group of: ");
            LocaleDataWrapper::outputCheckMessage(
                rCurrentLanguage.xLocaleData->appendLocaleInfo(Concat2View(aMsg + pFormatArr[0].NameID)));
            aMsg.setLength(0);
        }
    }
    // find the default (medium preferred, then long) and reset all other defaults
    sal_Int32 nElem, nDef, nMedium;
    nDef = nMedium = -1;
    for ( nElem = 0; nElem < nCnt; nElem++ )
    {
        if ( pFormatArr[nElem].Default )
        {
            switch ( pFormatArr[nElem].Type )
            {
            case i18n::KNumberFormatType::MEDIUM :
                nDef = nMedium = nElem;
                break;
            case i18n::KNumberFormatType::LONG :
                if ( nMedium == -1 )
                    nDef = nElem;
                [[fallthrough]];
            default:
                if ( nDef == -1 )
                    nDef = nElem;
                pFormatArr[nElem].Default = false;
            }
        }
    }
    if ( nDef == -1 )
        nDef = 0;
    pFormatArr[nDef].Default = true;
}

SvNumberformat* SvNFFormatData::GetEntry(sal_uInt32 nKey) const
{
    auto it = aFTable.find( nKey);
    if (it != aFTable.end())
        return it->second.get();
    return nullptr;
}

SvNumberformat* SvNFFormatData::GetFormatEntry(sal_uInt32 nKey)
{
    return GetEntry(nKey);
}

const SvNumberformat* SvNFFormatData::GetFormatEntry(sal_uInt32 nKey) const
{
    return GetEntry(nKey);
}

SvNumFormatType SvNFFormatData::GetType(sal_uInt32 nFIndex) const
{
    SvNumFormatType eType;
    const SvNumberformat* pFormat = GetFormatEntry(nFIndex);
    if (!pFormat)
        eType = SvNumFormatType::UNDEFINED;
    else
    {
        eType = pFormat->GetMaskedType();
        if (eType == SvNumFormatType::ALL)
            eType = SvNumFormatType::DEFINED;
    }
    return eType;
}

const SvNumberformat* SvNumberFormatter::GetEntry( sal_uInt32 nKey ) const
{
    ::osl::MutexGuard aGuard(GetInstanceMutex());
    return m_aFormatData.GetFormatEntry(nKey);
}

const SvNumberformat* SvNumberFormatter::GetSubstitutedEntry( sal_uInt32 nKey, sal_uInt32 & o_rNewKey )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    // A tad ugly, but GetStandardFormat() and GetFormatIndex() in
    // ImpSubstituteEntry() may have to add the LANGUAGE_SYSTEM formats if not
    // already present (which in practice most times they are).
    return ImpSubstituteEntry(m_aFormatData.GetFormatEntry(nKey), &o_rNewKey);
}

const SvNumberformat* SvNumberFormatter::ImpSubstituteEntry( const SvNumberformat* pFormat, sal_uInt32 * o_pRealKey )
{
    return ::ImpSubstituteEntry(m_aCurrentLanguage, m_aFormatData, GetNatNum(), m_aRWPolicy, pFormat, o_pRealKey);
}

void SvNFFormatData::ImpGenerateFormats(SvNFLanguageData& rCurrentLanguage, const NativeNumberWrapper& rNatNum, sal_uInt32 CLOffset, bool bNoAdditionalFormats )
{
    bool bOldConvertMode = rCurrentLanguage.pFormatScanner->GetConvertMode();
    if (bOldConvertMode)
    {
        rCurrentLanguage.pFormatScanner->SetConvertMode(false);      // switch off for this function
    }

    css::lang::Locale aLocale = rCurrentLanguage.GetLanguageTag().getLocale();
    css::uno::Reference< css::i18n::XNumberFormatCode > xNFC = i18n::NumberFormatMapper::create(rCurrentLanguage.GetComponentContext());
    sal_Int32 nIdx;

    // Number
    uno::Sequence< i18n::NumberFormatCode > aFormatSeq = xNFC->getAllFormatCode( i18n::KNumberFormatUsage::FIXED_NUMBER, aLocale );
    ImpAdjustFormatCodeDefault(rCurrentLanguage, aFormatSeq.getArray(), aFormatSeq.getLength() );

    // General
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_NUMBER_STANDARD );
    SvNumberformat* pStdFormat = ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
            CLOffset + ZF_STANDARD /* NF_NUMBER_STANDARD */ );
    if (pStdFormat)
    {
        // This is _the_ standard format.
        if (LocaleDataWrapper::areChecksEnabled() && pStdFormat->GetType() != SvNumFormatType::NUMBER)
        {
            LocaleDataWrapper::outputCheckMessage( rCurrentLanguage.xLocaleData->
                                                   appendLocaleInfo( u"SvNumberFormatter::ImpGenerateFormats: General format not NUMBER"));
        }
        pStdFormat->SetType( SvNumFormatType::NUMBER );
        pStdFormat->SetStandard();
        pStdFormat->SetLastInsertKey( SV_MAX_COUNT_STANDARD_FORMATS, SvNumberformat::FormatterPrivateAccess() );
    }
    else
    {
        if (LocaleDataWrapper::areChecksEnabled())
        {
            LocaleDataWrapper::outputCheckMessage( rCurrentLanguage.xLocaleData->
                                                   appendLocaleInfo( u"SvNumberFormatter::ImpGenerateFormats: General format not insertable, nothing will work"));
        }
    }

    {
        // Boolean
        OUString aFormatCode = rCurrentLanguage.pFormatScanner->GetBooleanString();
        sal_Int32 nCheckPos = 0;

        std::unique_ptr<SvNumberformat> pNewFormat(new SvNumberformat( aFormatCode, rCurrentLanguage.pFormatScanner.get(),
                                                                       rCurrentLanguage.pStringScanner.get(), rNatNum, nCheckPos, rCurrentLanguage.ActLnge ));
        pNewFormat->SetType(SvNumFormatType::LOGICAL);
        pNewFormat->SetStandard();
        if ( !aFTable.emplace(CLOffset + ZF_STANDARD_LOGICAL /* NF_BOOLEAN */,
                            std::move(pNewFormat)).second )
        {
            SAL_WARN( "svl.numbers", "SvNumberFormatter::ImpGenerateFormats: dup position Boolean");
        }

        // Text
        aFormatCode = "@";
        pNewFormat.reset(new SvNumberformat( aFormatCode, rCurrentLanguage.pFormatScanner.get(),
                                             rCurrentLanguage.pStringScanner.get(), rNatNum, nCheckPos, rCurrentLanguage.ActLnge ));
        pNewFormat->SetType(SvNumFormatType::TEXT);
        pNewFormat->SetStandard();
        if ( !aFTable.emplace( CLOffset + ZF_STANDARD_TEXT /* NF_TEXT */,
                               std::move(pNewFormat)).second )
        {
            SAL_WARN( "svl.numbers", "SvNumberFormatter::ImpGenerateFormats: dup position Text");
        }
    }

    // 0
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_NUMBER_INT );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD+1 /* NF_NUMBER_INT */ );

    // 0.00
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_NUMBER_DEC2 );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD+2 /* NF_NUMBER_DEC2 */ );

    // #,##0
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_NUMBER_1000INT );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD+3 /* NF_NUMBER_1000INT */ );

    // #,##0.00
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_NUMBER_1000DEC2 );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD+4 /* NF_NUMBER_1000DEC2 */ );

    // #.##0,00 System country/language dependent
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_NUMBER_SYSTEM );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD+5 /* NF_NUMBER_SYSTEM */ );


    // Percent number
    aFormatSeq = xNFC->getAllFormatCode( i18n::KNumberFormatUsage::PERCENT_NUMBER, aLocale );
    ImpAdjustFormatCodeDefault(rCurrentLanguage, aFormatSeq.getArray(), aFormatSeq.getLength() );

    // 0%
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_PERCENT_INT );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_PERCENT /* NF_PERCENT_INT */ );

    // 0.00%
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_PERCENT_DEC2 );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_PERCENT+1 /* NF_PERCENT_DEC2 */ );


    // Currency. NO default standard option! Default is determined of locale
    // data default currency and format is generated if needed.
    aFormatSeq = xNFC->getAllFormatCode( i18n::KNumberFormatUsage::CURRENCY, aLocale );
    if (LocaleDataWrapper::areChecksEnabled())
    {
        // though no default desired here, test for correctness of locale data
        ImpAdjustFormatCodeDefault(rCurrentLanguage, aFormatSeq.getArray(), aFormatSeq.getLength() );
    }

    // #,##0
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_CURRENCY_1000INT );
     // Just copy the format, to avoid COW on sequence after each possible reallocation
    auto aFormat = aFormatSeq[nIdx];
    aFormat.Default = false;
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormat,
                     CLOffset + ZF_STANDARD_CURRENCY /* NF_CURRENCY_1000INT */ );

    // #,##0.00
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_CURRENCY_1000DEC2 );
    aFormat = aFormatSeq[nIdx];
    aFormat.Default = false;
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormat,
                     CLOffset + ZF_STANDARD_CURRENCY+1 /* NF_CURRENCY_1000DEC2 */ );

    // #,##0 negative red
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_CURRENCY_1000INT_RED );
    aFormat = aFormatSeq[nIdx];
    aFormat.Default = false;
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormat,
                     CLOffset + ZF_STANDARD_CURRENCY+2 /* NF_CURRENCY_1000INT_RED */ );

    // #,##0.00 negative red
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_CURRENCY_1000DEC2_RED );
    aFormat = aFormatSeq[nIdx];
    aFormat.Default = false;
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormat,
                     CLOffset + ZF_STANDARD_CURRENCY+3 /* NF_CURRENCY_1000DEC2_RED */ );

    // #,##0.00 USD
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_CURRENCY_1000DEC2_CCC );
    aFormat = aFormatSeq[nIdx];
    aFormat.Default = false;
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormat,
                     CLOffset + ZF_STANDARD_CURRENCY+4 /* NF_CURRENCY_1000DEC2_CCC */ );

    // #.##0,--
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_CURRENCY_1000DEC2_DASHED );
    aFormat = aFormatSeq[nIdx];
    aFormat.Default = false;
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormat,
                     CLOffset + ZF_STANDARD_CURRENCY+5 /* NF_CURRENCY_1000DEC2_DASHED */ );


    // Date
    aFormatSeq = xNFC->getAllFormatCode( i18n::KNumberFormatUsage::DATE, aLocale );
    ImpAdjustFormatCodeDefault(rCurrentLanguage, aFormatSeq.getArray(), aFormatSeq.getLength() );

    // DD.MM.YY   System
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_SYSTEM_SHORT );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE /* NF_DATE_SYSTEM_SHORT */ );

    // NN DD.MMM YY
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_DEF_NNDDMMMYY );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE+1 /* NF_DATE_DEF_NNDDMMMYY */ );

    // DD.MM.YY   def/System
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_SYS_MMYY );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE+2 /* NF_DATE_SYS_MMYY */ );

    // DD MMM
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_SYS_DDMMM );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE+3 /* NF_DATE_SYS_DDMMM */ );

    // MMMM
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_MMMM );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE+4 /* NF_DATE_MMMM */ );

    // QQ YY
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_QQJJ );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE+5 /* NF_DATE_QQJJ */ );

    // DD.MM.YYYY   was DD.MM.[YY]YY
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_SYS_DDMMYYYY );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE+6 /* NF_DATE_SYS_DDMMYYYY */ );

    // DD.MM.YY   def/System
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_SYS_DDMMYY );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE+7 /* NF_DATE_SYS_DDMMYY */ );

    // NNN, D. MMMM YYYY   System
    // Long day of week: "NNNN" instead of "NNN," because of compatibility
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_SYSTEM_LONG );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE+8 /* NF_DATE_SYSTEM_LONG */ );

    // Hard coded but system (regional settings) delimiters dependent long date formats

    // D. MMM YY   def/System
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_SYS_DMMMYY );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE+9 /* NF_DATE_SYS_DMMMYY */ );

    // D. MMM YYYY   def/System
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_SYS_DMMMYYYY );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE_SYS_DMMMYYYY /* NF_DATE_SYS_DMMMYYYY */ );

    // D. MMMM YYYY   def/System
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_SYS_DMMMMYYYY );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE_SYS_DMMMMYYYY /* NF_DATE_SYS_DMMMMYYYY */ );

    // NN, D. MMM YY   def/System
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_SYS_NNDMMMYY );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE_SYS_NNDMMMYY /* NF_DATE_SYS_NNDMMMYY */ );

    // NN, D. MMMM YYYY   def/System
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_SYS_NNDMMMMYYYY );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE_SYS_NNDMMMMYYYY /*  NF_DATE_SYS_NNDMMMMYYYY */ );

    // NNN, D. MMMM YYYY   def/System
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_SYS_NNNNDMMMMYYYY );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE_SYS_NNNNDMMMMYYYY /* NF_DATE_SYS_NNNNDMMMMYYYY */ );

    // Hard coded DIN (Deutsche Industrie Norm) and EN (European Norm) date formats

    // D. MMM. YYYY   DIN/EN
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_DIN_DMMMYYYY );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE_DIN_DMMMYYYY /* NF_DATE_DIN_DMMMYYYY */ );

    // D. MMMM YYYY   DIN/EN
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_DIN_DMMMMYYYY );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE_DIN_DMMMMYYYY /* NF_DATE_DIN_DMMMMYYYY */ );

    // MM-DD   DIN/EN
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_DIN_MMDD );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE_DIN_MMDD /* NF_DATE_DIN_MMDD */ );

    // YY-MM-DD   DIN/EN
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_DIN_YYMMDD );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE_DIN_YYMMDD /* NF_DATE_DIN_YYMMDD */ );

    // YYYY-MM-DD   DIN/EN/ISO
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATE_DIN_YYYYMMDD );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATE_DIN_YYYYMMDD /* NF_DATE_DIN_YYYYMMDD */ );


    // Time
    aFormatSeq = xNFC->getAllFormatCode( i18n::KNumberFormatUsage::TIME, aLocale );
    ImpAdjustFormatCodeDefault(rCurrentLanguage, aFormatSeq.getArray(), aFormatSeq.getLength() );

    // HH:MM
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_TIME_HHMM );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_TIME /* NF_TIME_HHMM */ );

    // HH:MM:SS
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_TIME_HHMMSS );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_TIME+1 /* NF_TIME_HHMMSS */ );

    // HH:MM AM/PM
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_TIME_HHMMAMPM );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_TIME+2 /* NF_TIME_HHMMAMPM */ );

    // HH:MM:SS AM/PM
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_TIME_HHMMSSAMPM );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_TIME+3 /* NF_TIME_HHMMSSAMPM */ );

    // [HH]:MM:SS
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_TIME_HH_MMSS );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_TIME+4 /* NF_TIME_HH_MMSS */ );

    // MM:SS,00
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_TIME_MMSS00 );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_TIME+5 /* NF_TIME_MMSS00 */ );

    // [HH]:MM:SS,00
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_TIME_HH_MMSS00 );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_TIME+6 /* NF_TIME_HH_MMSS00 */ );


    // DateTime
    aFormatSeq = xNFC->getAllFormatCode( i18n::KNumberFormatUsage::DATE_TIME, aLocale );
    ImpAdjustFormatCodeDefault(rCurrentLanguage, aFormatSeq.getArray(), aFormatSeq.getLength() );

    // DD.MM.YY HH:MM   System
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATETIME_SYSTEM_SHORT_HHMM );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATETIME /* NF_DATETIME_SYSTEM_SHORT_HHMM */ );

    // DD.MM.YYYY HH:MM:SS   System
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATETIME_SYS_DDMMYYYY_HHMMSS );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATETIME+1 /* NF_DATETIME_SYS_DDMMYYYY_HHMMSS */ );

    // DD.MM.YYYY HH:MM   System
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_DATETIME_SYS_DDMMYYYY_HHMM );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_DATETIME+2 /* NF_DATETIME_SYS_DDMMYYYY_HHMM */ );

    const NfKeywordTable & rKeyword = rCurrentLanguage.pFormatScanner->GetKeywords();
    i18n::NumberFormatCode aSingleFormatCode;
    aSingleFormatCode.Usage = i18n::KNumberFormatUsage::DATE_TIME;

    // YYYY-MM-DD HH:MM:SS   ISO (with blank instead of 'T')
    aSingleFormatCode.Code =
        rKeyword[NF_KEY_YYYY] + "-" +
        rKeyword[NF_KEY_MM] + "-" +
        rKeyword[NF_KEY_DD] + " " +
        rKeyword[NF_KEY_HH] + ":" +
        rKeyword[NF_KEY_MMI] + ":" +
        rKeyword[NF_KEY_SS];
    SvNumberformat* pFormat = ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_DATETIME+3 /* NF_DATETIME_ISO_YYYYMMDD_HHMMSS */ );
    assert(pFormat);

    // YYYY-MM-DD HH:MM:SS,000   ISO (with blank instead of 'T') and
    // milliseconds and locale's time decimal separator
    aSingleFormatCode.Code =
        rKeyword[NF_KEY_YYYY] + "-" +
        rKeyword[NF_KEY_MM] + "-" +
        rKeyword[NF_KEY_DD] + " " +
        rKeyword[NF_KEY_HH] + ":" +
        rKeyword[NF_KEY_MMI] + ":" +
        rKeyword[NF_KEY_SS] + rCurrentLanguage.GetLocaleData()->getTime100SecSep() +
        "000";
    pFormat = ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_DATETIME+4 /* NF_DATETIME_ISO_YYYYMMDD_HHMMSS000 */ );
    assert(pFormat);

    // YYYY-MM-DD"T"HH:MM:SS   ISO
    aSingleFormatCode.Code =
        rKeyword[NF_KEY_YYYY] + "-" +
        rKeyword[NF_KEY_MM] + "-" +
        rKeyword[NF_KEY_DD] + "\"T\"" +
        rKeyword[NF_KEY_HH] + ":" +
        rKeyword[NF_KEY_MMI] + ":" +
        rKeyword[NF_KEY_SS];
    pFormat = ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_DATETIME+5 /* NF_DATETIME_ISO_YYYYMMDDTHHMMSS */ );
    assert(pFormat);
    pFormat->SetComment(u"ISO 8601"_ustr);    // not to be localized

    // YYYY-MM-DD"T"HH:MM:SS,000   ISO with milliseconds and ',' or '.' decimal separator
    aSingleFormatCode.Code =
        rKeyword[NF_KEY_YYYY] + "-" +
        rKeyword[NF_KEY_MM] + "-" +
        rKeyword[NF_KEY_DD] + "\"T\"" +
        rKeyword[NF_KEY_HH] + ":" +
        rKeyword[NF_KEY_MMI] + ":" +
        rKeyword[NF_KEY_SS] + (rCurrentLanguage.GetLocaleData()->getTime100SecSep() == "." ? "." : ",") +
        "000";
    pFormat = ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_DATETIME+6 /* NF_DATETIME_ISO_YYYYMMDDTHHMMSS000 */ );
    assert(pFormat);
    pFormat->SetComment(u"ISO 8601"_ustr);    // not to be localized


    // Scientific number
    aFormatSeq = xNFC->getAllFormatCode( i18n::KNumberFormatUsage::SCIENTIFIC_NUMBER, aLocale );
    ImpAdjustFormatCodeDefault(rCurrentLanguage, aFormatSeq.getArray(), aFormatSeq.getLength() );

    // 0.00E+000
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_SCIENTIFIC_000E000 );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_SCIENTIFIC /* NF_SCIENTIFIC_000E000 */ );

    // 0.00E+00
    nIdx = ImpGetFormatCodeIndex(rCurrentLanguage, aFormatSeq, NF_SCIENTIFIC_000E00 );
    ImpInsertFormat(rCurrentLanguage, rNatNum, aFormatSeq[nIdx],
                     CLOffset + ZF_STANDARD_SCIENTIFIC+1 /* NF_SCIENTIFIC_000E00 */ );


    // Fraction number (no default option)
    aSingleFormatCode.Usage = i18n::KNumberFormatUsage::FRACTION_NUMBER;

     // # ?/?
    aSingleFormatCode.Code = "# ?/?";
    ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_FRACTION /* NF_FRACTION_1D */ );

    // # ??/??
    //! "??/" would be interpreted by the compiler as a trigraph for '\'
    aSingleFormatCode.Code = "# ?\?/?\?";
    ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_FRACTION+1 /* NF_FRACTION_2D */ );

    // # ???/???
    //! "??/" would be interpreted by the compiler as a trigraph for '\'
    aSingleFormatCode.Code = "# ?\?\?/?\?\?";
    ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_FRACTION+2 /* NF_FRACTION_3D */ );

    // # ?/2
    aSingleFormatCode.Code = "# ?/2";
    ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_FRACTION+3 /* NF_FRACTION_2 */ );

    // # ?/4
    aSingleFormatCode.Code = "# ?/4";
    ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_FRACTION+4 /* NF_FRACTION_4 */ );

    // # ?/8
    aSingleFormatCode.Code = "# ?/8";
    ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_FRACTION+5 /* NF_FRACTION_8 */ );

    // # ??/16
    aSingleFormatCode.Code = "# ?\?/16";
    ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_FRACTION+6 /* NF_FRACTION_16 */ );

    // # ??/10
    aSingleFormatCode.Code = "# ?\?/10";
    ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_FRACTION+7 /* NF_FRACTION_10 */ );

    // # ??/100
    aSingleFormatCode.Code = "# ?\?/100";
    ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_FRACTION+8 /* NF_FRACTION_100 */ );


    // Week of year
    aSingleFormatCode.Code = rKeyword[NF_KEY_WW];
    ImpInsertFormat(rCurrentLanguage, rNatNum, aSingleFormatCode,
                     CLOffset + ZF_STANDARD_DATE_WW /* NF_DATE_WW */ );

    // Now all additional format codes provided by I18N, but only if not
    // changing SystemCL, then they are appended last after user defined.
    if ( !bNoAdditionalFormats )
    {
        ImpGenerateAdditionalFormats(rCurrentLanguage, rNatNum, CLOffset, xNFC, false);
    }
    if (bOldConvertMode)
    {
        rCurrentLanguage.pFormatScanner->SetConvertMode(true);
    }
}

void SvNFFormatData::ImpGenerateAdditionalFormats(SvNFLanguageData& rCurrentLanguage,
            const NativeNumberWrapper& rNatNum, sal_uInt32 CLOffset,
            css::uno::Reference< css::i18n::XNumberFormatCode > const & rNumberFormatCode,
            bool bAfterChangingSystemCL )
{
    SvNumberformat* pStdFormat = GetFormatEntry( CLOffset + ZF_STANDARD );
    if ( !pStdFormat )
    {
        SAL_WARN( "svl.numbers", "ImpGenerateAdditionalFormats: no GENERAL format" );
        return ;
    }
    sal_uInt32 nPos = CLOffset + pStdFormat->GetLastInsertKey( SvNumberformat::FormatterPrivateAccess() );
    css::lang::Locale aLocale = rCurrentLanguage.GetLanguageTag().getLocale();

    // All currencies, this time with [$...] which was stripped in
    // ImpGenerateFormats for old "automatic" currency formats.
    uno::Sequence< i18n::NumberFormatCode > aFormatSeq = rNumberFormatCode->getAllFormatCode( i18n::KNumberFormatUsage::CURRENCY, aLocale );
    sal_Int32 nCodes = aFormatSeq.getLength();
    auto aNonConstRange = asNonConstRange(aFormatSeq);
    ImpAdjustFormatCodeDefault(rCurrentLanguage, aNonConstRange.begin(), nCodes);
    for ( i18n::NumberFormatCode& rFormat : aNonConstRange )
    {
        if ( nPos - CLOffset >= SV_COUNTRY_LANGUAGE_OFFSET )
        {
            SAL_WARN( "svl.numbers", "ImpGenerateAdditionalFormats: too many formats" );
            break;  // for
        }
        if ( rFormat.Index < NF_INDEX_TABLE_RESERVED_START &&
                rFormat.Index != NF_CURRENCY_1000DEC2_CCC )
        {   // Insert only if not already inserted, but internal index must be
            // above so ImpInsertFormat can distinguish it.
            sal_Int16 nOrgIndex = rFormat.Index;
            rFormat.Index = sal::static_int_cast< sal_Int16 >(
                rFormat.Index + nCodes + NF_INDEX_TABLE_ENTRIES);
            //! no default on currency
            bool bDefault = rFormat.Default;
            rFormat.Default = false;
            if ( SvNumberformat* pNewFormat = ImpInsertFormat(rCurrentLanguage, rNatNum, rFormat, nPos+1,
                        bAfterChangingSystemCL, nOrgIndex ) )
            {
                pNewFormat->SetAdditionalBuiltin();
                nPos++;
            }
            rFormat.Index = nOrgIndex;
            rFormat.Default = bDefault;
        }
    }

    // All additional format codes provided by I18N that are not old standard
    // index. Additional formats may define defaults, currently there is no
    // check if more than one default of a usage/type combination is provided,
    // like it is done for usage groups with ImpAdjustFormatCodeDefault().
    // There is no harm though, on first invocation ImpGetDefaultFormat() will
    // use the first default encountered.
    aFormatSeq = rNumberFormatCode->getAllFormatCodes( aLocale );
    for ( const auto& rFormat : std::as_const(aFormatSeq) )
    {
        if ( nPos - CLOffset >= SV_COUNTRY_LANGUAGE_OFFSET )
        {
            SAL_WARN( "svl.numbers", "ImpGenerateAdditionalFormats: too many formats" );
            break;  // for
        }
        if ( rFormat.Index >= NF_INDEX_TABLE_RESERVED_START )
        {
            if ( SvNumberformat* pNewFormat = ImpInsertFormat(rCurrentLanguage, rNatNum, rFormat, nPos+1,
                        bAfterChangingSystemCL ) )
            {
                pNewFormat->SetAdditionalBuiltin();
                nPos++;
            }
        }
    }

    pStdFormat->SetLastInsertKey( static_cast<sal_uInt16>(nPos - CLOffset), SvNumberformat::FormatterPrivateAccess() );
}

void SvNumberFormatter::ImpGenerateAdditionalFormats( sal_uInt32 CLOffset,
            css::uno::Reference< css::i18n::XNumberFormatCode > const & rNumberFormatCode )
{
    m_aFormatData.ImpGenerateAdditionalFormats(m_aCurrentLanguage, GetNatNum(), CLOffset, rNumberFormatCode, /*bAfterChangingSystemCL*/true);
}

namespace {

// return position of a special character
sal_Int32 ImpPosToken(const OUStringBuffer & sFormat, sal_Unicode token, sal_Int32 nStartPos = 0)
{
    sal_Int32 nLength = sFormat.getLength();
    for ( sal_Int32 i=nStartPos; i<nLength && i>=0 ; i++ )
    {
        switch(sFormat[i])
        {
            case '\"' : // skip text
                i = sFormat.indexOf('\"',i+1);
                break;
            case '['  : // skip condition
                i = sFormat.indexOf(']',i+1);
                break;
            case '\\' : // skip escaped character
                i++;
                break;
            case ';'  :
                if (token == ';')
                    return i;
                break;
            case 'e'  :
            case 'E'  :
                if (token == 'E')
                   return i; // if 'E' is outside "" and [] it must be the 'E' exponent
                break;
            default : break;
        }
        if ( i<0 )
            i--;
    }
    return -2;
}

}

OUString SvNFEngine::GenerateFormat(SvNFLanguageData& rCurrentLanguage,
                                    const SvNFFormatData& rFormatData,
                                    const NativeNumberWrapper& rNatNum, const Accessor& rFuncs,
                                    sal_uInt32 nIndex,
                                    LanguageType eLnge,
                                    bool bThousand,
                                    bool IsRed,
                                    sal_uInt16 nPrecision,
                                    sal_uInt16 nLeadingZeros)
{
    eLnge = rCurrentLanguage.ImpResolveLanguage(eLnge);
    const SvNumberformat* pFormat = rFormatData.GetFormatEntry( nIndex );
    const SvNumFormatType eType = (pFormat ? pFormat->GetMaskedType() : SvNumFormatType::UNDEFINED);

    rFuncs.mGetCLOffset(rCurrentLanguage, rNatNum, eLnge); // create new standard formats if necessary (and allowed)

    utl::DigitGroupingIterator aGrouping( rCurrentLanguage.xLocaleData->getDigitGrouping());
    // always group of 3 for Engineering notation
    const sal_Int32 nDigitsInFirstGroup = ( bThousand && (eType == SvNumFormatType::SCIENTIFIC) ) ? 3 : aGrouping.get();
    const OUString& rThSep = rCurrentLanguage.GetNumThousandSep();

    OUStringBuffer sString;
    using comphelper::string::padToLength;

    if (eType & SvNumFormatType::TIME)
    {
        assert(pFormat && "with !pFormat eType can only be SvNumFormatType::UNDEFINED");
        sString = pFormat->GetFormatStringForTimePrecision( nPrecision );
    }
    else if (nLeadingZeros == 0)
    {
        if (!bThousand)
            sString.append('#');
        else
        {
            if (eType == SvNumFormatType::SCIENTIFIC)
            {  // for scientific, bThousand is used for Engineering notation
                sString.append("###");
            }
            else
            {
                sString.append("#" + rThSep);
                padToLength(sString, sString.getLength() + nDigitsInFirstGroup, '#');
            }
        }
    }
    else
    {
        for (sal_uInt16 i = 0; i < nLeadingZeros; i++)
        {
            if (bThousand && i > 0 && i == aGrouping.getPos())
            {
                sString.insert(0, rThSep);
                aGrouping.advance();
            }
            sString.insert(0, '0');
        }
        if ( bThousand )
        {
            sal_Int32 nDigits = (eType == SvNumFormatType::SCIENTIFIC) ?  3*((nLeadingZeros-1)/3 + 1) : nDigitsInFirstGroup + 1;
            for (sal_Int32 i = nLeadingZeros; i < nDigits; i++)
            {
                if ( i % nDigitsInFirstGroup == 0 )
                    sString.insert(0, rThSep);
                sString.insert(0, '#');
            }
        }
    }
    if (nPrecision > 0 && eType != SvNumFormatType::FRACTION && !( eType & SvNumFormatType::TIME ) )
    {
        sString.append(rCurrentLanguage.GetNumDecimalSep());
        padToLength(sString, sString.getLength() + nPrecision, '0');
    }

    // Native Number
    const OUString sPosNatNumModifier = pFormat ? pFormat->GetNatNumModifierString( 0 ) : u""_ustr;
    const OUString sNegNatNumModifier = pFormat ?
            // if a negative format already exists, use its NatNum modifier
            // else use NatNum modifier of positive format
            ( pFormat->GetNumForString( 1, 0 )  ? pFormat->GetNatNumModifierString( 1 ) : sPosNatNumModifier )
            : u""_ustr;

    if (eType == SvNumFormatType::PERCENT)
    {
        sString.append( pFormat->GetPercentString() );
    }
    else if (eType == SvNumFormatType::SCIENTIFIC)
    {
      OUStringBuffer sOldFormatString(pFormat->GetFormatstring());
      sal_Int32 nIndexE = ImpPosToken( sOldFormatString, 'E' );
      if (nIndexE > -1)
      {
        sal_Int32 nIndexSep = ImpPosToken( sOldFormatString, ';', nIndexE );
        if (nIndexSep > nIndexE)
            sString.append( sOldFormatString.subView(nIndexE, nIndexSep - nIndexE) );
        else
            sString.append( sOldFormatString.subView(nIndexE) );
      }
    }
    else if (eType == SvNumFormatType::CURRENCY)
    {
        OUStringBuffer sNegStr(sString);
        OUString aCurr;
        const NfCurrencyEntry* pEntry;
        bool bBank;
        bool isPosNatNum12 = sPosNatNumModifier.startsWith( "[NatNum12" );
        bool isNegNatNum12 = sNegNatNumModifier.startsWith( "[NatNum12" );
        if ( !isPosNatNum12 || !isNegNatNum12 )
        {
            if (rFormatData.GetNewCurrencySymbolString(nIndex, aCurr, &pEntry, &bBank))
            {
                if ( pEntry )
                {
                    sal_uInt16 nPosiForm = NfCurrencyEntry::GetEffectivePositiveFormat(
                        rCurrentLanguage.xLocaleData->getCurrPositiveFormat(),
                        pEntry->GetPositiveFormat(), bBank );
                    sal_uInt16 nNegaForm = NfCurrencyEntry::GetEffectiveNegativeFormat(
                        rCurrentLanguage.xLocaleData->getCurrNegativeFormat(),
                        pEntry->GetNegativeFormat(), bBank );
                    if ( !isPosNatNum12 )
                        pEntry->CompletePositiveFormatString( sString, bBank, nPosiForm );
                    if ( !isNegNatNum12 )
                        pEntry->CompleteNegativeFormatString( sNegStr, bBank, nNegaForm );
                }
                else
                {   // assume currency abbreviation (AKA banking symbol), not symbol
                    sal_uInt16 nPosiForm = NfCurrencyEntry::GetEffectivePositiveFormat(
                        rCurrentLanguage.xLocaleData->getCurrPositiveFormat(),
                        rCurrentLanguage.xLocaleData->getCurrPositiveFormat(), true );
                    sal_uInt16 nNegaForm = NfCurrencyEntry::GetEffectiveNegativeFormat(
                        rCurrentLanguage.xLocaleData->getCurrNegativeFormat(),
                        rCurrentLanguage.xLocaleData->getCurrNegativeFormat(), true );
                    if ( !isPosNatNum12 )
                        NfCurrencyEntry::CompletePositiveFormatString( sString, aCurr, nPosiForm );
                    if ( !isNegNatNum12 )
                        NfCurrencyEntry::CompleteNegativeFormatString( sNegStr, aCurr, nNegaForm );
                }
            }
            else
            {   // "automatic" old style
                OUString aSymbol, aAbbrev;
                rCurrentLanguage.GetCompatibilityCurrency(aSymbol, aAbbrev);
                if ( !isPosNatNum12 )
                    NfCurrencyEntry::CompletePositiveFormatString( sString,
                                        aSymbol, rCurrentLanguage.xLocaleData->getCurrPositiveFormat() );
                if ( !isNegNatNum12 )
                    NfCurrencyEntry::CompleteNegativeFormatString( sNegStr,
                                        aSymbol, rCurrentLanguage.xLocaleData->getCurrNegativeFormat() );
            }
        }
        sString.append( ';' );
        if (IsRed)
        {
            sString.append("[" + rCurrentLanguage.pFormatScanner->GetRedString() + "]");
        }
        sString.append( sNegNatNumModifier );
        if ( isNegNatNum12 )
        {
            sString.append( '-' );
        }
        sString.append(sNegStr);
    }
    else if (eType == SvNumFormatType::FRACTION)
    {
        OUString aIntegerFractionDelimiterString = pFormat->GetIntegerFractionDelimiterString( 0 );
        if ( aIntegerFractionDelimiterString == " " )
            sString.append( aIntegerFractionDelimiterString );
        else
        {
            sString.append( "\"" + aIntegerFractionDelimiterString + "\"" );
        }
        sString.append( pFormat->GetNumeratorString( 0 )
            + "/" );
        if ( nPrecision > 0 )
            padToLength(sString, sString.getLength() + nPrecision, '?');
        else
            sString.append( '#' );
    }
    if (eType != SvNumFormatType::CURRENCY)
    {
        bool insertBrackets = false;
        if ( eType != SvNumFormatType::UNDEFINED)
        {
            insertBrackets = pFormat->IsNegativeInBracket();
        }
        if (IsRed || insertBrackets)
        {
            OUStringBuffer sTmpStr(sString);

            if (pFormat && pFormat->HasPositiveBracketPlaceholder())
            {
                 sTmpStr.append("_)");
            }
            sTmpStr.append(';');

            if (IsRed)
            {
                sTmpStr.append("[" + rCurrentLanguage.pFormatScanner->GetRedString() + "]");
            }
            sTmpStr.append( sNegNatNumModifier );

            if (insertBrackets)
            {
                sTmpStr.append("(" + sString + ")");
            }
            else
            {
                sTmpStr.append("-" + sString);
            }
            sString = std::move(sTmpStr);
        }
    }
    sString.insert( 0, sPosNatNumModifier );
    return sString.makeStringAndClear();
}

OUString SvNumberFormatter::GenerateFormat(sal_uInt32 nIndex,
                                           LanguageType eLnge,
                                           bool bThousand,
                                           bool IsRed,
                                           sal_uInt16 nPrecision,
                                           sal_uInt16 nLeadingZeros)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return SvNFEngine::GenerateFormat(m_aCurrentLanguage, m_aFormatData,
                                      GetNatNum(), m_aRWPolicy,
                                      nIndex, eLnge,
                                      bThousand, IsRed,
                                      nPrecision, nLeadingZeros);
}

bool SvNumberFormatter::IsUserDefined(sal_uInt32 F_Index) const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    const SvNumberformat* pFormat = m_aFormatData.GetFormatEntry(F_Index);

    return pFormat && (pFormat->GetType() & SvNumFormatType::DEFINED);
}

bool SvNumberFormatter::IsUserDefined(std::u16string_view sStr,
                                      LanguageType eLnge)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    if (eLnge == LANGUAGE_DONTKNOW)
    {
        eLnge = IniLnge;
    }
    sal_uInt32 CLOffset = m_aFormatData.ImpGenerateCL(m_aCurrentLanguage, GetNatNum(), eLnge);     // create new standard formats if necessary
    eLnge = m_aCurrentLanguage.ActLnge;

    sal_uInt32 nKey = ImpIsEntry(sStr, CLOffset, eLnge);
    if (nKey == NUMBERFORMAT_ENTRY_NOT_FOUND)
    {
        return true;
    }
    SvNumberformat* pEntry = m_aFormatData.GetFormatEntry( nKey );
    return pEntry && (pEntry->GetType() & SvNumFormatType::DEFINED);
}

sal_uInt32 SvNumberFormatter::GetEntryKey(std::u16string_view sStr,
                                          LanguageType eLnge)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    if (eLnge == LANGUAGE_DONTKNOW)
    {
        eLnge = IniLnge;
    }
    sal_uInt32 CLOffset = m_aFormatData.ImpGenerateCL(m_aCurrentLanguage, GetNatNum(), eLnge);     // create new standard formats if necessary
    return ImpIsEntry(sStr, CLOffset, eLnge);
}

sal_uInt32 SvNFEngine::GetStandardIndex(SvNFLanguageData& rCurrentLanguage,
                                        const SvNFFormatData& rFormatData,
                                        const NativeNumberWrapper& rNatNum,
                                        const Accessor& rFuncs,
                                        LanguageType eLnge)
{
    return SvNFEngine::GetStandardFormat(rCurrentLanguage, rFormatData,
                                         rNatNum, rFuncs,
                                         SvNumFormatType::NUMBER, eLnge);
}

sal_uInt32 SvNumberFormatter::GetStandardIndex(LanguageType eLnge)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return SvNFEngine::GetStandardIndex(m_aCurrentLanguage, m_aFormatData, GetNatNum(),
                                        m_aRWPolicy, eLnge);
}

SvNumFormatType SvNumberFormatter::GetType(sal_uInt32 nFIndex) const
{
    ::osl::MutexGuard aGuard(GetInstanceMutex());
    return m_aFormatData.GetType(nFIndex);
}

void SvNumberFormatter::ClearMergeTable()
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    if ( pMergeTable )
    {
        pMergeTable->clear();
    }
}

SvNumberFormatterIndexTable* SvNumberFormatter::MergeFormatter(SvNumberFormatter& rTable)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    if ( pMergeTable )
    {
        ClearMergeTable();
    }
    else
    {
        pMergeTable.reset( new SvNumberFormatterIndexTable );
    }

    sal_uInt32 nCLOffset = 0;
    sal_uInt32 nOldKey, nOffset, nNewKey;

    for (const auto& rEntry : rTable.m_aFormatData.aFTable)
    {
        SvNumberformat* pFormat = rEntry.second.get();
        nOldKey = rEntry.first;
        nOffset = nOldKey % SV_COUNTRY_LANGUAGE_OFFSET;     // relative index
        if (nOffset == 0)                                   // 1st format of CL
        {
            nCLOffset = m_aFormatData.ImpGenerateCL(m_aCurrentLanguage, GetNatNum(), pFormat->GetLanguage());
        }
        if (nOffset <= SV_MAX_COUNT_STANDARD_FORMATS)     // Std.form.
        {
            nNewKey = nCLOffset + nOffset;
            if (m_aFormatData.aFTable.find( nNewKey) == m_aFormatData.aFTable.end())    // not already present
            {
                std::unique_ptr<SvNumberformat> pNewEntry(new SvNumberformat( *pFormat, *m_aCurrentLanguage.pFormatScanner ));
                if (!m_aFormatData.aFTable.emplace( nNewKey, std::move(pNewEntry)).second)
                {
                    SAL_WARN( "svl.numbers", "SvNumberFormatter::MergeFormatter: dup position");
                }
            }
            if (nNewKey != nOldKey)                     // new index
            {
                (*pMergeTable)[nOldKey] = nNewKey;
            }
        }
        else                                            // user defined
        {
            std::unique_ptr<SvNumberformat> pNewEntry(new SvNumberformat( *pFormat, *m_aCurrentLanguage.pFormatScanner ));
            nNewKey = ImpIsEntry(pNewEntry->GetFormatstring(),
                                 nCLOffset,
                                 pFormat->GetLanguage());
            if (nNewKey == NUMBERFORMAT_ENTRY_NOT_FOUND) // only if not present yet
            {
                SvNumberformat* pStdFormat = m_aFormatData.GetFormatEntry(nCLOffset + ZF_STANDARD);
                sal_uInt32 nPos = nCLOffset + pStdFormat->GetLastInsertKey( SvNumberformat::FormatterPrivateAccess() );
                nNewKey = nPos+1;
                if (nNewKey - nCLOffset >= SV_COUNTRY_LANGUAGE_OFFSET)
                {
                    SAL_WARN( "svl.numbers", "SvNumberFormatter::MergeFormatter: too many formats for CL");
                }
                else if (!m_aFormatData.aFTable.emplace( nNewKey, std::move(pNewEntry)).second)
                {
                    SAL_WARN( "svl.numbers", "SvNumberFormatter::MergeFormatter: dup position");
                }
                else
                {
                    pStdFormat->SetLastInsertKey(static_cast<sal_uInt16>(nNewKey - nCLOffset),
                            SvNumberformat::FormatterPrivateAccess());
                }
            }
            if (nNewKey != nOldKey)                     // new index
            {
                (*pMergeTable)[nOldKey] = nNewKey;
            }
        }
    }
    return pMergeTable.get();
}


SvNumberFormatterMergeMap SvNumberFormatter::ConvertMergeTableToMap()
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    if (!HasMergeFormatTable())
    {
        return SvNumberFormatterMergeMap();
    }
    SvNumberFormatterMergeMap aMap;
    for (const auto& rEntry : *pMergeTable)
    {
        sal_uInt32 nOldKey = rEntry.first;
        aMap[ nOldKey ] = rEntry.second;
    }
    ClearMergeTable();
    return aMap;
}

namespace
{
    bool IsFormatSimpleBuiltIn(const SvNFLanguageData& rCurrentLanguage, sal_uInt32 nFormat, LanguageType eLnge)
    {
        if (nFormat < SV_COUNTRY_LANGUAGE_OFFSET && eLnge == rCurrentLanguage.GetIniLanguage())
            return true; // it stays as it is

        sal_uInt32 nOffset = nFormat % SV_COUNTRY_LANGUAGE_OFFSET;  // relative index
        if (nOffset > SV_MAX_COUNT_STANDARD_FORMATS)
            return true; // not a built-in format

        return false;
    }
}

sal_uInt32 SvNFEngine::GetCLOffsetRO(const SvNFFormatData& rFormatData, SvNFLanguageData&, const NativeNumberWrapper&, LanguageType eLnge)
{
    sal_uInt32 nCLOffset = rFormatData.ImpGetCLOffset(eLnge);
    assert(nCLOffset <= rFormatData.MaxCLOffset && "Language must have already been encountered");
    return nCLOffset;
}

// we can't cache to SvNFFormatData in RO mode, so cache to a per-thread temp map which we can consult in FindFormatRO
// caches can be merged with SvNumberFormatter::MergeDefaultFormatKeys
void SvNFEngine::CacheFormatRO(SvNFFormatData::DefaultFormatKeysMap& rFormatCache, sal_uInt32 nSearch, sal_uInt32 nFormat)
{
    rFormatCache[nSearch] = nFormat;
}

sal_uInt32 SvNFEngine::FindFormatRO(const SvNFFormatData& rFormatData, const SvNFFormatData::DefaultFormatKeysMap& rFormatCache, sal_uInt32 nSearch)
{
    sal_uInt32 nFormat = rFormatData.FindCachedDefaultFormat(nSearch);
    if (nFormat != NUMBERFORMAT_ENTRY_NOT_FOUND)
        return nFormat;
    return ::FindCachedDefaultFormat(rFormatCache, nSearch);
}

sal_uInt32 SvNFEngine::GetCLOffsetRW(SvNFFormatData& rFormatData, SvNFLanguageData& rCurrentLanguage, const NativeNumberWrapper& rNatNum, LanguageType eLnge)
{
    return rFormatData.ImpGenerateCL(rCurrentLanguage, rNatNum, eLnge);    // create new standard formats if necessary
}

void SvNFEngine::CacheFormatRW(SvNFFormatData& rFormatData, sal_uInt32 nSearch, sal_uInt32 nFormat)
{
    rFormatData.aDefaultFormatKeys[nSearch] = nFormat;
}

sal_uInt32 SvNFEngine::FindFormatRW(const SvNFFormatData& rFormatData, sal_uInt32 nSearch)
{
    return rFormatData.FindCachedDefaultFormat(nSearch);
}

sal_uInt32 SvNFEngine::DefaultCurrencyRW(SvNFFormatData& rFormatData, SvNFLanguageData& rCurrentLanguage, const NativeNumberWrapper& rNatNum, sal_uInt32 CLOffset, LanguageType eLnge)
{
    if (eLnge == LANGUAGE_SYSTEM)
        return rFormatData.ImpGetDefaultSystemCurrencyFormat(rCurrentLanguage, rNatNum);
    return rFormatData.ImpGetDefaultCurrencyFormat(rCurrentLanguage, rNatNum, CLOffset, eLnge);
}

sal_uInt32 SvNFEngine::DefaultCurrencyRO(const SvNFFormatData& rFormatData, SvNFLanguageData&, const NativeNumberWrapper&, sal_uInt32 CLOffset, LanguageType eLnge)
{
    if (eLnge == LANGUAGE_SYSTEM)
    {
        assert(rFormatData.nDefaultSystemCurrencyFormat != NUMBERFORMAT_ENTRY_NOT_FOUND);
        return rFormatData.nDefaultSystemCurrencyFormat;
    }

    SvNFFormatData::DefaultFormatKeysMap::const_iterator it = rFormatData.aDefaultFormatKeys.find(CLOffset + ZF_STANDARD_CURRENCY);
    assert(it != rFormatData.aDefaultFormatKeys.end() && it->second != NUMBERFORMAT_ENTRY_NOT_FOUND);
    return it->second;
}

SvNFEngine::Accessor SvNFEngine::GetRWPolicy(SvNFFormatData& rFormatData)
{
    return
    {
        [&rFormatData](SvNFLanguageData& rCurrentLanguage, const NativeNumberWrapper& rNatNum, LanguageType eLnge)
        {
            return SvNFEngine::GetCLOffsetRW(rFormatData, rCurrentLanguage, rNatNum, eLnge);
        },
        [&rFormatData](sal_uInt32 nSearch, sal_uInt32 nFormat)
        {
            return SvNFEngine::CacheFormatRW(rFormatData, nSearch, nFormat);
        },
        [&rFormatData](sal_uInt32 nSearch)
        {
            return SvNFEngine::FindFormatRW(rFormatData, nSearch);
        },
        [&rFormatData](SvNFLanguageData& rCurrentLanguage, const NativeNumberWrapper& rNatNum, sal_uInt32 CLOffset, LanguageType eLnge)
        {
            return SvNFEngine::DefaultCurrencyRW(rFormatData, rCurrentLanguage, rNatNum, CLOffset, eLnge);
        }
    };
}

void SvNumberFormatter::PrepForRoMode()
{
    SvNumberFormatter::GetTheCurrencyTable(); // create this now so threads don't attempt to create it simultaneously
    if (m_aFormatData.nDefaultSystemCurrencyFormat == NUMBERFORMAT_ENTRY_NOT_FOUND)
    {
        m_aFormatData.ImpGetDefaultSystemCurrencyFormat(m_aCurrentLanguage, GetNatNum());
        assert(m_aFormatData.nDefaultSystemCurrencyFormat != NUMBERFORMAT_ENTRY_NOT_FOUND);
    }
}

SvNFEngine::Accessor SvNFEngine::GetROPolicy(const SvNFFormatData& rFormatData, SvNFFormatData::DefaultFormatKeysMap& rFormatCache)
{
    assert(rFormatData.nDefaultSystemCurrencyFormat != NUMBERFORMAT_ENTRY_NOT_FOUND && "ensure PrepForRoMode is called");
    assert(g_CurrencyTableInitialized && "ensure PrepForRoMode is called");
    return
    {
        [&rFormatData](SvNFLanguageData& rCurrentLanguage, const NativeNumberWrapper& rNatNum, LanguageType eLnge)
        {
            return SvNFEngine::GetCLOffsetRO(rFormatData, rCurrentLanguage, rNatNum, eLnge);
        },
        [&rFormatCache](sal_uInt32 nSearch, sal_uInt32 nFormat)
        {
            return SvNFEngine::CacheFormatRO(rFormatCache, nSearch, nFormat);
        },
        [&rFormatData, rFormatCache](sal_uInt32 nSearch)
        {
            return SvNFEngine::FindFormatRO(rFormatData, rFormatCache, nSearch);
        },
        [&rFormatData](SvNFLanguageData& rCurrentLanguage, const NativeNumberWrapper& rNatNum, sal_uInt32 CLOffset, LanguageType eLnge)
        {
            return SvNFEngine::DefaultCurrencyRO(rFormatData, rCurrentLanguage, rNatNum, CLOffset, eLnge);
        }
    };
}

sal_uInt32 SvNFEngine::GetFormatForLanguageIfBuiltIn(SvNFLanguageData& rCurrentLanguage,
                                                     const NativeNumberWrapper& rNatNum,
                                                     const Accessor& rFuncs,
                                                     sal_uInt32 nFormat, LanguageType eLnge)
{
    eLnge = rCurrentLanguage.ImpResolveLanguage(eLnge);

    if (IsFormatSimpleBuiltIn(rCurrentLanguage, nFormat, eLnge))
        return nFormat;

    sal_uInt32 nCLOffset = rFuncs.mGetCLOffset(rCurrentLanguage, rNatNum, eLnge);
    sal_uInt32 nOffset = nFormat % SV_COUNTRY_LANGUAGE_OFFSET;  // relative index
    return nCLOffset + nOffset;
}

sal_uInt32 SvNumberFormatter::GetFormatForLanguageIfBuiltIn( sal_uInt32 nFormat,
                                                             LanguageType eLnge )
{
    ::osl::MutexGuard aGuard(GetInstanceMutex());
    return SvNFEngine::GetFormatForLanguageIfBuiltIn(m_aCurrentLanguage, GetNatNum(), m_aRWPolicy, nFormat, eLnge);
}

LanguageType SvNFLanguageData::ImpResolveLanguage(LanguageType eLnge) const
{
    return eLnge == LANGUAGE_DONTKNOW ? IniLnge : eLnge;
}

sal_uInt32 SvNFEngine::GetFormatIndex(SvNFLanguageData& rCurrentLanguage, const Accessor& rFuncs,
                                      const NativeNumberWrapper& rNatNum, NfIndexTableOffset nTabOff,
                                      LanguageType eLnge)
{
    eLnge = rCurrentLanguage.ImpResolveLanguage(eLnge);

    sal_uInt32 nCLOffset = rFuncs.mGetCLOffset(rCurrentLanguage, rNatNum, eLnge); // create new standard formats if necessary (and allowed)

    return ImpGetFormatIndex(nTabOff, nCLOffset);
}

sal_uInt32 SvNumberFormatter::GetFormatIndex(NfIndexTableOffset nTabOff, LanguageType eLnge)
{
    ::osl::MutexGuard aGuard(GetInstanceMutex());
    return SvNFEngine::GetFormatIndex(m_aCurrentLanguage, m_aRWPolicy, GetNatNum(), nTabOff, eLnge);
}

// static
NfIndexTableOffset SvNumberFormatter::GetIndexTableOffset(sal_uInt32 nFormat)
{
    sal_uInt32 nOffset = nFormat % SV_COUNTRY_LANGUAGE_OFFSET;      // relative index
    if ( nOffset > SV_MAX_COUNT_STANDARD_FORMATS )
    {
        return NF_INDEX_TABLE_ENTRIES;      // not a built-in format
    }

    for ( sal_uInt16 j = 0; j < NF_INDEX_TABLE_ENTRIES; j++ )
    {
        if (indexTable[j] == nOffset)
            return static_cast<NfIndexTableOffset>(j);
    }
    return NF_INDEX_TABLE_ENTRIES;      // bad luck
}

void SvNumberFormatter::SetEvalDateFormat( NfEvalDateFormat eEDF )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    m_aCurrentLanguage.SetEvalDateFormat(eEDF);
}

NfEvalDateFormat SvNumberFormatter::GetEvalDateFormat() const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return m_aCurrentLanguage.GetEvalDateFormat();
}

void SvNumberFormatter::SetYear2000( sal_uInt16 nVal )
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    m_aCurrentLanguage.pStringScanner->SetYear2000( nVal );
}


sal_uInt16 SvNumberFormatter::GetYear2000() const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return m_aCurrentLanguage.pStringScanner->GetYear2000();
}

sal_uInt16 SvNFLanguageData::ExpandTwoDigitYear( sal_uInt16 nYear ) const
{
    if ( nYear < 100 )
        return SvNumberFormatter::ExpandTwoDigitYear( nYear,
            pStringScanner->GetYear2000() );
    return nYear;
}

sal_uInt16 SvNumberFormatter::ExpandTwoDigitYear( sal_uInt16 nYear ) const
{
    return m_aCurrentLanguage.ExpandTwoDigitYear(nYear);
}

// static
sal_uInt16 SvNumberFormatter::GetYear2000Default()
{
    if (!comphelper::IsFuzzing())
        return officecfg::Office::Common::DateFormat::TwoDigitYear::get();
    return 1930;
}

// static
void SvNumberFormatter::resetTheCurrencyTable()
{
    SAL_INFO("svl", "Resetting the currency table.");

    nSystemCurrencyPosition = 0;
    g_CurrencyTableInitialized = false;

    GetFormatterRegistry().ConfigurationChanged(nullptr, ConfigurationHints::Locale | ConfigurationHints::Currency | ConfigurationHints::DatePatterns);
}

// static
const NfCurrencyTable& SvNumberFormatter::GetTheCurrencyTable()
{
    while (!g_CurrencyTableInitialized)
        ImpInitCurrencyTable();
    return theCurrencyTable();
}

// static
const NfCurrencyEntry* SvNumberFormatter::MatchSystemCurrency()
{
    // MUST call GetTheCurrencyTable() before accessing nSystemCurrencyPosition
    const NfCurrencyTable& rTable = GetTheCurrencyTable();
    return nSystemCurrencyPosition ? &rTable[nSystemCurrencyPosition] : nullptr;
}

// static
const NfCurrencyEntry& SvNumberFormatter::GetCurrencyEntry( LanguageType eLang )
{
    if ( eLang == LANGUAGE_SYSTEM )
    {
        const NfCurrencyEntry* pCurr = MatchSystemCurrency();
        return pCurr ? *pCurr : GetTheCurrencyTable()[0];
    }
    else
    {
        eLang = MsLangId::getRealLanguage( eLang );
        const NfCurrencyTable& rTable = GetTheCurrencyTable();
        sal_uInt16 nCount = rTable.size();
        for ( sal_uInt16 j = 0; j < nCount; j++ )
        {
            if ( rTable[j].GetLanguage() == eLang )
                return rTable[j];
        }
        return rTable[0];
    }
}


// static
const NfCurrencyEntry* SvNumberFormatter::GetCurrencyEntry(std::u16string_view rAbbrev, LanguageType eLang )
{
    eLang = MsLangId::getRealLanguage( eLang );
    const NfCurrencyTable& rTable = GetTheCurrencyTable();
    sal_uInt16 nCount = rTable.size();
    for ( sal_uInt16 j = 0; j < nCount; j++ )
    {
        if ( rTable[j].GetLanguage() == eLang &&
             rTable[j].GetBankSymbol() == rAbbrev )
        {
            return &rTable[j];
        }
    }
    return nullptr;
}


// static
const NfCurrencyEntry* SvNumberFormatter::GetLegacyOnlyCurrencyEntry( std::u16string_view rSymbol,
                                                                      std::u16string_view rAbbrev )
{
    GetTheCurrencyTable();      // just for initialization
    const NfCurrencyTable& rTable = theLegacyOnlyCurrencyTable();
    sal_uInt16 nCount = rTable.size();
    for ( sal_uInt16 j = 0; j < nCount; j++ )
    {
        if ( rTable[j].GetSymbol() == rSymbol &&
             rTable[j].GetBankSymbol() == rAbbrev )
        {
            return &rTable[j];
        }
    }
    return nullptr;
}


// static
IMPL_STATIC_LINK_NOARG( SvNumberFormatter, CurrencyChangeLink, LinkParamNone*, void )
{
    OUString aAbbrev;
    LanguageType eLang = LANGUAGE_SYSTEM;
    SvtSysLocaleOptions().GetCurrencyAbbrevAndLanguage( aAbbrev, eLang );
    SetDefaultSystemCurrency( aAbbrev, eLang );
}


// static
void SvNumberFormatter::SetDefaultSystemCurrency( std::u16string_view rAbbrev, LanguageType eLang )
{
    ::osl::MutexGuard aGuard( GetGlobalMutex() );
    if ( eLang == LANGUAGE_SYSTEM )
    {
        eLang = SvtSysLocale().GetLanguageTag().getLanguageType();
    }
    const NfCurrencyTable& rTable = GetTheCurrencyTable();
    sal_uInt16 nCount = rTable.size();
    if ( !rAbbrev.empty() )
    {
        for ( sal_uInt16 j = 0; j < nCount; j++ )
        {
            if ( rTable[j].GetLanguage() == eLang && rTable[j].GetBankSymbol() == rAbbrev )
            {
                nSystemCurrencyPosition = j;
                return ;
            }
        }
    }
    else
    {
        for ( sal_uInt16 j = 0; j < nCount; j++ )
        {
            if ( rTable[j].GetLanguage() == eLang )
            {
                nSystemCurrencyPosition = j;
                return ;
            }
        }
    }
    nSystemCurrencyPosition = 0;    // not found => simple SYSTEM
}

void SvNFFormatData::ResetDefaultSystemCurrency()
{
    nDefaultSystemCurrencyFormat = NUMBERFORMAT_ENTRY_NOT_FOUND;
}

void SvNumberFormatter::ResetDefaultSystemCurrency()
{
    m_aFormatData.ResetDefaultSystemCurrency();
}

void SvNumberFormatter::InvalidateDateAcceptancePatterns()
{
    m_aCurrentLanguage.pStringScanner->InvalidateDateAcceptancePatterns();
}

sal_uInt32 SvNFFormatData::ImpGetDefaultSystemCurrencyFormat(SvNFLanguageData& rCurrentLanguage,
                                                             const NativeNumberWrapper& rNatNum)
{
    if ( nDefaultSystemCurrencyFormat == NUMBERFORMAT_ENTRY_NOT_FOUND )
    {
        sal_Int32 nCheck;
        SvNumFormatType nType;
        NfWSStringsDtor aCurrList;
        sal_uInt16 nDefault = rCurrentLanguage.GetCurrencyFormatStrings( aCurrList,
            SvNumberFormatter::GetCurrencyEntry( LANGUAGE_SYSTEM ), false );
        DBG_ASSERT( aCurrList.size(), "where is the NewCurrency System standard format?!?" );
        // if already loaded or user defined nDefaultSystemCurrencyFormat
        // will be set to the right value
        PutEntry(rCurrentLanguage, rNatNum, aCurrList[ nDefault ], nCheck, nType,
            nDefaultSystemCurrencyFormat, LANGUAGE_SYSTEM );
        DBG_ASSERT( nCheck == 0, "NewCurrency CheckError" );
        DBG_ASSERT( nDefaultSystemCurrencyFormat != NUMBERFORMAT_ENTRY_NOT_FOUND,
            "nDefaultSystemCurrencyFormat == NUMBERFORMAT_ENTRY_NOT_FOUND" );
    }
    return nDefaultSystemCurrencyFormat;
}

sal_uInt32 SvNFFormatData::ImpGetDefaultCurrencyFormat(SvNFLanguageData& rCurrentLanguage, const NativeNumberWrapper& rNatNum,
                                                       sal_uInt32 CLOffset, LanguageType eLnge)
{
    DefaultFormatKeysMap::const_iterator it = aDefaultFormatKeys.find( CLOffset + ZF_STANDARD_CURRENCY );
    sal_uInt32 nDefaultCurrencyFormat = (it != aDefaultFormatKeys.end() ?
            it->second : NUMBERFORMAT_ENTRY_NOT_FOUND);
    if ( nDefaultCurrencyFormat == NUMBERFORMAT_ENTRY_NOT_FOUND )
    {
        // look for a defined standard
        sal_uInt32 nStopKey = CLOffset + SV_COUNTRY_LANGUAGE_OFFSET;
        sal_uInt32 nKey(0);
        auto it2 = aFTable.lower_bound( CLOffset );
        while ( it2 != aFTable.end() && (nKey = it2->first) >= CLOffset && nKey < nStopKey )
        {
            const SvNumberformat* pEntry = it2->second.get();
            if ( pEntry->IsStandard() && (pEntry->GetType() & SvNumFormatType::CURRENCY) )
            {
                nDefaultCurrencyFormat = nKey;
                break;  // while
            }
            ++it2;
        }

        if ( nDefaultCurrencyFormat == NUMBERFORMAT_ENTRY_NOT_FOUND )
        {   // none found, create one
            sal_Int32 nCheck;
            NfWSStringsDtor aCurrList;
            sal_uInt16 nDefault = rCurrentLanguage.GetCurrencyFormatStrings( aCurrList,
                SvNumberFormatter::GetCurrencyEntry(eLnge), false );
            DBG_ASSERT( aCurrList.size(), "where is the NewCurrency standard format?" );
            if ( !aCurrList.empty() )
            {
                // if already loaded or user defined nDefaultSystemCurrencyFormat
                // will be set to the right value
                SvNumFormatType nType;
                PutEntry(rCurrentLanguage, rNatNum, aCurrList[ nDefault ], nCheck, nType,
                    nDefaultCurrencyFormat, eLnge );
                DBG_ASSERT( nCheck == 0, "NewCurrency CheckError" );
                DBG_ASSERT( nDefaultCurrencyFormat != NUMBERFORMAT_ENTRY_NOT_FOUND,
                    "nDefaultCurrencyFormat == NUMBERFORMAT_ENTRY_NOT_FOUND" );
            }
            // old automatic currency format as a last resort
            if ( nDefaultCurrencyFormat == NUMBERFORMAT_ENTRY_NOT_FOUND )
                nDefaultCurrencyFormat = CLOffset + ZF_STANDARD_CURRENCY+3;
            else
            {   // mark as standard so that it is found next time
                SvNumberformat* pEntry = GetFormatEntry( nDefaultCurrencyFormat );
                if ( pEntry )
                    pEntry->SetStandard();
            }
        }
        aDefaultFormatKeys[ CLOffset + ZF_STANDARD_CURRENCY ] = nDefaultCurrencyFormat;
    }
    return nDefaultCurrencyFormat;
}


// static
// true: continue; false: break loop, if pFoundEntry==NULL dupe found
bool SvNumberFormatter::ImpLookupCurrencyEntryLoopBody(
    const NfCurrencyEntry*& pFoundEntry, bool& bFoundBank, const NfCurrencyEntry* pData,
    sal_uInt16 nPos, std::u16string_view rSymbol )
{
    bool bFound;
    if ( pData->GetSymbol() == rSymbol )
    {
        bFound = true;
        bFoundBank = false;
    }
    else if ( pData->GetBankSymbol() == rSymbol )
    {
        bFound = true;
        bFoundBank = true;
    }
    else
        bFound = false;
    if ( bFound )
    {
        if ( pFoundEntry && pFoundEntry != pData )
        {
            pFoundEntry = nullptr;
            return false;   // break loop, not unique
        }
        if ( nPos == 0 )
        {   // first entry is SYSTEM
            pFoundEntry = MatchSystemCurrency();
            if ( pFoundEntry )
            {
                return false;   // break loop
                // even if there are more matching entries
                // this one is probably the one we are looking for
            }
            else
            {
                pFoundEntry = pData;
            }
        }
        else
        {
            pFoundEntry = pData;
        }
    }
    return true;
}

void SvNFFormatData::MergeDefaultFormatKeys(const DefaultFormatKeysMap& rDefaultFormatKeys)
{
    for (const auto& r : rDefaultFormatKeys)
    {
#ifndef NDEBUG
        auto iter = aDefaultFormatKeys.find(r.first);
        assert(iter == aDefaultFormatKeys.end() || iter->second == r.second);
#endif
        aDefaultFormatKeys[r.first] = r.second;
    }
}

void SvNumberFormatter::MergeDefaultFormatKeys(const SvNFFormatData::DefaultFormatKeysMap& rDefaultFormatKeys)
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    m_aFormatData.MergeDefaultFormatKeys(rDefaultFormatKeys);
}

bool SvNFFormatData::GetNewCurrencySymbolString(sal_uInt32 nFormat, OUString& rStr,
                                                const NfCurrencyEntry** ppEntry /* = NULL */,
                                                bool* pBank /* = NULL */) const
{
    if ( ppEntry )
        *ppEntry = nullptr;
    if ( pBank )
        *pBank = false;

    const SvNumberformat* pFormat = GetFormatEntry(nFormat);
    if ( pFormat )
    {
        OUString aSymbol, aExtension;
        if ( pFormat->GetNewCurrencySymbol( aSymbol, aExtension ) )
        {
            OUStringBuffer sBuff(128); // guess-estimate of a value that will pretty much guarantee no re-alloc
            if ( ppEntry )
            {
                bool bFoundBank = false;
                // we definitely need an entry matching the format code string
                const NfCurrencyEntry* pFoundEntry = SvNumberFormatter::GetCurrencyEntry(
                    bFoundBank, aSymbol, aExtension, pFormat->GetLanguage(),
                    true );
                if ( pFoundEntry )
                {
                    *ppEntry = pFoundEntry;
                    if ( pBank )
                        *pBank = bFoundBank;
                    rStr = pFoundEntry->BuildSymbolString(bFoundBank);
                }
            }
            if ( rStr.isEmpty() )
            {   // analog to BuildSymbolString
                sBuff.append("[$");
                if ( aSymbol.indexOf( '-' ) != -1 ||
                        aSymbol.indexOf( ']' ) != -1 )
                {
                    sBuff.append("\"" + aSymbol + "\"");
                }
                else
                {
                    sBuff.append(aSymbol);
                }
                if ( !aExtension.isEmpty() )
                {
                    sBuff.append(aExtension);
                }
                sBuff.append(']');
            }
            rStr = sBuff.makeStringAndClear();
            return true;
        }
    }
    rStr.clear();
    return false;
}

bool SvNumberFormatter::GetNewCurrencySymbolString( sal_uInt32 nFormat, OUString& rStr,
                                                    const NfCurrencyEntry** ppEntry /* = NULL */,
                                                    bool* pBank /* = NULL */ ) const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return m_aFormatData.GetNewCurrencySymbolString(nFormat, rStr, ppEntry, pBank);
}

// static
const NfCurrencyEntry* SvNumberFormatter::GetCurrencyEntry( bool & bFoundBank,
                                                            std::u16string_view rSymbol,
                                                            std::u16string_view rExtension,
                                                            LanguageType eFormatLanguage,
                                                            bool bOnlyStringLanguage )
{
    sal_Int32 nExtLen = rExtension.size();
    LanguageType eExtLang;
    if ( nExtLen )
    {
        // rExtension should be a 16-bit hex value max FFFF which may contain a
        // leading "-" separator (that is not a minus sign, but toInt32 can be
        // used to parse it, with post-processing as necessary):
        sal_Int32 nExtLang = o3tl::toInt32(rExtension, 16);
        if ( !nExtLang )
        {
            eExtLang = LANGUAGE_DONTKNOW;
        }
        else
        {
            if (nExtLang < 0)
                nExtLang = -nExtLang;
            SAL_WARN_IF(nExtLang > 0xFFFF, "svl.numbers", "Out of range Lang Id: " << nExtLang << " from input string: " << OUString(rExtension));
            eExtLang = LanguageType(nExtLang & 0xFFFF);
        }
    }
    else
    {
        eExtLang = LANGUAGE_DONTKNOW;
    }
    const NfCurrencyEntry* pFoundEntry = nullptr;
    const NfCurrencyTable& rTable = GetTheCurrencyTable();
    sal_uInt16 nCount = rTable.size();
    bool bCont = true;

    // first try with given extension language/country
    if ( nExtLen )
    {
        for ( sal_uInt16 j = 0; j < nCount && bCont; j++ )
        {
            LanguageType eLang = rTable[j].GetLanguage();
            if ( eLang == eExtLang ||
                 ((eExtLang == LANGUAGE_DONTKNOW) &&
                  (eLang == LANGUAGE_SYSTEM)))
            {
                bCont = ImpLookupCurrencyEntryLoopBody( pFoundEntry, bFoundBank,
                                                        &rTable[j], j, rSymbol );
            }
        }
    }

    // ok?
    if ( pFoundEntry || !bCont || (bOnlyStringLanguage && nExtLen) )
    {
        return pFoundEntry;
    }
    if ( !bOnlyStringLanguage )
    {
        // now try the language/country of the number format
        for ( sal_uInt16 j = 0; j < nCount && bCont; j++ )
        {
            LanguageType eLang = rTable[j].GetLanguage();
            if ( eLang == eFormatLanguage ||
                 ((eFormatLanguage == LANGUAGE_DONTKNOW) &&
                  (eLang == LANGUAGE_SYSTEM)))
            {
                bCont = ImpLookupCurrencyEntryLoopBody( pFoundEntry, bFoundBank,
                                                        &rTable[j], j, rSymbol );
            }
        }

        // ok?
        if ( pFoundEntry || !bCont )
        {
            return pFoundEntry;
        }
    }

    // then try without language/country if no extension specified
    if ( !nExtLen )
    {
        for ( sal_uInt16 j = 0; j < nCount && bCont; j++ )
        {
            bCont = ImpLookupCurrencyEntryLoopBody( pFoundEntry, bFoundBank,
                                                    &rTable[j], j, rSymbol );
        }
    }

    return pFoundEntry;
}

void SvNFLanguageData::GetCompatibilityCurrency( OUString& rSymbol, OUString& rAbbrev ) const
{
    const css::uno::Sequence< css::i18n::Currency2 >
        xCurrencies( xLocaleData->getAllCurrencies() );

    auto pCurrency = std::find_if(xCurrencies.begin(), xCurrencies.end(),
        [](const css::i18n::Currency2& rCurrency) { return rCurrency.UsedInCompatibleFormatCodes; });
    if (pCurrency != xCurrencies.end())
    {
        rSymbol = pCurrency->Symbol;
        rAbbrev = pCurrency->BankSymbol;
    }
    else
    {
        if (LocaleDataWrapper::areChecksEnabled())
        {
            LocaleDataWrapper::outputCheckMessage( xLocaleData->
                                                   appendLocaleInfo( u"GetCompatibilityCurrency: none?"));
        }
        rSymbol = xLocaleData->getCurrSymbol();
        rAbbrev = xLocaleData->getCurrBankSymbol();
    }
}

void SvNumberFormatter::GetCompatibilityCurrency( OUString& rSymbol, OUString& rAbbrev ) const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    m_aCurrentLanguage.GetCompatibilityCurrency(rSymbol, rAbbrev);
}

static void lcl_CheckCurrencySymbolPosition( const NfCurrencyEntry& rCurr )
{
    switch ( rCurr.GetPositiveFormat() )
    {
    case 0:                                         // $1
    case 1:                                         // 1$
    case 2:                                         // $ 1
    case 3:                                         // 1 $
        break;
    default:
        LocaleDataWrapper::outputCheckMessage( "lcl_CheckCurrencySymbolPosition: unknown PositiveFormat");
        break;
    }
    switch ( rCurr.GetNegativeFormat() )
    {
    case 0:                                         // ($1)
    case 1:                                         // -$1
    case 2:                                         // $-1
    case 3:                                         // $1-
    case 4:                                         // (1$)
    case 5:                                         // -1$
    case 6:                                         // 1-$
    case 7:                                         // 1$-
    case 8:                                         // -1 $
    case 9:                                         // -$ 1
    case 10:                                        // 1 $-
    case 11:                                        // $ -1
    case 12 :                                       // $ 1-
    case 13 :                                       // 1- $
    case 14 :                                       // ($ 1)
    case 15 :                                       // (1 $)
        break;
    default:
        LocaleDataWrapper::outputCheckMessage( "lcl_CheckCurrencySymbolPosition: unknown NegativeFormat");
        break;
    }
}

// static
bool SvNumberFormatter::IsLocaleInstalled( LanguageType eLang )
{
    // The set is initialized as a side effect of the currency table
    // created, make sure that exists, which usually is the case unless a
    // SvNumberFormatter was never instantiated.
    GetTheCurrencyTable();
    return theInstalledLocales.find( eLang) != theInstalledLocales.end();
}

// static
void SvNumberFormatter::ImpInitCurrencyTable()
{
    // Race condition possible:
    // ::osl::MutexGuard aGuard( GetMutex() );
    // while (!g_CurrencyTableInitialized)
    //      ImpInitCurrencyTable();
    static bool bInitializing = false;
    if (g_CurrencyTableInitialized || bInitializing)
    {
        return ;
    }
    bInitializing = true;

    LanguageType eSysLang = SvtSysLocale().GetLanguageTag().getLanguageType();
    const LocaleDataWrapper* pLocaleData = LocaleDataWrapper::get(
        SvtSysLocale().GetLanguageTag() );
    // get user configured currency
    OUString aConfiguredCurrencyAbbrev;
    LanguageType eConfiguredCurrencyLanguage = LANGUAGE_SYSTEM;
    SvtSysLocaleOptions().GetCurrencyAbbrevAndLanguage(
        aConfiguredCurrencyAbbrev, eConfiguredCurrencyLanguage );
    sal_uInt16 nSecondarySystemCurrencyPosition = 0;
    sal_uInt16 nMatchingSystemCurrencyPosition = 0;

    // First entry is SYSTEM:
    auto& rCurrencyTable = theCurrencyTable();
    rCurrencyTable.insert(
        rCurrencyTable.begin(),
        NfCurrencyEntry(*pLocaleData, LANGUAGE_SYSTEM));
    sal_uInt16 nCurrencyPos = 1;

    const css::uno::Sequence< css::lang::Locale > xLoc = LocaleDataWrapper::getInstalledLocaleNames();
    sal_Int32 nLocaleCount = xLoc.getLength();
    SAL_INFO( "svl.numbers", "number of locales: \"" << nLocaleCount << "\"" );
    NfCurrencyTable &rLegacyOnlyCurrencyTable = theLegacyOnlyCurrencyTable();
    sal_uInt16 nLegacyOnlyCurrencyPos = 0;
    for ( css::lang::Locale const & rLocale : xLoc )
    {
        LanguageType eLang = LanguageTag::convertToLanguageType( rLocale, false);
        theInstalledLocales.insert( eLang);
        pLocaleData = LocaleDataWrapper::get( LanguageTag(rLocale) );
        Sequence< Currency2 > aCurrSeq = pLocaleData->getAllCurrencies();
        sal_Int32 nCurrencyCount = aCurrSeq.getLength();
        Currency2 const * const pCurrencies = aCurrSeq.getConstArray();

        // one default currency for each locale, insert first so it is found first
        sal_Int32 nDefault;
        for ( nDefault = 0; nDefault < nCurrencyCount; nDefault++ )
        {
            if ( pCurrencies[nDefault].Default )
                break;
        }
        std::optional<NfCurrencyEntry> pEntry;
        if ( nDefault < nCurrencyCount )
        {
            pEntry.emplace(pCurrencies[nDefault], *pLocaleData, eLang);
        }
        else
        {   // first or ShellsAndPebbles
            pEntry.emplace(*pLocaleData, eLang);
        }
        if (LocaleDataWrapper::areChecksEnabled())
        {
            lcl_CheckCurrencySymbolPosition( *pEntry );
        }
        if ( !nSystemCurrencyPosition && !aConfiguredCurrencyAbbrev.isEmpty() &&
             pEntry->GetBankSymbol() == aConfiguredCurrencyAbbrev &&
             pEntry->GetLanguage() == eConfiguredCurrencyLanguage )
        {
            nSystemCurrencyPosition = nCurrencyPos;
        }
        if ( !nMatchingSystemCurrencyPosition &&
             pEntry->GetLanguage() == eSysLang )
        {
            nMatchingSystemCurrencyPosition = nCurrencyPos;
        }
        rCurrencyTable.insert(
                rCurrencyTable.begin() + nCurrencyPos++, std::move(*pEntry));
        // all remaining currencies for each locale
        if ( nCurrencyCount > 1 )
        {
            sal_Int32 nCurrency;
            for ( nCurrency = 0; nCurrency < nCurrencyCount; nCurrency++ )
            {
                if (pCurrencies[nCurrency].LegacyOnly)
                {
                    rLegacyOnlyCurrencyTable.insert(
                        rLegacyOnlyCurrencyTable.begin() + nLegacyOnlyCurrencyPos++,
                        NfCurrencyEntry(
                            pCurrencies[nCurrency], *pLocaleData, eLang));
                }
                else if ( nCurrency != nDefault )
                {
                    pEntry.emplace(pCurrencies[nCurrency], *pLocaleData, eLang);
                    // no dupes
                    bool bInsert = true;
                    sal_uInt16 n = rCurrencyTable.size();
                    sal_uInt16 aCurrencyIndex = 1; // skip first SYSTEM entry
                    for ( sal_uInt16 j=1; j<n; j++ )
                    {
                        if ( rCurrencyTable[aCurrencyIndex++] == *pEntry )
                        {
                            bInsert = false;
                            break;  // for
                        }
                    }
                    if ( !bInsert )
                    {
                        pEntry.reset();
                    }
                    else
                    {
                        if ( !nSecondarySystemCurrencyPosition &&
                             (!aConfiguredCurrencyAbbrev.isEmpty() ?
                              pEntry->GetBankSymbol() == aConfiguredCurrencyAbbrev :
                              pEntry->GetLanguage() == eConfiguredCurrencyLanguage) )
                        {
                            nSecondarySystemCurrencyPosition = nCurrencyPos;
                        }
                        if ( !nMatchingSystemCurrencyPosition &&
                             pEntry->GetLanguage() ==  eSysLang )
                        {
                            nMatchingSystemCurrencyPosition = nCurrencyPos;
                        }
                        rCurrencyTable.insert(
                            rCurrencyTable.begin() + nCurrencyPos++, std::move(*pEntry));
                    }
                }
            }
        }
    }
    if ( !nSystemCurrencyPosition )
    {
        nSystemCurrencyPosition = nSecondarySystemCurrencyPosition;
    }
    if ((!aConfiguredCurrencyAbbrev.isEmpty() && !nSystemCurrencyPosition) &&
        LocaleDataWrapper::areChecksEnabled())
    {
        LocaleDataWrapper::outputCheckMessage(
                "SvNumberFormatter::ImpInitCurrencyTable: configured currency not in I18N locale data.");
    }
    // match SYSTEM if no configured currency found
    if ( !nSystemCurrencyPosition )
    {
        nSystemCurrencyPosition = nMatchingSystemCurrencyPosition;
    }
    if ((aConfiguredCurrencyAbbrev.isEmpty() && !nSystemCurrencyPosition) &&
        LocaleDataWrapper::areChecksEnabled())
    {
        LocaleDataWrapper::outputCheckMessage(
                "SvNumberFormatter::ImpInitCurrencyTable: system currency not in I18N locale data.");
    }
    SvtSysLocaleOptions::SetCurrencyChangeLink( LINK( nullptr, SvNumberFormatter, CurrencyChangeLink ) );
    bInitializing = false;
    g_CurrencyTableInitialized = true;
}

static std::ptrdiff_t addToCurrencyFormatsList( NfWSStringsDtor& rStrArr, const OUString& rFormat )
{
    // Prevent duplicates even over subsequent calls of
    // GetCurrencyFormatStrings() with the same vector.
    NfWSStringsDtor::const_iterator it( std::find( rStrArr.begin(), rStrArr.end(), rFormat));
    if (it != rStrArr.end())
        return it - rStrArr.begin();

    rStrArr.push_back( rFormat);
    return rStrArr.size() - 1;
}

sal_uInt16 SvNFLanguageData::GetCurrencyFormatStrings(NfWSStringsDtor& rStrArr,
                                                      const NfCurrencyEntry& rCurr,
                                                      bool bBank) const
{
    OUString aRed = "["
                  + pFormatScanner->GetRedString()
                  + "]";

    sal_uInt16 nDefault = 0;
    if ( bBank )
    {
        // Only bank symbols.
        OUString aPositiveBank = rCurr.BuildPositiveFormatString(true, *xLocaleData);
        OUString aNegativeBank = rCurr.BuildNegativeFormatString(true, *xLocaleData );

        OUString format1 = aPositiveBank
                         + ";"
                         + aNegativeBank;
        addToCurrencyFormatsList( rStrArr, format1);

        OUString format2 = aPositiveBank
                         + ";"
                         + aRed
                         + aNegativeBank;
        nDefault = addToCurrencyFormatsList( rStrArr, format2);
    }
    else
    {
        // Mixed formats like in SvNumberFormatter::ImpGenerateFormats() but no
        // duplicates if no decimals in currency.
        OUString aPositive = rCurr.BuildPositiveFormatString(false, *xLocaleData );
        OUString aNegative = rCurr.BuildNegativeFormatString(false, *xLocaleData );
        OUString format1;
        OUString format2;
        OUString format3;
        OUString format4;
        OUString format5;
        if (rCurr.GetDigits())
        {
            OUString aPositiveNoDec = rCurr.BuildPositiveFormatString(false, *xLocaleData, 0);
            OUString aNegativeNoDec = rCurr.BuildNegativeFormatString(false, *xLocaleData, 0 );
            OUString aPositiveDashed = rCurr.BuildPositiveFormatString(false, *xLocaleData, 2);
            OUString aNegativeDashed = rCurr.BuildNegativeFormatString(false, *xLocaleData, 2);

            format1 = aPositiveNoDec
                    + ";"
                    + aNegativeNoDec;

            format3 = aPositiveNoDec
                    + ";"
                    + aRed
                    + aNegativeNoDec;

            format5 = aPositiveDashed
                    + ";"
                    + aRed
                    + aNegativeDashed;
        }

        format2 = aPositive
                + ";"
                + aNegative;

        format4 = aPositive
                + ";"
                + aRed
                + aNegative;

        if (rCurr.GetDigits())
        {
            addToCurrencyFormatsList( rStrArr, format1);
        }
        addToCurrencyFormatsList( rStrArr, format2);
        if (rCurr.GetDigits())
        {
            addToCurrencyFormatsList( rStrArr, format3);
        }
        nDefault = addToCurrencyFormatsList( rStrArr, format4);
        if (rCurr.GetDigits())
        {
            addToCurrencyFormatsList( rStrArr, format5);
        }
    }
    return nDefault;
}

sal_uInt16 SvNumberFormatter::GetCurrencyFormatStrings( NfWSStringsDtor& rStrArr,
                                                        const NfCurrencyEntry& rCurr,
                                                        bool bBank ) const
{
    ::osl::MutexGuard aGuard(GetInstanceMutex());
    return m_aCurrentLanguage.GetCurrencyFormatStrings(rStrArr, rCurr, bBank);
}

sal_uInt32 SvNumberFormatter::GetMergeFormatIndex( sal_uInt32 nOldFmt ) const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    if (pMergeTable)
    {
        SvNumberFormatterIndexTable::const_iterator it = pMergeTable->find(nOldFmt);
        if (it != pMergeTable->end())
        {
            return it->second;
        }
    }
    return nOldFmt;
}

bool SvNumberFormatter::HasMergeFormatTable() const
{
    ::osl::MutexGuard aGuard( GetInstanceMutex() );
    return pMergeTable && !pMergeTable->empty();
}

// static
sal_uInt16 SvNumberFormatter::ExpandTwoDigitYear( sal_uInt16 nYear, sal_uInt16 nTwoDigitYearStart )
{
    if ( nYear < 100 )
    {
        if ( nYear < (nTwoDigitYearStart % 100) )
        {
            return nYear + (((nTwoDigitYearStart / 100) + 1) * 100);
        }
        else
        {
            return nYear + ((nTwoDigitYearStart / 100) * 100);
        }
    }
    return nYear;
}

NfCurrencyEntry::NfCurrencyEntry( const LocaleDataWrapper& rLocaleData, LanguageType eLang )
{
    aSymbol         = rLocaleData.getCurrSymbol();
    aBankSymbol     = rLocaleData.getCurrBankSymbol();
    eLanguage       = eLang;
    nPositiveFormat = rLocaleData.getCurrPositiveFormat();
    nNegativeFormat = rLocaleData.getCurrNegativeFormat();
    nDigits         = rLocaleData.getCurrDigits();
    cZeroChar       = rLocaleData.getCurrZeroChar();
}


NfCurrencyEntry::NfCurrencyEntry( const css::i18n::Currency & rCurr,
                                  const LocaleDataWrapper& rLocaleData, LanguageType eLang )
{
    aSymbol         = rCurr.Symbol;
    aBankSymbol     = rCurr.BankSymbol;
    eLanguage       = eLang;
    nPositiveFormat = rLocaleData.getCurrPositiveFormat();
    nNegativeFormat = rLocaleData.getCurrNegativeFormat();
    nDigits         = rCurr.DecimalPlaces;
    cZeroChar       = rLocaleData.getCurrZeroChar();
}

bool NfCurrencyEntry::operator==( const NfCurrencyEntry& r ) const
{
    return aSymbol      == r.aSymbol
        && aBankSymbol  == r.aBankSymbol
        && eLanguage    == r.eLanguage
        ;
}

OUString NfCurrencyEntry::BuildSymbolString(bool bBank,
                                            bool bWithoutExtension) const
{
    OUStringBuffer aBuf("[$");
    if (bBank)
    {
        aBuf.append(aBankSymbol);
    }
    else
    {
        if ( aSymbol.indexOf( '-' ) >= 0 ||
             aSymbol.indexOf( ']' ) >= 0)
        {
            aBuf.append("\"" + aSymbol + "\"");
        }
        else
        {
            aBuf.append(aSymbol);
        }
        if ( !bWithoutExtension && eLanguage != LANGUAGE_DONTKNOW && eLanguage != LANGUAGE_SYSTEM )
        {
            sal_Int32 nLang = static_cast<sal_uInt16>(eLanguage);
            aBuf.append("-" + OUString::number(nLang, 16).toAsciiUpperCase());
        }
    }
    aBuf.append(']');
    return aBuf.makeStringAndClear();
}

OUString NfCurrencyEntry::Impl_BuildFormatStringNumChars( const LocaleDataWrapper& rLoc,
                                                          sal_uInt16 nDecimalFormat) const
{
    OUStringBuffer aBuf("#" + rLoc.getNumThousandSep() + "##0");
    if (nDecimalFormat && nDigits)
    {
        aBuf.append(rLoc.getNumDecimalSep());
        sal_Unicode cDecimalChar = nDecimalFormat == 2 ? '-' : cZeroChar;
        for (sal_uInt16 i = 0; i < nDigits; ++i)
        {
            aBuf.append(cDecimalChar);
        }
    }
    return aBuf.makeStringAndClear();
}


OUString NfCurrencyEntry::BuildPositiveFormatString(bool bBank, const LocaleDataWrapper& rLoc,
                                                    sal_uInt16 nDecimalFormat) const
{
    OUStringBuffer sBuf(Impl_BuildFormatStringNumChars(rLoc, nDecimalFormat));
    sal_uInt16 nPosiForm = NfCurrencyEntry::GetEffectivePositiveFormat( rLoc.getCurrPositiveFormat(),
                                                                        nPositiveFormat, bBank );
    CompletePositiveFormatString(sBuf, bBank, nPosiForm);
    return sBuf.makeStringAndClear();
}


OUString NfCurrencyEntry::BuildNegativeFormatString(bool bBank,
            const LocaleDataWrapper& rLoc, sal_uInt16 nDecimalFormat ) const
{
    OUStringBuffer sBuf(Impl_BuildFormatStringNumChars(rLoc, nDecimalFormat));
    sal_uInt16 nNegaForm = NfCurrencyEntry::GetEffectiveNegativeFormat( rLoc.getCurrNegativeFormat(),
                                                                        nNegativeFormat, bBank );
    CompleteNegativeFormatString(sBuf, bBank, nNegaForm);
    return sBuf.makeStringAndClear();
}


void NfCurrencyEntry::CompletePositiveFormatString(OUStringBuffer& rStr, bool bBank,
                                                   sal_uInt16 nPosiForm) const
{
    OUString aSymStr = BuildSymbolString(bBank);
    NfCurrencyEntry::CompletePositiveFormatString( rStr, aSymStr, nPosiForm );
}


void NfCurrencyEntry::CompleteNegativeFormatString(OUStringBuffer& rStr, bool bBank,
                                                   sal_uInt16 nNegaForm) const
{
    OUString aSymStr = BuildSymbolString(bBank);
    NfCurrencyEntry::CompleteNegativeFormatString( rStr, aSymStr, nNegaForm );
}


// static
void NfCurrencyEntry::CompletePositiveFormatString(OUStringBuffer& rStr, std::u16string_view rSymStr,
                                                   sal_uInt16 nPositiveFormat)
{
    switch( nPositiveFormat )
    {
        case 0:                                         // $1
            rStr.insert(0, rSymStr);
        break;
        case 1:                                         // 1$
            rStr.append(rSymStr);
        break;
        case 2:                                         // $ 1
        {
            rStr.insert(0, OUString::Concat(rSymStr) + " ");
        }
        break;
        case 3:                                         // 1 $
        {
            rStr.append(' ');
            rStr.append(rSymStr);
        }
        break;
        default:
            SAL_WARN( "svl.numbers", "NfCurrencyEntry::CompletePositiveFormatString: unknown option");
        break;
    }
}


// static
void NfCurrencyEntry::CompleteNegativeFormatString(OUStringBuffer& rStr,
                                                   std::u16string_view rSymStr,
                                                   sal_uInt16 nNegaForm)
{
    switch( nNegaForm )
    {
        case 0:                                         // ($1)
        {
            rStr.insert(0, OUString::Concat("(") + rSymStr);
            rStr.append(')');
        }
        break;
        case 1:                                         // -$1
        {
            rStr.insert(0, OUString::Concat("-") + rSymStr);
        }
        break;
        case 2:                                         // $-1
        {
            rStr.insert(0, OUString::Concat(rSymStr) + "-");
        }
        break;
        case 3:                                         // $1-
        {
            rStr.insert(0, rSymStr);
            rStr.append('-');
        }
        break;
        case 4:                                         // (1$)
        {
            rStr.insert(0, '(');
            rStr.append(rSymStr);
            rStr.append(')');
        }
        break;
        case 5:                                         // -1$
        {
            rStr.append(rSymStr);
            rStr.insert(0, '-');
        }
        break;
        case 6:                                         // 1-$
        {
            rStr.append('-');
            rStr.append(rSymStr);
        }
        break;
        case 7:                                         // 1$-
        {
            rStr.append(rSymStr);
            rStr.append('-');
        }
        break;
        case 8:                                         // -1 $
        {
            rStr.append(' ');
            rStr.append(rSymStr);
            rStr.insert(0, '-');
        }
        break;
        case 9:                                         // -$ 1
        {
            rStr.insert(0, OUString::Concat("-") + rSymStr + " ");
        }
        break;
        case 10:                                        // 1 $-
        {
            rStr.append(' ');
            rStr.append(rSymStr);
            rStr.append('-');
        }
        break;
        case 11:                                        // $ -1
        {
            rStr.insert(0, OUString::Concat(rSymStr) + " -");
        }
        break;
        case 12 :                                       // $ 1-
        {
            rStr.insert(0, OUString::Concat(rSymStr) + " ");
            rStr.append('-');
        }
        break;
        case 13 :                                       // 1- $
        {
            rStr.append('-');
            rStr.append(' ');
            rStr.append(rSymStr);
        }
        break;
        case 14 :                                       // ($ 1)
        {
            rStr.insert(0, OUString::Concat("(") + rSymStr + " ");
            rStr.append(')');
        }
        break;
        case 15 :                                       // (1 $)
        {
            rStr.insert(0, '(');
            rStr.append(' ');
            rStr.append(rSymStr);
            rStr.append(')');
        }
        break;
        default:
            SAL_WARN( "svl.numbers", "NfCurrencyEntry::CompleteNegativeFormatString: unknown option");
        break;
    }
}


// static
sal_uInt16 NfCurrencyEntry::GetEffectivePositiveFormat( sal_uInt16 nIntlFormat,
                                                        sal_uInt16 nCurrFormat, bool bBank )
{
    if ( bBank )
    {
#if NF_BANKSYMBOL_FIX_POSITION
        (void) nIntlFormat; // avoid warnings
        return 3;
#else
        switch ( nIntlFormat )
        {
        case 0:                                         // $1
            nIntlFormat = 2;                            // $ 1
            break;
        case 1:                                         // 1$
            nIntlFormat = 3;                            // 1 $
            break;
        case 2:                                         // $ 1
            break;
        case 3:                                         // 1 $
            break;
        default:
            SAL_WARN( "svl.numbers", "NfCurrencyEntry::GetEffectivePositiveFormat: unknown option");
            break;
        }
        return nIntlFormat;
#endif
    }
    else
        return nCurrFormat;
}


//! Call this only if nCurrFormat is really with parentheses!
static sal_uInt16 lcl_MergeNegativeParenthesisFormat( sal_uInt16 nIntlFormat, sal_uInt16 nCurrFormat )
{
    short nSign = 0;        // -1:=bracket 0:=left, 1:=middle, 2:=right
    switch ( nIntlFormat )
    {
    case 0:                                         // ($1)
    case 4:                                         // (1$)
    case 14 :                                       // ($ 1)
    case 15 :                                       // (1 $)
        return nCurrFormat;
    case 1:                                         // -$1
    case 5:                                         // -1$
    case 8:                                         // -1 $
    case 9:                                         // -$ 1
        nSign = 0;
        break;
    case 2:                                         // $-1
    case 6:                                         // 1-$
    case 11 :                                       // $ -1
    case 13 :                                       // 1- $
        nSign = 1;
        break;
    case 3:                                         // $1-
    case 7:                                         // 1$-
    case 10:                                        // 1 $-
    case 12 :                                       // $ 1-
        nSign = 2;
        break;
    default:
        SAL_WARN( "svl.numbers", "lcl_MergeNegativeParenthesisFormat: unknown option");
        break;
    }

    switch ( nCurrFormat )
    {
    case 0:                                         // ($1)
        switch ( nSign )
        {
        case 0:
            return 1;                           // -$1
        case 1:
            return 2;                           // $-1
        case 2:
            return 3;                           // $1-
        }
        break;
    case 4:                                         // (1$)
        switch ( nSign )
        {
        case 0:
            return 5;                           // -1$
        case 1:
            return 6;                           // 1-$
        case 2:
            return 7;                           // 1$-
        }
        break;
    case 14 :                                       // ($ 1)
        switch ( nSign )
        {
        case 0:
            return 9;                           // -$ 1
        case 1:
            return 11;                          // $ -1
        case 2:
            return 12;                          // $ 1-
        }
        break;
    case 15 :                                       // (1 $)
        switch ( nSign )
        {
        case 0:
            return 8;                           // -1 $
        case 1:
            return 13;                          // 1- $
        case 2:
            return 10;                          // 1 $-
        }
        break;
    }
    return nCurrFormat;
}


// static
sal_uInt16 NfCurrencyEntry::GetEffectiveNegativeFormat( sal_uInt16 nIntlFormat,
            sal_uInt16 nCurrFormat, bool bBank )
{
    if ( bBank )
    {
#if NF_BANKSYMBOL_FIX_POSITION
        return 8;
#else
        switch ( nIntlFormat )
        {
        case 0:                                         // ($1)
//          nIntlFormat = 14;                           // ($ 1)
            nIntlFormat = 9;                            // -$ 1
            break;
        case 1:                                         // -$1
            nIntlFormat = 9;                            // -$ 1
            break;
        case 2:                                         // $-1
            nIntlFormat = 11;                           // $ -1
            break;
        case 3:                                         // $1-
            nIntlFormat = 12;                           // $ 1-
            break;
        case 4:                                         // (1$)
//          nIntlFormat = 15;                           // (1 $)
            nIntlFormat = 8;                            // -1 $
            break;
        case 5:                                         // -1$
            nIntlFormat = 8;                            // -1 $
            break;
        case 6:                                         // 1-$
            nIntlFormat = 13;                           // 1- $
            break;
        case 7:                                         // 1$-
            nIntlFormat = 10;                           // 1 $-
            break;
        case 8:                                         // -1 $
            break;
        case 9:                                         // -$ 1
            break;
        case 10:                                        // 1 $-
            break;
        case 11:                                        // $ -1
            break;
        case 12 :                                       // $ 1-
            break;
        case 13 :                                       // 1- $
            break;
        case 14 :                                       // ($ 1)
//          nIntlFormat = 14;                           // ($ 1)
            nIntlFormat = 9;                            // -$ 1
            break;
        case 15 :                                       // (1 $)
//          nIntlFormat = 15;                           // (1 $)
            nIntlFormat = 8;                            // -1 $
            break;
        default:
            SAL_WARN( "svl.numbers", "NfCurrencyEntry::GetEffectiveNegativeFormat: unknown option");
            break;
        }
#endif
    }
    else if ( nIntlFormat != nCurrFormat )
    {
        switch ( nCurrFormat )
        {
        case 0:                                         // ($1)
            nIntlFormat = lcl_MergeNegativeParenthesisFormat(
                nIntlFormat, nCurrFormat );
            break;
        case 1:                                         // -$1
            nIntlFormat = nCurrFormat;
            break;
        case 2:                                         // $-1
            nIntlFormat = nCurrFormat;
            break;
        case 3:                                         // $1-
            nIntlFormat = nCurrFormat;
            break;
        case 4:                                         // (1$)
            nIntlFormat = lcl_MergeNegativeParenthesisFormat(
                nIntlFormat, nCurrFormat );
            break;
        case 5:                                         // -1$
            nIntlFormat = nCurrFormat;
            break;
        case 6:                                         // 1-$
            nIntlFormat = nCurrFormat;
            break;
        case 7:                                         // 1$-
            nIntlFormat = nCurrFormat;
            break;
        case 8:                                         // -1 $
            nIntlFormat = nCurrFormat;
            break;
        case 9:                                         // -$ 1
            nIntlFormat = nCurrFormat;
            break;
        case 10:                                        // 1 $-
            nIntlFormat = nCurrFormat;
            break;
        case 11:                                        // $ -1
            nIntlFormat = nCurrFormat;
            break;
        case 12 :                                       // $ 1-
            nIntlFormat = nCurrFormat;
            break;
        case 13 :                                       // 1- $
            nIntlFormat = nCurrFormat;
            break;
        case 14 :                                       // ($ 1)
            nIntlFormat = lcl_MergeNegativeParenthesisFormat(
                nIntlFormat, nCurrFormat );
            break;
        case 15 :                                       // (1 $)
            nIntlFormat = lcl_MergeNegativeParenthesisFormat(
                nIntlFormat, nCurrFormat );
            break;
        default:
            SAL_WARN( "svl.numbers", "NfCurrencyEntry::GetEffectiveNegativeFormat: unknown option");
            break;
        }
    }
    return nIntlFormat;
}

const NfKeywordTable & SvNumberFormatter::GetKeywords( sal_uInt32 nKey )
{
    osl::MutexGuard aGuard( GetInstanceMutex() );
    const SvNumberformat* pFormat = m_aFormatData.GetFormatEntry( nKey);
    if (pFormat)
        ChangeIntl( pFormat->GetLanguage());
    else
        ChangeIntl( IniLnge);
    return m_aCurrentLanguage.pFormatScanner->GetKeywords();
}

// static
const NfKeywordTable & SvNumberFormatter::GetEnglishKeywords()
{
    return ImpSvNumberformatScan::GetEnglishKeywords();
}

// static
const std::vector<Color> & SvNumberFormatter::GetStandardColors()
{
    return ImpSvNumberformatScan::GetStandardColors();
}

// static
size_t SvNumberFormatter::GetMaxDefaultColors()
{
    return ImpSvNumberformatScan::GetMaxDefaultColors();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
