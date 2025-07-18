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

#include "rtattributehandler.hxx"

#include <osl/diagnose.h>
#include <svx/svxids.hrc>
#include <editeng/eeitem.hxx>
#include <svl/itemset.hxx>
#include <svl/itempool.hxx>
#include <tools/mapunit.hxx>
#include <vcl/mapmod.hxx>
#include <vcl/outdev.hxx>

#include <editeng/adjustitem.hxx>
#include <editeng/escapementitem.hxx>
#include <editeng/lspcitem.hxx>
#include <editeng/fhgtitem.hxx>
#include <editeng/frmdiritem.hxx>
#include <editeng/scriptsetitem.hxx>


namespace frm
{


    AttributeHandler::AttributeHandler( AttributeId _nAttributeId, WhichId _nWhichId )
        :m_nAttribute( _nAttributeId )
        ,m_nWhich    ( _nWhichId     )
    {
    }


    AttributeHandler::~AttributeHandler()
    {
    }


    AttributeId AttributeHandler::getAttributeId( ) const
    {
        return getAttribute();
    }


    AttributeCheckState AttributeHandler::implGetCheckState( const SfxPoolItem& /*_rItem*/ ) const
    {
        OSL_FAIL( "AttributeHandler::implGetCheckState: not to be called!" );
        return eIndetermined;
    }


    void AttributeHandler::putItemForScript( SfxItemSet& _rAttribs, const SfxPoolItem& _rItem, SvtScriptType _nForScriptType ) const
    {
        SvxScriptSetItem aSetItem( static_cast<WhichId>(getAttributeId()), *_rAttribs.GetPool() );
        aSetItem.PutItemForScriptType( _nForScriptType, _rItem );
        _rAttribs.Put( aSetItem.GetItemSet(), false );
    }


    AttributeCheckState AttributeHandler::getCheckState( const SfxItemSet& _rAttribs ) const
    {
        AttributeCheckState eSimpleState( eIndetermined );
        const SfxPoolItem* pItem = _rAttribs.GetItem( getWhich() );
        if ( pItem )
            eSimpleState = implGetCheckState( *pItem );
        return eSimpleState;
    }


    AttributeState AttributeHandler::getState( const SfxItemSet& _rAttribs ) const
    {
        AttributeState aState( eIndetermined );
        aState.eSimpleState = getCheckState( _rAttribs );
        return aState;
    }


    //= AttributeHandlerFactory


    namespace
    {
        WhichId lcl_implGetWhich( const SfxItemPool& _rPool, AttributeId _nAttributeId )
        {
            WhichId nWhich = 0;
            switch ( _nAttributeId )
            {
            case SID_ATTR_CHAR_LATIN_FONTHEIGHT:nWhich = EE_CHAR_FONTHEIGHT;break;
            case SID_ATTR_CHAR_LATIN_FONT:      nWhich = EE_CHAR_FONTINFO;  break;
            case SID_ATTR_CHAR_LATIN_LANGUAGE:  nWhich = EE_CHAR_LANGUAGE;  break;
            case SID_ATTR_CHAR_LATIN_POSTURE:   nWhich = EE_CHAR_ITALIC;    break;
            case SID_ATTR_CHAR_LATIN_WEIGHT:    nWhich = EE_CHAR_WEIGHT;    break;

            default:
                nWhich = _rPool.GetWhichIDFromSlotID( static_cast<SfxSlotId>(_nAttributeId) );
            }
            return nWhich;
        }
    }

    ::rtl::Reference< AttributeHandler > AttributeHandlerFactory::getHandlerFor( AttributeId _nAttributeId, const SfxItemPool& _rEditEnginePool )
    {
        ::rtl::Reference< AttributeHandler > pReturn;
        switch ( _nAttributeId )
        {
        case SID_ATTR_PARA_ADJUST_LEFT  :
        case SID_ATTR_PARA_ADJUST_CENTER:
        case SID_ATTR_PARA_ADJUST_RIGHT :
        case SID_ATTR_PARA_ADJUST_BLOCK :
            pReturn = new ParaAlignmentHandler( _nAttributeId );
            break;

        case SID_ATTR_PARA_LINESPACE_10:
        case SID_ATTR_PARA_LINESPACE_15:
        case SID_ATTR_PARA_LINESPACE_20:
            pReturn = new LineSpacingHandler( _nAttributeId );
            break;

        case SID_SET_SUPER_SCRIPT:
        case SID_SET_SUB_SCRIPT:
            pReturn = new EscapementHandler( _nAttributeId );
            break;

        case SID_ATTR_CHAR_FONTHEIGHT:
        case SID_ATTR_CHAR_CTL_FONTHEIGHT:
        case SID_ATTR_CHAR_CJK_FONTHEIGHT:
        case SID_ATTR_CHAR_LATIN_FONTHEIGHT:
            pReturn = new FontSizeHandler( _nAttributeId, lcl_implGetWhich( _rEditEnginePool, _nAttributeId ) );
            break;

        case SID_ATTR_PARA_LEFT_TO_RIGHT:
        case SID_ATTR_PARA_RIGHT_TO_LEFT:
            pReturn = new ParagraphDirectionHandler( _nAttributeId );
            break;

        case SID_ATTR_PARA_HANGPUNCTUATION:
        case SID_ATTR_PARA_FORBIDDEN_RULES:
        case SID_ATTR_PARA_SCRIPTSPACE:
            pReturn = new BooleanHandler( _nAttributeId, lcl_implGetWhich( _rEditEnginePool, _nAttributeId ) );
            break;

        default:
            pReturn = new SlotHandler( static_cast<SfxSlotId>(_nAttributeId), lcl_implGetWhich( _rEditEnginePool, _nAttributeId ) );
            break;

        }

        return pReturn;
    }

    ParaAlignmentHandler::ParaAlignmentHandler( AttributeId _nAttributeId )
        :AttributeHandler( _nAttributeId, EE_PARA_JUST )
        ,m_eAdjust( SvxAdjust::Center )
    {
        switch ( getAttribute() )
        {
            case SID_ATTR_PARA_ADJUST_LEFT  : m_eAdjust = SvxAdjust::Left;    break;
            case SID_ATTR_PARA_ADJUST_CENTER: m_eAdjust = SvxAdjust::Center;  break;
            case SID_ATTR_PARA_ADJUST_RIGHT : m_eAdjust = SvxAdjust::Right;   break;
            case SID_ATTR_PARA_ADJUST_BLOCK : m_eAdjust = SvxAdjust::Block;   break;
            default:
                OSL_FAIL( "ParaAlignmentHandler::ParaAlignmentHandler: invalid slot!" );
                break;
        }
    }


    AttributeCheckState ParaAlignmentHandler::implGetCheckState( const SfxPoolItem& _rItem ) const
    {
        assert( dynamic_cast<const SvxAdjustItem*>( &_rItem) && "ParaAlignmentHandler::implGetCheckState: invalid pool item!" );
        SvxAdjust eAdjust = static_cast< const SvxAdjustItem& >( _rItem ).GetAdjust();
        return ( eAdjust == m_eAdjust ) ? eChecked : eUnchecked;
    }


    void ParaAlignmentHandler::executeAttribute( const SfxItemSet& /*_rCurrentAttribs*/, SfxItemSet& _rNewAttribs, const SfxPoolItem* _pAdditionalArg, SvtScriptType /*_nForScriptType*/ ) const
    {
        OSL_ENSURE( !_pAdditionalArg, "ParaAlignmentHandler::executeAttribute: this is a simple toggle attribute - no args possible!" );
        _rNewAttribs.Put( SvxAdjustItem( m_eAdjust, getWhich() ) );
    }

    LineSpacingHandler::LineSpacingHandler( AttributeId _nAttributeId )
        :AttributeHandler( _nAttributeId, EE_PARA_SBL )
        ,m_nLineSpace( 100 )
    {
        switch ( getAttribute() )
        {
            case SID_ATTR_PARA_LINESPACE_10: m_nLineSpace = 100; break;
            case SID_ATTR_PARA_LINESPACE_15: m_nLineSpace = 150; break;
            case SID_ATTR_PARA_LINESPACE_20: m_nLineSpace = 200; break;
            default:
                OSL_FAIL( "LineSpacingHandler::LineSpacingHandler: invalid slot!" );
                break;
        }
    }


    AttributeCheckState LineSpacingHandler::implGetCheckState( const SfxPoolItem& _rItem ) const
    {
        assert( dynamic_cast<const SvxLineSpacingItem*>( &_rItem) && "LineSpacingHandler::implGetCheckState: invalid pool item!" );
        sal_uInt16 nLineSpace = static_cast< const SvxLineSpacingItem& >( _rItem ).GetPropLineSpace();
        return ( nLineSpace == m_nLineSpace ) ? eChecked : eUnchecked;
    }


    void LineSpacingHandler::executeAttribute( const SfxItemSet& /*_rCurrentAttribs*/, SfxItemSet& _rNewAttribs, const SfxPoolItem* _pAdditionalArg, SvtScriptType /*_nForScriptType*/ ) const
    {
        OSL_ENSURE( !_pAdditionalArg, "LineSpacingHandler::executeAttribute: this is a simple toggle attribute - no args possible!" );

        SvxLineSpacingItem aLineSpacing( m_nLineSpace, getWhich() );
        aLineSpacing.SetLineSpaceRule( SvxLineSpaceRule::Auto );
        if ( 100 == m_nLineSpace )
            aLineSpacing.SetInterLineSpaceRule( SvxInterLineSpaceRule::Off );
        else
            aLineSpacing.SetPropLineSpace( m_nLineSpace );

        _rNewAttribs.Put( aLineSpacing );
    }

    EscapementHandler::EscapementHandler( AttributeId _nAttributeId )
        :AttributeHandler( _nAttributeId, EE_CHAR_ESCAPEMENT )
        ,m_eEscapement( SvxEscapement::Off )
    {
        switch ( getAttribute() )
        {
            case SID_SET_SUPER_SCRIPT   : m_eEscapement = SvxEscapement::Superscript; break;
            case SID_SET_SUB_SCRIPT     : m_eEscapement = SvxEscapement::Subscript;   break;
            default:
                OSL_FAIL( "EscapementHandler::EscapementHandler: invalid slot!" );
                break;
        }
    }


    AttributeCheckState EscapementHandler::implGetCheckState( const SfxPoolItem& _rItem ) const
    {
        assert( dynamic_cast<const SvxEscapementItem*>( &_rItem) && "EscapementHandler::getState: invalid pool item!" );
        SvxEscapement eEscapement = static_cast< const SvxEscapementItem& >( _rItem ).GetEscapement();
        return ( eEscapement == m_eEscapement ) ? eChecked : eUnchecked;
    }


    void EscapementHandler::executeAttribute( const SfxItemSet& _rCurrentAttribs, SfxItemSet& _rNewAttribs, const SfxPoolItem* _pAdditionalArg, SvtScriptType /*_nForScriptType*/ ) const    {
        OSL_ENSURE( !_pAdditionalArg, "EscapementHandler::executeAttribute: this is a simple toggle attribute - no args possible!" );
            // well, in theory we could allow an SvxEscapementItem here, but this is not needed

        bool bIsChecked = getCheckState( _rCurrentAttribs ) == eChecked;
        _rNewAttribs.Put( SvxEscapementItem( bIsChecked ? SvxEscapement::Off : m_eEscapement, getWhich() ) );
    }

    SlotHandler::SlotHandler( AttributeId _nAttributeId, WhichId _nWhichId )
        :AttributeHandler( _nAttributeId, _nWhichId )
        ,m_bScriptDependent( false )
    {
        m_bScriptDependent = ( sal_uInt16(SID_ATTR_CHAR_WEIGHT) == _nAttributeId )
                         ||  ( sal_uInt16(SID_ATTR_CHAR_POSTURE) == _nAttributeId )
                         ||  ( sal_uInt16(SID_ATTR_CHAR_FONT) == _nAttributeId );
    }


    AttributeState SlotHandler::getState( const SfxItemSet& _rAttribs ) const
    {
        AttributeState aState( eIndetermined );

        const SfxPoolItem* pItem = _rAttribs.GetItem( getWhich() );
        if ( pItem )
            aState.setItem( pItem );

        return aState;
    }


    void SlotHandler::executeAttribute( const SfxItemSet& /*_rCurrentAttribs*/, SfxItemSet& _rNewAttribs, const SfxPoolItem* _pAdditionalArg, SvtScriptType _nForScriptType ) const
    {
        if ( _pAdditionalArg )
        {
            std::unique_ptr<SfxPoolItem> pCorrectWich(_pAdditionalArg->CloneSetWhich(getWhich()));

            if ( m_bScriptDependent )
                putItemForScript( _rNewAttribs, *pCorrectWich, _nForScriptType );
            else
                _rNewAttribs.Put( std::move(pCorrectWich) );
        }
        else
            OSL_FAIL( "SlotHandler::executeAttribute: need attributes to do something!" );
    }

    FontSizeHandler::FontSizeHandler( AttributeId _nAttributeId, WhichId _nWhichId )
        :AttributeHandler( _nAttributeId, _nWhichId )
    {
        OSL_ENSURE( ( _nAttributeId == sal_uInt16(SID_ATTR_CHAR_FONTHEIGHT) ) || ( _nAttributeId == sal_uInt16(SID_ATTR_CHAR_CTL_FONTHEIGHT) )
            || ( _nAttributeId == sal_uInt16(SID_ATTR_CHAR_CJK_FONTHEIGHT) ) || ( _nAttributeId == sal_uInt16(SID_ATTR_CHAR_LATIN_FONTHEIGHT) ),
            "FontSizeHandler::FontSizeHandler: invalid attribute id!" );
    }


    AttributeState FontSizeHandler::getState( const SfxItemSet& _rAttribs ) const
    {
        AttributeState aState( eIndetermined );

        const SfxPoolItem* pItem = _rAttribs.GetItem( getWhich() );
        const SvxFontHeightItem* pFontHeightItem = dynamic_cast<const SvxFontHeightItem*>( pItem  );
        OSL_ENSURE( pFontHeightItem || !pItem, "FontSizeHandler::getState: invalid item!" );
        if ( pFontHeightItem )
        {
            // by definition, the item should have the unit twip
            sal_uInt32 nHeight = pFontHeightItem->GetHeight();
            if ( _rAttribs.GetPool()->GetMetric( getWhich() ) != MapUnit::MapTwip )
            {
                nHeight = OutputDevice::LogicToLogic(
                    Size( 0, nHeight ),
                    MapMode(  _rAttribs.GetPool()->GetMetric( getWhich() ) ),
                    MapMode( MapUnit::MapTwip )
                ).Height();
            }

            SvxFontHeightItem* pNewItem = new SvxFontHeightItem( nHeight, 100, getWhich() );
            pNewItem->SetProp( pFontHeightItem->GetProp(), pFontHeightItem->GetPropUnit() );
            aState.setItem( pNewItem );
        }

        return aState;
    }


    void FontSizeHandler::executeAttribute( const SfxItemSet& /*_rCurrentAttribs*/, SfxItemSet& _rNewAttribs, const SfxPoolItem* _pAdditionalArg, SvtScriptType _nForScriptType ) const
    {
        const SvxFontHeightItem* pFontHeightItem = dynamic_cast<const SvxFontHeightItem*>( _pAdditionalArg  );
        OSL_ENSURE( pFontHeightItem, "FontSizeHandler::executeAttribute: need a FontHeightItem!" );

        if ( !pFontHeightItem )
            return;

        sal_uInt32 nHeight = pFontHeightItem->GetHeight();
        if ( _rNewAttribs.GetPool()->GetMetric( getWhich() ) != MapUnit::MapTwip )
        {
            nHeight = OutputDevice::LogicToLogic(
                Size( 0, nHeight ),
                MapMode( MapUnit::MapTwip ),
                MapMode( _rNewAttribs.GetPool()->GetMetric( getWhich() ) )
            ).Height();
        }

        SvxFontHeightItem aNewItem( nHeight, 100, getWhich() );
        aNewItem.SetProp( pFontHeightItem->GetProp(), pFontHeightItem->GetPropUnit() );

        if ( ( getAttributeId() == sal_uInt16(SID_ATTR_CHAR_FONTHEIGHT) ) && _nForScriptType != SvtScriptType::NONE)
            putItemForScript( _rNewAttribs, aNewItem, _nForScriptType );
        else
            _rNewAttribs.Put( aNewItem );
    }

    ParagraphDirectionHandler::ParagraphDirectionHandler( AttributeId _nAttributeId )
        :AttributeHandler( _nAttributeId, EE_PARA_WRITINGDIR )
        ,m_eParagraphDirection( SvxFrameDirection::Horizontal_LR_TB )
        ,m_eDefaultAdjustment( SvxAdjust::Right )
        ,m_eOppositeDefaultAdjustment( SvxAdjust::Left )
    {
        switch ( getAttributeId() )
        {
            case SID_ATTR_PARA_LEFT_TO_RIGHT: m_eParagraphDirection = SvxFrameDirection::Horizontal_LR_TB; m_eDefaultAdjustment = SvxAdjust::Left; break;
            case SID_ATTR_PARA_RIGHT_TO_LEFT: m_eParagraphDirection = SvxFrameDirection::Horizontal_RL_TB; m_eDefaultAdjustment = SvxAdjust::Right; break;
            default:
                OSL_FAIL( "ParagraphDirectionHandler::ParagraphDirectionHandler: invalid attribute id!" );
        }

        if ( SvxAdjust::Right == m_eDefaultAdjustment )
            m_eOppositeDefaultAdjustment = SvxAdjust::Left;
        else
            m_eOppositeDefaultAdjustment = SvxAdjust::Right;
    }


    AttributeCheckState ParagraphDirectionHandler::implGetCheckState( const SfxPoolItem& _rItem ) const
    {
        assert( dynamic_cast<const SvxFrameDirectionItem*>( &_rItem) && "ParagraphDirectionHandler::implGetCheckState: invalid pool item!" );
        SvxFrameDirection eDirection = static_cast< const SvxFrameDirectionItem& >( _rItem ).GetValue();
        return ( eDirection == m_eParagraphDirection ) ? eChecked : eUnchecked;
    }


    void ParagraphDirectionHandler::executeAttribute( const SfxItemSet& _rCurrentAttribs, SfxItemSet& _rNewAttribs, const SfxPoolItem* /*_pAdditionalArg*/, SvtScriptType /*_nForScriptType*/ ) const
    {
        _rNewAttribs.Put( SvxFrameDirectionItem( m_eParagraphDirection, getWhich() ) );

        // if the current adjustment of the was the default adjustment for the *previous* text direction,
        // then we toggle the adjustment, too
        SvxAdjust eCurrentAdjustment = SvxAdjust::Left;
        if ( const SvxAdjustItem* pCurrentAdjustment =  _rCurrentAttribs.GetItemIfSet( EE_PARA_JUST ) )
            eCurrentAdjustment = pCurrentAdjustment->GetAdjust();

        if ( eCurrentAdjustment == m_eOppositeDefaultAdjustment )
            _rNewAttribs.Put( SvxAdjustItem( m_eDefaultAdjustment, EE_PARA_JUST ) );
    }

    BooleanHandler::BooleanHandler( AttributeId _nAttributeId, WhichId _nWhichId )
        :AttributeHandler( _nAttributeId, _nWhichId )
    {
    }


    AttributeCheckState BooleanHandler::implGetCheckState( const SfxPoolItem& _rItem ) const
    {
        OSL_ENSURE( dynamic_cast<const SfxBoolItem*>( &_rItem) !=  nullptr, "BooleanHandler::implGetCheckState: invalid item!" );
        if ( auto pBoolItem = dynamic_cast<const SfxBoolItem*>( &_rItem) )
            return pBoolItem->GetValue() ? eChecked : eUnchecked;

        return eIndetermined;
    }


    void BooleanHandler::executeAttribute( const SfxItemSet& /*_rCurrentAttribs*/, SfxItemSet& _rNewAttribs, const SfxPoolItem* _pAdditionalArg, SvtScriptType /*_nForScriptType*/ ) const
    {
        OSL_ENSURE( dynamic_cast<const SfxBoolItem*>( _pAdditionalArg) !=  nullptr, "BooleanHandler::executeAttribute: invalid argument!" );
        if ( _pAdditionalArg )
        {
            _rNewAttribs.Put( _pAdditionalArg->CloneSetWhich(getWhich()) );
        }
    }


}   // namespace frm


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
