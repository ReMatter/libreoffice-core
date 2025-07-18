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

#include <hintids.hxx>

#include <com/sun/star/i18n/TextConversionOption.hpp>
#include <com/sun/star/lang/XInitialization.hpp>
#include <com/sun/star/ui/dialogs/XExecutableDialog.hpp>
#include <com/sun/star/awt/XWindow.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/linguistic2/XThesaurus.hpp>

#include <i18nutil/transliteration.hxx>
#include <sfx2/namedcolor.hxx>
#include <sfx2/objface.hxx>
#include <sfx2/viewfrm.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/request.hxx>
#include <editeng/colritem.hxx>
#include <editeng/editund2.hxx>
#include <editeng/eeitem.hxx>
#include <editeng/flstitem.hxx>
#include <editeng/spltitem.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/ulspitem.hxx>
#include <editeng/orphitem.hxx>
#include <editeng/formatbreakitem.hxx>
#include <editeng/widwitem.hxx>
#include <editeng/kernitem.hxx>
#include <editeng/escapementitem.hxx>
#include <editeng/lspcitem.hxx>
#include <editeng/adjustitem.hxx>
#include <editeng/hyphenzoneitem.hxx>
#include <editeng/udlnitem.hxx>
#include <editeng/fontitem.hxx>
#include <svx/clipfmtitem.hxx>
#include <svx/chinese_translation_unodialog.hxx>
#include <svl/stritem.hxx>
#include <svl/slstitm.hxx>
#include <editeng/frmdiritem.hxx>
#include <svl/whiter.hxx>
#include <svl/cjkoptions.hxx>
#include <svl/ctloptions.hxx>
#include <unotools/useroptions.hxx>
#include <editeng/flditem.hxx>
#include <svx/hlnkitem.hxx>
#include <sfx2/htmlmode.hxx>
#include <editeng/langitem.hxx>
#include <editeng/scriptsetitem.hxx>
#include <swundo.hxx>
#include <doc.hxx>
#include <viewopt.hxx>
#include <wrtsh.hxx>
#include <chrdlgmodes.hxx>
#include <edtwin.hxx>
#include <SwRewriter.hxx>

#include <cmdid.h>
#include <strings.hrc>
#include <breakit.hxx>
#include <annotsh.hxx>
#include <view.hxx>
#include <PostItMgr.hxx>
#include <AnnotationWin.hxx>

#include <swtypes.hxx>

#include <svx/svxdlg.hxx>

#include <vcl/EnumContext.hxx>
#include <svl/itempool.hxx>
#include <editeng/outliner.hxx>
#include <editeng/editview.hxx>
#include <osl/diagnose.h>

#include <svl/languageoptions.hxx>

#include <svl/undo.hxx>
#include <swabstdlg.hxx>

#include <comphelper/string.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/propertysequence.hxx>

#include <langhelper.hxx>

#include <memory>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::i18n;

#define ShellClass_SwAnnotationShell

#include <sfx2/msg.hxx>
#include <swslots.hxx>

SFX_IMPL_INTERFACE(SwAnnotationShell, SfxShell)

void SwAnnotationShell::InitInterface_Impl()
{
    GetStaticInterface()->RegisterObjectBar(SFX_OBJECTBAR_OBJECT, SfxVisibilityFlags::Invisible, ToolbarId::Text_Toolbox_Sw);

    GetStaticInterface()->RegisterPopupMenu(u"annotation"_ustr);
}


SfxItemPool* SwAnnotationShell::GetAnnotationPool(SwView const & rV)
{
    SwWrtShell &rSh = rV.GetWrtShell();
    return rSh.GetAttrPool().GetSecondaryPool();
}

SwAnnotationShell::SwAnnotationShell( SwView& r )
    : m_rView(r)
{
    SetPool(SwAnnotationShell::GetAnnotationPool(m_rView));
    SfxShell::SetContextName(vcl::EnumContext::GetContextName(vcl::EnumContext::Context::Annotation));
}

SwAnnotationShell::~SwAnnotationShell()
{
    if (m_rView.GetWrtShell().CanInsert())
        m_rView.ShowCursor(true);
}

SfxUndoManager* SwAnnotationShell::GetUndoManager()
{
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if ( !pPostItMgr ||
         !pPostItMgr->HasActiveSidebarWin() )
    {
        OSL_ENSURE(pPostItMgr,"PostItMgr::Layout(): We are looping forever");
        return nullptr;
    }
    return &pPostItMgr->GetActiveSidebarWin()->GetOutlinerView()->GetOutliner().GetUndoManager();
}

void SwAnnotationShell::Exec( SfxRequest &rReq )
{
    //TODO: clean this up!!!!
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if ( !pPostItMgr || !pPostItMgr->HasActiveSidebarWin() )
        return;

    OutlinerView* pOLV = pPostItMgr->GetActiveSidebarWin()->GetOutlinerView();
    SfxItemSet aEditAttr(pOLV->GetAttribs());
    SfxItemSet aNewAttr(*aEditAttr.GetPool(), aEditAttr.GetRanges());

    const sal_uInt16 nSlot = rReq.GetSlot();
    const sal_uInt16 nWhich = GetPool().GetWhichIDFromSlotID(nSlot);
    const SfxItemSet *pNewAttrs = rReq.GetArgs();
    sal_uInt16 nEEWhich = 0;
    switch (nSlot)
    {
        case SID_PARASPACE_INCREASE:
        case SID_PARASPACE_DECREASE:
        {
            SvxULSpaceItem aULSpace( aEditAttr.Get( EE_PARA_ULSPACE ) );
            sal_uInt16 nUpper = aULSpace.GetUpper();
            sal_uInt16 nLower = aULSpace.GetLower();

            if ( nSlot == SID_PARASPACE_INCREASE )
            {
                nUpper = std::min< sal_uInt16 >( nUpper + 57, 5670 );
                nLower = std::min< sal_uInt16 >( nLower + 57, 5670 );
            }
            else
            {
                nUpper = std::max< sal_Int16 >( nUpper - 57, 0 );
                nLower = std::max< sal_Int16 >( nLower - 57, 0 );
            }

            aULSpace.SetUpper( nUpper );
            aULSpace.SetLower( nLower );
            aNewAttr.Put( aULSpace );
            rReq.Done();
        }
        break;
        case SID_ATTR_PARA_LRSPACE:
            {
                SvxLRSpaceItem aParaMargin(static_cast<const SvxLRSpaceItem&>(rReq.
                                        GetArgs()->Get(nSlot)));
                aParaMargin.SetWhich( EE_PARA_LRSPACE );

                aNewAttr.Put(aParaMargin);
                rReq.Done();
                break;
            }
        case SID_ATTR_PARA_LINESPACE:
            {
                SvxLineSpacingItem aParaMargin = static_cast<const SvxLineSpacingItem&>(pNewAttrs->Get(
                                                            GetPool().GetWhichIDFromSlotID(nSlot)));
                aParaMargin.SetWhich( EE_PARA_SBL );

                aNewAttr.Put(aParaMargin);
                rReq.Done();
                break;
            }
        case SID_ATTR_PARA_ULSPACE:
            {
                SvxULSpaceItem aULSpace = static_cast<const SvxULSpaceItem&>(pNewAttrs->Get(
                    GetPool().GetWhichIDFromSlotID(nSlot)));
                aULSpace.SetWhich( EE_PARA_ULSPACE );
                aNewAttr.Put( aULSpace );
                rReq.Done();
            }
            break;
        case FN_GROW_FONT_SIZE:
        case FN_SHRINK_FONT_SIZE:
        {
            const SfxObjectShell* pObjSh = SfxObjectShell::Current();
            const SvxFontListItem* pFontListItem = static_cast<const SvxFontListItem*>
                    (pObjSh ? pObjSh->GetItem(SID_ATTR_CHAR_FONTLIST) : nullptr);
            const FontList* pFontList = pFontListItem ? pFontListItem->GetFontList() : nullptr;
            pOLV->GetEditView().ChangeFontSize( nSlot == FN_GROW_FONT_SIZE, pFontList );
        }
        break;

        case SID_ATTR_CHAR_FONT:
        case SID_ATTR_CHAR_FONTHEIGHT:
        case SID_ATTR_CHAR_WEIGHT:
        case SID_ATTR_CHAR_POSTURE:
            {
                SfxItemPool* pSecondPool = aEditAttr.GetPool()->GetSecondaryPool();
                if( !pSecondPool )
                    pSecondPool = aEditAttr.GetPool();
                SvxScriptSetItem aSetItem( nSlot, *pSecondPool );
                aSetItem.PutItemForScriptType( pOLV->GetSelectedScriptType(), pNewAttrs->Get( nWhich ));
                aNewAttr.Put( aSetItem.GetItemSet() );
                rReq.Done();
                break;
            }
        case SID_ATTR_CHAR_COLOR: nEEWhich = EE_CHAR_COLOR; break;

        case SID_ATTR_CHAR_COLOR2:
        {
            if (!rReq.GetArgs())
            {
                const std::optional<NamedColor> oColor
                    = m_rView.GetDocShell()->GetRecentColor(SID_ATTR_CHAR_COLOR);
                if (oColor.has_value())
                {
                    nEEWhich = GetPool().GetWhichIDFromSlotID(SID_ATTR_CHAR_COLOR);
                    const model::ComplexColor aCol = (*oColor).getComplexColor();
                    aNewAttr.Put(SvxColorItem(aCol.getFinalColor(), aCol, nEEWhich));
                    rReq.SetArgs(aNewAttr);
                    rReq.SetSlot(SID_ATTR_CHAR_COLOR);
                }
            }
            break;
        }
        case SID_ATTR_CHAR_BACK_COLOR:
        {
            nEEWhich = GetPool().GetWhichIDFromSlotID(nSlot);
            if (!rReq.GetArgs())
            {
                const std::optional<NamedColor> oColor
                    = m_rView.GetDocShell()->GetRecentColor(nSlot);
                if (oColor.has_value())
                {
                    const model::ComplexColor aCol = (*oColor).getComplexColor();
                    aNewAttr.Put(SvxColorItem(aCol.getFinalColor(), aCol, nEEWhich));
                }
            }
            break;
        }
        case SID_ATTR_CHAR_UNDERLINE:
        {
            if( rReq.GetArgs() )
            {
                const SvxUnderlineItem* pItem = rReq.GetArg<SvxUnderlineItem>(SID_ATTR_CHAR_UNDERLINE);
                if (pItem)
                {
                    aNewAttr.Put(*pItem);
                }
                else
                {
                    FontLineStyle eFU = aEditAttr.Get( EE_CHAR_UNDERLINE ).GetLineStyle();
                    aNewAttr.Put( SvxUnderlineItem( eFU != LINESTYLE_NONE ?LINESTYLE_NONE : LINESTYLE_SINGLE,  EE_CHAR_UNDERLINE ) );
                }
            }
            break;
        }
        case SID_ATTR_CHAR_OVERLINE:
        {
            FontLineStyle eFO = aEditAttr.Get(EE_CHAR_OVERLINE).GetLineStyle();
            aNewAttr.Put(SvxOverlineItem(eFO == LINESTYLE_SINGLE ? LINESTYLE_NONE : LINESTYLE_SINGLE, EE_CHAR_OVERLINE));
            break;
        }
        case SID_ATTR_CHAR_CONTOUR:     nEEWhich = EE_CHAR_OUTLINE; break;
        case SID_ATTR_CHAR_SHADOWED:    nEEWhich = EE_CHAR_SHADOW; break;
        case SID_ATTR_CHAR_STRIKEOUT:   nEEWhich = EE_CHAR_STRIKEOUT; break;
        case SID_ATTR_CHAR_WORDLINEMODE: nEEWhich = EE_CHAR_WLM; break;
        case SID_ATTR_CHAR_RELIEF      : nEEWhich = EE_CHAR_RELIEF;  break;
        case SID_ATTR_CHAR_LANGUAGE    : nEEWhich = EE_CHAR_LANGUAGE;break;
        case SID_ATTR_CHAR_KERNING     : nEEWhich = EE_CHAR_KERNING; break;
        case SID_ATTR_CHAR_SCALEWIDTH:   nEEWhich = EE_CHAR_FONTWIDTH; break;
        case SID_ATTR_CHAR_AUTOKERN  :   nEEWhich = EE_CHAR_PAIRKERNING; break;
        case SID_ATTR_CHAR_ESCAPEMENT:   nEEWhich = EE_CHAR_ESCAPEMENT; break;
        case SID_ATTR_PARA_ADJUST_LEFT:
            aNewAttr.Put(SvxAdjustItem(SvxAdjust::Left, EE_PARA_JUST));
        break;
        case SID_ATTR_PARA_ADJUST_CENTER:
            aNewAttr.Put(SvxAdjustItem(SvxAdjust::Center, EE_PARA_JUST));
        break;
        case SID_ATTR_PARA_ADJUST_RIGHT:
            aNewAttr.Put(SvxAdjustItem(SvxAdjust::Right, EE_PARA_JUST));
        break;
        case SID_ATTR_PARA_ADJUST_BLOCK:
            aNewAttr.Put(SvxAdjustItem(SvxAdjust::Block, EE_PARA_JUST));
        break;

        case SID_ATTR_PARA_LINESPACE_10:
        {
            SvxLineSpacingItem aItem(LINE_SPACE_DEFAULT_HEIGHT, EE_PARA_SBL);
            aItem.SetPropLineSpace(100);
            aNewAttr.Put(aItem);
        }
        break;
        case SID_ATTR_PARA_LINESPACE_115:
        {
            SvxLineSpacingItem aItem(LINE_SPACE_DEFAULT_HEIGHT, EE_PARA_SBL);
            aItem.SetPropLineSpace(115);
            aNewAttr.Put(aItem);
        }
        break;
        case SID_ATTR_PARA_LINESPACE_15:
        {
            SvxLineSpacingItem aItem(LINE_SPACE_DEFAULT_HEIGHT, EE_PARA_SBL);
            aItem.SetPropLineSpace(150);
            aNewAttr.Put(aItem);
        }
        break;
        case SID_ATTR_PARA_LINESPACE_20:
        {
            SvxLineSpacingItem aItem(LINE_SPACE_DEFAULT_HEIGHT, EE_PARA_SBL);
            aItem.SetPropLineSpace(200);
            aNewAttr.Put(aItem);
        }
        break;
        case SID_SELECTALL:
        {
            Outliner& rOutliner = pOLV->GetOutliner();
            sal_Int32 nParaCount = rOutliner.GetParagraphCount();
            if (nParaCount > 0)
                pOLV->SelectRange(0, nParaCount );
            break;
        }
        case FN_FORMAT_RESET:
        {
            pPostItMgr->GetActiveSidebarWin()->ResetAttributes();
            rReq.Done();
            break;
        }
        case FN_SET_SUPER_SCRIPT:
        {
            SvxEscapementItem aItem(EE_CHAR_ESCAPEMENT);
            SvxEscapement eEsc = aEditAttr.Get(EE_CHAR_ESCAPEMENT).GetEscapement();

            if( eEsc == SvxEscapement::Superscript )
                aItem.SetEscapement( SvxEscapement::Off );
            else
                aItem.SetEscapement( SvxEscapement::Superscript );
            aNewAttr.Put( aItem );
        }
        break;
        case FN_SET_SUB_SCRIPT:
        {
            SvxEscapementItem aItem(EE_CHAR_ESCAPEMENT);
            SvxEscapement eEsc = aEditAttr.Get(EE_CHAR_ESCAPEMENT).GetEscapement();

            if( eEsc == SvxEscapement::Subscript )
                aItem.SetEscapement( SvxEscapement::Off );
            else
                aItem.SetEscapement( SvxEscapement::Subscript );
            aNewAttr.Put( aItem );
        }
        break;
        case SID_HYPERLINK_SETLINK:
        {
            const SfxPoolItem* pItem = nullptr;
            if(pNewAttrs)
                pNewAttrs->GetItemState(nSlot, false, &pItem);

            if(pItem)
            {
                const SvxHyperlinkItem& rHLinkItem = *static_cast<const SvxHyperlinkItem *>(pItem);
                SvxURLField aField(rHLinkItem.GetURL(), rHLinkItem.GetName(), SvxURLFormat::AppDefault);
                aField.SetTargetFrame(rHLinkItem.GetTargetFrame());

                const SvxFieldItem* pFieldItem = pOLV->GetFieldAtSelection();

                if (pFieldItem && dynamic_cast<const SvxURLField *>(pFieldItem->GetField()) != nullptr)
                {
                    // Select the field so that it will be deleted during insert
                    ESelection aSel = pOLV->GetSelection();
                    aSel.end.nIndex++;
                    pOLV->SetSelection(aSel);
                }
                if (pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus()!=SwPostItHelper::DELETED)
                    pOLV->InsertField(SvxFieldItem(aField, EE_FEATURE_FIELD));
            }
            break;
        }
        case FN_INSERT_SOFT_HYPHEN:
        case FN_INSERT_HARDHYPHEN:
        case FN_INSERT_HARD_SPACE:
        case FN_INSERT_NNBSP:
        case SID_INSERT_RLM :
        case SID_INSERT_LRM :
        case SID_INSERT_WJ :
        case SID_INSERT_ZWSP:
        {
            sal_Unicode cIns = 0;
            switch(rReq.GetSlot())
            {
                case FN_INSERT_SOFT_HYPHEN: cIns = CHAR_SOFTHYPHEN; break;
                case FN_INSERT_HARDHYPHEN: cIns = CHAR_HARDHYPHEN; break;
                case FN_INSERT_HARD_SPACE: cIns = CHAR_HARDBLANK; break;
                case FN_INSERT_NNBSP: cIns = CHAR_NNBSP; break;
                case SID_INSERT_RLM : cIns = CHAR_RLM ; break;
                case SID_INSERT_LRM : cIns = CHAR_LRM ; break;
                case SID_INSERT_ZWSP : cIns = CHAR_ZWSP ; break;
                case SID_INSERT_WJ: cIns = CHAR_WJ; break;
            }
            pOLV->InsertText( OUString(cIns));
            rReq.Done();
            break;
        }
        case SID_CHARMAP:
        {
            if (pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus()!=SwPostItHelper::DELETED)
                InsertSymbol(rReq);
            break;
        }
        case FN_INSERT_STRING:
        {
            const SfxPoolItem* pItem = nullptr;
            if (pNewAttrs)
                pNewAttrs->GetItemState(nSlot, false, &pItem );
            if (pItem && pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus()!=SwPostItHelper::DELETED)
                pOLV->InsertText(static_cast<const SfxStringItem *>(pItem)->GetValue());
            break;
        }
        case FN_FORMAT_FOOTNOTE_DLG:
        {
            m_rView.ExecFormatFootnote();
            break;
        }
        case FN_NUMBERING_OUTLINE_DLG:
        {
            m_rView.ExecNumberingOutline(GetPool());
            rReq.Done();
        }
        break;
        case SID_OPEN_XML_FILTERSETTINGS:
        {
            HandleOpenXmlFilterSettings(rReq);
        }
        break;
        case FN_WORDCOUNT_DIALOG:
        {
            m_rView.UpdateWordCount(this, nSlot);
            break;
        }
        case SID_CHAR_DLG_EFFECT:
        case SID_CHAR_DLG_POSITION:
        case SID_CHAR_DLG:
        {
            const SfxItemSet* pArgs = rReq.GetArgs();
            const SfxStringItem* pItem = rReq.GetArg<SfxStringItem>(FN_PARAM_1);

            if( !pArgs || pItem )
            {
                /* mod
                SwView* pView = &GetView();
                FieldUnit eMetric = ::GetDfltMetric(dynamic_cast<SwWebView*>( pView) !=  nullptr );
                SwModule::get()->PutItem(SfxUInt16Item(SID_ATTR_METRIC, eMetric));
                */
                SfxItemSet aDlgAttr(SfxItemSet::makeFixedSfxItemSet<XATTR_FILLSTYLE, XATTR_FILLCOLOR, EE_ITEMS_START, EE_ITEMS_END>(GetPool()));

                // util::Language does not exist in the EditEngine! Therefore not included in the set.

                aDlgAttr.Put( aEditAttr );
                aDlgAttr.Put( SvxKerningItem(0, RES_CHRATR_KERNING) );

                SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
                VclPtr<SfxAbstractTabDialog> pDlg(pFact->CreateSwCharDlg(m_rView.GetFrameWeld(), m_rView, aDlgAttr, SwCharDlgMode::Ann));
                if (nSlot == SID_CHAR_DLG_EFFECT)
                {
                    pDlg->SetCurPageId(u"fonteffects"_ustr);
                }
                if (nSlot == SID_CHAR_DLG_POSITION)
                {
                    pDlg->SetCurPageId(u"position"_ustr);
                }
                else if (pItem)
                {
                    pDlg->SetCurPageId(pItem->GetValue());
                }

                auto xRequest = std::make_shared<SfxRequest>(rReq);
                rReq.Ignore(); // the 'old' request is not relevant any more
                pDlg->StartExecuteAsync(
                    [this, pDlg, xRequest=std::move(xRequest), nEEWhich, aNewAttr2=aNewAttr, pOLV] (sal_Int32 nResult) mutable ->void
                    {
                        if (nResult == RET_OK)
                        {
                            xRequest->Done( *( pDlg->GetOutputItemSet() ) );
                            aNewAttr2.Put(*pDlg->GetOutputItemSet());
                            ExecPost(*xRequest, nEEWhich, aNewAttr2, pOLV);
                        }
                        pDlg->disposeOnce();
                    }
                );
                return;
            }
            else
                aNewAttr.Put(*pArgs);
            break;
        }
        case SID_PARA_DLG:
        {
            const SfxItemSet* pArgs = rReq.GetArgs();

            if (!pArgs)
            {
                /* mod todo ???
                SwView* pView = &GetView();
                FieldUnit eMetric = ::GetDfltMetric(dynamic_cast<SwWebView*>( pView) !=  nullptr );
                SwModule::get()->PutItem(SfxUInt16Item(SID_ATTR_METRIC, eMetric));
                */
                SfxItemSet aDlgAttr(SfxItemSet::makeFixedSfxItemSet<
                    EE_ITEMS_START, EE_ITEMS_END,
                    SID_ATTR_PARA_HYPHENZONE, SID_ATTR_PARA_WIDOWS>(GetPool()));

                aDlgAttr.Put(aEditAttr);

                aDlgAttr.Put( SvxHyphenZoneItem( false, RES_PARATR_HYPHENZONE) );
                aDlgAttr.Put( SvxFormatBreakItem( SvxBreak::NONE, RES_BREAK ) );
                aDlgAttr.Put( SvxFormatSplitItem( true, RES_PARATR_SPLIT ) );
                aDlgAttr.Put( SvxWidowsItem( 0, RES_PARATR_WIDOWS ) );
                aDlgAttr.Put( SvxOrphansItem( 0, RES_PARATR_ORPHANS ) );

                SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
                ScopedVclPtr<SfxAbstractTabDialog> pDlg(pFact->CreateSwParaDlg(m_rView.GetFrameWeld(), m_rView, aDlgAttr, true));
                sal_uInt16 nRet = pDlg->Execute();
                if(RET_OK == nRet)
                {
                    rReq.Done( *( pDlg->GetOutputItemSet() ) );
                    aNewAttr.Put(*pDlg->GetOutputItemSet());
                }
                if(RET_OK != nRet)
                    return;
            }
            else
                aNewAttr.Put(*pArgs);
            break;
        }

        case SID_AUTOSPELL_CHECK:
        {
            m_rView.ExecuteSlot(rReq);
            break;
        }
        case SID_ATTR_PARA_LEFT_TO_RIGHT:
        case SID_ATTR_PARA_RIGHT_TO_LEFT:
        {
            bool bLeftToRight = nSlot == SID_ATTR_PARA_LEFT_TO_RIGHT;

            const SfxPoolItem* pPoolItem;
            if( pNewAttrs && SfxItemState::SET == pNewAttrs->GetItemState( nSlot, true, &pPoolItem ) )
            {
                if( !static_cast<const SfxBoolItem*>(pPoolItem)->GetValue() )
                    bLeftToRight = !bLeftToRight;
            }
            SfxItemSet aAttr(SfxItemSet::makeFixedSfxItemSet<
                EE_PARA_WRITINGDIR, EE_PARA_WRITINGDIR,
                EE_PARA_JUST, EE_PARA_JUST>(*aNewAttr.GetPool()));

            SvxAdjust nAdjust = SvxAdjust::Left;
            if( const SvxAdjustItem* pAdjustItem = aEditAttr.GetItemIfSet(EE_PARA_JUST ) )
                nAdjust = pAdjustItem->GetAdjust();

            if( bLeftToRight )
            {
                aAttr.Put( SvxFrameDirectionItem( SvxFrameDirection::Horizontal_LR_TB, EE_PARA_WRITINGDIR ) );
                if( nAdjust == SvxAdjust::Right )
                    aAttr.Put( SvxAdjustItem( SvxAdjust::Left, EE_PARA_JUST ) );
            }
            else
            {
                aAttr.Put( SvxFrameDirectionItem( SvxFrameDirection::Horizontal_RL_TB, EE_PARA_WRITINGDIR ) );
                if( nAdjust == SvxAdjust::Left )
                    aAttr.Put( SvxAdjustItem( SvxAdjust::Right, EE_PARA_JUST ) );
            }
            pOLV->SetAttribs(aAttr);
            break;
        }
    }

    ExecPost(rReq, nEEWhich, aNewAttr, pOLV );
}

void SwAnnotationShell::ExecPost( const SfxRequest& rReq, sal_uInt16 nEEWhich, SfxItemSet& rNewAttr, OutlinerView* pOLV )
{
    const SfxItemSet *pNewAttrs = rReq.GetArgs();
    const sal_uInt16 nSlot = rReq.GetSlot();
    const sal_uInt16 nWhich = GetPool().GetWhichIDFromSlotID(nSlot);

    if(nEEWhich && pNewAttrs)
    {
        rNewAttr.Put(pNewAttrs->Get(nWhich).CloneSetWhich(nEEWhich));
    }
    else if (nEEWhich == EE_CHAR_COLOR)
    {
        m_rView.GetViewFrame().GetDispatcher()->Execute(SID_CHAR_DLG_EFFECT);
    }
    else if (nEEWhich == EE_CHAR_KERNING)
    {
        m_rView.GetViewFrame().GetDispatcher()->Execute(SID_CHAR_DLG_POSITION);
    }


    tools::Rectangle aOutRect = pOLV->GetOutputArea();
    if (tools::Rectangle() != aOutRect && rNewAttr.Count())
        pOLV->SetAttribs(rNewAttr);

    m_rView.GetViewFrame().GetBindings().InvalidateAll(false);
    if ( pOLV->GetOutliner().IsModified() )
        m_rView.GetWrtShell().SetModified();
}

void SwAnnotationShell::GetState(SfxItemSet& rSet)
{
    //TODO: clean this up!!!
    // FN_SET_SUPER_SCRIPT
    //SID_ATTR_PARA_ADJUST
    //SID_ATTR_PARA_ADJUST_BLOCK

    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if ( !pPostItMgr || !pPostItMgr->HasActiveSidebarWin() )
        return;

    OutlinerView* pOLV = pPostItMgr->GetActiveSidebarWin()->GetOutlinerView();
    SfxItemSet aEditAttr(pOLV->GetAttribs());

    SfxWhichIter aIter(rSet);
    sal_uInt16 nWhich = aIter.FirstWhich();
    while(nWhich)
    {
        sal_uInt16 nEEWhich = 0;
        sal_uInt16 nSlotId = GetPool().GetSlotId( nWhich );
        switch( nSlotId )
        {
            case SID_ATTR_PARA_LRSPACE:
            case SID_ATTR_PARA_LEFTSPACE:
            case SID_ATTR_PARA_RIGHTSPACE:
            case SID_ATTR_PARA_FIRSTLINESPACE:
            {
                SfxItemState eState = aEditAttr.GetItemState( EE_PARA_LRSPACE );
                if( eState >= SfxItemState::DEFAULT )
                {
                    SvxLRSpaceItem aLR = aEditAttr.Get( EE_PARA_LRSPACE );
                    aLR.SetWhich(nSlotId);
                    rSet.Put(aLR);
                }
                else
                    rSet.InvalidateItem(nSlotId);
            }
            break;
            case SID_ATTR_PARA_LINESPACE:
            {
                SfxItemState eState = aEditAttr.GetItemState( EE_PARA_SBL );
                if( eState >= SfxItemState::DEFAULT )
                {
                    const SvxLineSpacingItem& aLR = aEditAttr.Get( EE_PARA_SBL );
                    rSet.Put(aLR);
                }
                else
                    rSet.InvalidateItem(nSlotId);
            }
            break;
            case SID_ATTR_PARA_ULSPACE:
            case SID_ATTR_PARA_ABOVESPACE:
            case SID_ATTR_PARA_BELOWSPACE:
            case SID_PARASPACE_INCREASE:
            case SID_PARASPACE_DECREASE:
                {
                    SfxItemState eState = aEditAttr.GetItemState( EE_PARA_ULSPACE );
                    if( eState >= SfxItemState::DEFAULT )
                    {
                        SvxULSpaceItem aULSpace = aEditAttr.Get( EE_PARA_ULSPACE );
                        if ( !aULSpace.GetUpper() && !aULSpace.GetLower() )
                            rSet.DisableItem( SID_PARASPACE_DECREASE );
                        else if ( aULSpace.GetUpper() >= 5670 && aULSpace.GetLower() >= 5670 )
                            rSet.DisableItem( SID_PARASPACE_INCREASE );
                        if ( nSlotId == SID_ATTR_PARA_ULSPACE
                            || nSlotId == SID_ATTR_PARA_BELOWSPACE
                            || nSlotId == SID_ATTR_PARA_ABOVESPACE
                        )
                        {
                            aULSpace.SetWhich(nSlotId);
                            rSet.Put(aULSpace);
                        }
                    }
                    else
                    {
                        rSet.DisableItem( SID_PARASPACE_INCREASE );
                        rSet.DisableItem( SID_PARASPACE_DECREASE );
                        rSet.InvalidateItem( SID_ATTR_PARA_ULSPACE );
                        rSet.InvalidateItem( SID_ATTR_PARA_ABOVESPACE );
                        rSet.InvalidateItem( SID_ATTR_PARA_BELOWSPACE );
                    }
                }
                break;
            case SID_ATTR_CHAR_FONT:
            case SID_ATTR_CHAR_FONTHEIGHT:
            case SID_ATTR_CHAR_WEIGHT:
            case SID_ATTR_CHAR_POSTURE:
                {
                    SvtScriptType nScriptType = pOLV->GetSelectedScriptType();
                    SfxItemPool* pSecondPool = aEditAttr.GetPool()->GetSecondaryPool();
                    if( !pSecondPool )
                        pSecondPool = aEditAttr.GetPool();
                    SvxScriptSetItem aSetItem( nSlotId, *pSecondPool );
                    aSetItem.GetItemSet().Put( aEditAttr, false );
                    const SfxPoolItem* pI = aSetItem.GetItemOfScript( nScriptType );
                    if( pI )
                    {
                        rSet.Put(pI->CloneSetWhich(nWhich));
                    }
                    else
                        rSet.InvalidateItem( nWhich );
                }
                break;
            case SID_ATTR_CHAR_COLOR2:
            case SID_ATTR_CHAR_COLOR: nEEWhich = EE_CHAR_COLOR; break;
            case SID_ATTR_CHAR_BACK_COLOR: nEEWhich = EE_CHAR_BKGCOLOR; break;
            case SID_ATTR_CHAR_UNDERLINE: nEEWhich = EE_CHAR_UNDERLINE;break;
            case SID_ATTR_CHAR_OVERLINE: nEEWhich = EE_CHAR_OVERLINE;break;
            case SID_ATTR_CHAR_CONTOUR: nEEWhich = EE_CHAR_OUTLINE; break;
            case SID_ATTR_CHAR_SHADOWED:  nEEWhich = EE_CHAR_SHADOW;break;
            case SID_ATTR_CHAR_STRIKEOUT: nEEWhich = EE_CHAR_STRIKEOUT;break;
            case SID_ATTR_CHAR_LANGUAGE    : nEEWhich = EE_CHAR_LANGUAGE;break;
            case SID_ATTR_CHAR_ESCAPEMENT: nEEWhich = EE_CHAR_ESCAPEMENT;break;
            case SID_ATTR_CHAR_KERNING:  nEEWhich = EE_CHAR_KERNING;break;
            case FN_SET_SUPER_SCRIPT:
            case FN_SET_SUB_SCRIPT:
            {
                SvxEscapement nEsc;
                if (nWhich==FN_SET_SUPER_SCRIPT)
                    nEsc = SvxEscapement::Superscript;
                else
                    nEsc = SvxEscapement::Subscript;

                const SfxPoolItem *pEscItem = &aEditAttr.Get( EE_CHAR_ESCAPEMENT );
                if( nEsc == static_cast<const SvxEscapementItem*>(pEscItem)->GetEscapement() )
                    rSet.Put( SfxBoolItem( nWhich, true ));
                else
                    rSet.InvalidateItem( nWhich );
                break;
            }
            case SID_ATTR_PARA_ADJUST_LEFT:
            case SID_ATTR_PARA_ADJUST_RIGHT:
            case SID_ATTR_PARA_ADJUST_CENTER:
            case SID_ATTR_PARA_ADJUST_BLOCK:
                {
                    SvxAdjust eAdjust = SvxAdjust::Left;
                    if (nWhich==SID_ATTR_PARA_ADJUST_LEFT)
                        eAdjust = SvxAdjust::Left;
                    else if (nWhich==SID_ATTR_PARA_ADJUST_RIGHT)
                        eAdjust = SvxAdjust::Right;
                    else if (nWhich==SID_ATTR_PARA_ADJUST_CENTER)
                        eAdjust = SvxAdjust::Center;
                    else if (nWhich==SID_ATTR_PARA_ADJUST_BLOCK)
                        eAdjust = SvxAdjust::Block;

                    const SvxAdjustItem *pAdjust = aEditAttr.GetItemIfSet( EE_PARA_JUST, false );

                    if( !pAdjust || IsInvalidItem( pAdjust ))
                    {
                        rSet.InvalidateItem( nSlotId );
                    }
                    else
                    {
                        if ( eAdjust == pAdjust->GetAdjust())
                            rSet.Put( SfxBoolItem( nWhich, true ));
                        else
                            rSet.InvalidateItem( nWhich );
                    }
                    break;
                }
            case SID_ATTR_PARA_LINESPACE_10:
            case SID_ATTR_PARA_LINESPACE_115:
            case SID_ATTR_PARA_LINESPACE_15:
            case SID_ATTR_PARA_LINESPACE_20:
                {
                    int nLSpace = 0;
                    if (nWhich==SID_ATTR_PARA_LINESPACE_10)
                        nLSpace = 100;
                    else if (nWhich==SID_ATTR_PARA_LINESPACE_115)
                        nLSpace = 115;
                    else if (nWhich==SID_ATTR_PARA_LINESPACE_15)
                        nLSpace = 150;
                    else if (nWhich==SID_ATTR_PARA_LINESPACE_20)
                        nLSpace = 200;

                    const SvxLineSpacingItem *pLSpace = aEditAttr.GetItemIfSet( EE_PARA_SBL, false );

                    if( !pLSpace || IsInvalidItem( pLSpace ))
                    {
                        rSet.InvalidateItem( nSlotId );
                    }
                    else
                    {
                        if( nLSpace == pLSpace->GetPropLineSpace() )
                            rSet.Put( SfxBoolItem( nWhich, true ));
                        else
                        {
                            // tdf#114631 - disable non selected line spacing
                            rSet.Put(SfxBoolItem(nWhich, false));
                        }
                    }
                    break;
                }
            case SID_AUTOSPELL_CHECK:
            {
                const SfxPoolItemHolder aResult(m_rView.GetSlotState(nWhich));
                if (aResult)
                    rSet.Put(SfxBoolItem(nWhich, static_cast<const SfxBoolItem*>(aResult.getItem())->GetValue()));
                else
                    rSet.DisableItem( nWhich );
                break;
            }
            case SID_ATTR_PARA_LEFT_TO_RIGHT:
            case SID_ATTR_PARA_RIGHT_TO_LEFT:
            {
                if ( !SvtCTLOptions::IsCTLFontEnabled() )
                    rSet.DisableItem( nWhich );
                else
                {
                    if (pOLV->GetOutliner().IsVertical())
                        rSet.DisableItem( nWhich );
                    else
                    {
                        bool bFlag = false;
                        switch( aEditAttr.Get( EE_PARA_WRITINGDIR ).GetValue() )
                        {
                            case SvxFrameDirection::Horizontal_LR_TB:
                            {
                                bFlag = nWhich == SID_ATTR_PARA_LEFT_TO_RIGHT;
                                rSet.Put( SfxBoolItem( nWhich, bFlag ));
                                break;
                            }
                            case SvxFrameDirection::Horizontal_RL_TB:
                            {
                                bFlag = nWhich != SID_ATTR_PARA_LEFT_TO_RIGHT;
                                rSet.Put( SfxBoolItem( nWhich, bFlag ));
                                break;
                            }
                            default:
                                break;
                        }
                    }
                }
            }
            break;
            case SID_INSERT_RLM :
            case SID_INSERT_LRM :
            {
                bool bEnabled = SvtCTLOptions::IsCTLFontEnabled();
                m_rView.GetViewFrame().GetBindings().SetVisibleState( nWhich, bEnabled );
                if(!bEnabled)
                    rSet.DisableItem(nWhich);
            }
            break;
            default:
                rSet.InvalidateItem( nWhich );
                break;
        }

        if(nEEWhich)
        {
            rSet.Put(aEditAttr.Get(nEEWhich).CloneSetWhich(nWhich));
            if(nEEWhich == EE_CHAR_KERNING)
            {
                SfxItemState eState = aEditAttr.GetItemState( EE_CHAR_KERNING );
                if ( eState == SfxItemState::INVALID )
                {
                    rSet.InvalidateItem(EE_CHAR_KERNING);
                }
            }
        }

        if (pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus()==SwPostItHelper::DELETED)
            rSet.DisableItem( nWhich );

        nWhich = aIter.NextWhich();
    }
}

void SwAnnotationShell::ExecSearch(SfxRequest& rReq)
{
    m_rView.ExecSearch(rReq);
}

void SwAnnotationShell::StateSearch(SfxItemSet &rSet)
{
    m_rView.StateSearch(rSet);
}

void SwAnnotationShell::ExecClpbrd(SfxRequest const &rReq)
{
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if ( !pPostItMgr || !pPostItMgr->HasActiveSidebarWin() )
        return;

    OutlinerView* pOLV = pPostItMgr->GetActiveSidebarWin()->GetOutlinerView();

    tools::Long aOldHeight = pPostItMgr->GetActiveSidebarWin()->GetPostItTextHeight();
    sal_uInt16 nSlot = rReq.GetSlot();
    switch (nSlot)
    {
        case SID_CUT:
            if ( (pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus()!=SwPostItHelper::DELETED) && pOLV->HasSelection() )
                pOLV->Cut();
            break;
        case SID_COPY:
            if( pOLV->HasSelection() )
                pOLV->Copy();
            break;
        case SID_PASTE:
            if (pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus()!=SwPostItHelper::DELETED)
                pOLV->PasteSpecial();
            break;
        case SID_PASTE_UNFORMATTED:
            if (pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus()!=SwPostItHelper::DELETED)
                pOLV->Paste();
            break;
        case SID_PASTE_SPECIAL:
        {
            if (pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus()!=SwPostItHelper::DELETED)
            {
                SvxAbstractDialogFactory* pFact = SvxAbstractDialogFactory::Create();
                ScopedVclPtr<SfxAbstractPasteDialog> pDlg(pFact->CreatePasteDialog(m_rView.GetEditWin().GetFrameWeld()));

                pDlg->Insert( SotClipboardFormatId::STRING, OUString() );
                pDlg->Insert( SotClipboardFormatId::RTF,    OUString() );
                pDlg->Insert( SotClipboardFormatId::RICHTEXT,    OUString() );

                TransferableDataHelper aDataHelper( TransferableDataHelper::CreateFromSystemClipboard( &m_rView.GetEditWin() ) );

                SotClipboardFormatId nFormat = pDlg->GetFormat( aDataHelper.GetTransferable() );

                if (nFormat != SotClipboardFormatId::NONE)
                {
                    if (nFormat == SotClipboardFormatId::STRING)
                        pOLV->Paste();
                    else
                        pOLV->PasteSpecial();
                }
            }
            break;
        }
        case SID_CLIPBOARD_FORMAT_ITEMS:
        {
            SotClipboardFormatId nFormat = SotClipboardFormatId::NONE;
            const SfxPoolItem* pItem;
            if (rReq.GetArgs() && rReq.GetArgs()->GetItemState(nSlot, true, &pItem) == SfxItemState::SET)
            {
                if (const SfxUInt32Item* pUInt32Item = dynamic_cast<const SfxUInt32Item *>(pItem))
                    nFormat = static_cast<SotClipboardFormatId>(pUInt32Item->GetValue());
            }

            if ( nFormat != SotClipboardFormatId::NONE )
            {
                if (SotClipboardFormatId::STRING == nFormat)
                    pOLV->Paste();
                else
                    pOLV->PasteSpecial();
            }
            break;
        }
    }
    pPostItMgr->GetActiveSidebarWin()->ResizeIfNecessary(aOldHeight,pPostItMgr->GetActiveSidebarWin()->GetPostItTextHeight());
}

void SwAnnotationShell::StateClpbrd(SfxItemSet &rSet)
{
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if ( !pPostItMgr || !pPostItMgr->HasActiveSidebarWin() )
        return;
    OutlinerView* pOLV = pPostItMgr->GetActiveSidebarWin()->GetOutlinerView();

    TransferableDataHelper aDataHelper( TransferableDataHelper::CreateFromSystemClipboard( &m_rView.GetEditWin() ) );
    bool bPastePossible = ( aDataHelper.HasFormat( SotClipboardFormatId::STRING ) || aDataHelper.HasFormat( SotClipboardFormatId::RTF )
        || aDataHelper.HasFormat( SotClipboardFormatId::RICHTEXT ));
    bPastePossible = bPastePossible &&  (pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus()!=SwPostItHelper::DELETED);

    SfxWhichIter aIter(rSet);
    sal_uInt16 nWhich = aIter.FirstWhich();

    while(nWhich)
    {
        switch(nWhich)
        {
            case SID_CUT:
            {
                if (pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus() == SwPostItHelper::DELETED)
                    rSet.DisableItem( nWhich );
                [[fallthrough]];
            }
            case SID_COPY:
            {
                SfxObjectShell* pObjectShell = GetObjectShell();
                if (!pOLV->HasSelection() || (pObjectShell && pObjectShell->isContentExtractionLocked()) )
                    rSet.DisableItem( nWhich );
                break;
            }
            case SID_PASTE:
            case SID_PASTE_UNFORMATTED:
            case SID_PASTE_SPECIAL:
                {
                    if( !bPastePossible )
                        rSet.DisableItem( nWhich );
                    break;
                }
            case SID_CLIPBOARD_FORMAT_ITEMS:
                {
                    if ( bPastePossible )
                    {
                        SvxClipboardFormatItem aFormats( SID_CLIPBOARD_FORMAT_ITEMS );
                        if ( aDataHelper.HasFormat( SotClipboardFormatId::RTF ) )
                            aFormats.AddClipbrdFormat( SotClipboardFormatId::RTF );
                        if ( aDataHelper.HasFormat( SotClipboardFormatId::RICHTEXT ) )
                            aFormats.AddClipbrdFormat( SotClipboardFormatId::RICHTEXT );
                        aFormats.AddClipbrdFormat( SotClipboardFormatId::STRING );
                        rSet.Put( aFormats );
                    }
                    else
                        rSet.DisableItem( nWhich );
                    break;
                }
        }
        nWhich = aIter.NextWhich();
    }
}

void SwAnnotationShell::StateStatusLine(SfxItemSet &rSet)
{
    SfxWhichIter aIter( rSet );
    sal_uInt16 nWhich = aIter.FirstWhich();

    while( nWhich )
    {
        switch( nWhich )
        {
            case FN_STAT_SELMODE:
            {
                rSet.Put(SfxUInt16Item(FN_STAT_SELMODE, 0));
                rSet.DisableItem( nWhich );
                break;
            }
            case FN_STAT_TEMPLATE:
            {
                rSet.DisableItem( nWhich );
                break;
            }
        }
        nWhich = aIter.NextWhich();
    }
}

void SwAnnotationShell::StateInsert(SfxItemSet &rSet)
{
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if ( !pPostItMgr || !pPostItMgr->HasActiveSidebarWin() )
        return;

    OutlinerView* pOLV = pPostItMgr->GetActiveSidebarWin()->GetOutlinerView();
    SfxWhichIter aIter(rSet);
    sal_uInt16 nWhich = aIter.FirstWhich();

    while(nWhich)
    {
        switch(nWhich)
        {
            case SID_HYPERLINK_GETLINK:
                {
                    SvxHyperlinkItem aHLinkItem;
                    aHLinkItem.SetInsertMode(HLINK_FIELD);

                    const SvxFieldItem* pFieldItem = pOLV->GetFieldAtSelection();

                    if (pFieldItem)
                    {
                        if (const SvxURLField* pURLField = dynamic_cast<const SvxURLField *>(pFieldItem->GetField()))
                        {
                            aHLinkItem.SetName(pURLField->GetRepresentation());
                            aHLinkItem.SetURL(pURLField->GetURL());
                            aHLinkItem.SetTargetFrame(pURLField->GetTargetFrame());
                        }
                    }
                    else
                    {
                        OUString sSel(pOLV->GetSelected());
                        sSel = sSel.copy(0, std::min<sal_Int32>(255, sSel.getLength()));
                        aHLinkItem.SetName(comphelper::string::stripEnd(sSel, ' '));
                    }

                    sal_uInt16 nHtmlMode = ::GetHtmlMode(m_rView.GetDocShell());
                    aHLinkItem.SetInsertMode(static_cast<SvxLinkInsertMode>(aHLinkItem.GetInsertMode() |
                        ((nHtmlMode & HTMLMODE_ON) != 0 ? HLINK_HTMLMODE : 0)));

                    rSet.Put(aHLinkItem);
                }
                break;
        }

        if (pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus()==SwPostItHelper::DELETED)
            rSet.DisableItem( nWhich );

        nWhich = aIter.NextWhich();
    }
}

void SwAnnotationShell::NoteExec(SfxRequest const &rReq)
{
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if ( !pPostItMgr )
        return;

    sal_uInt16 nSlot = rReq.GetSlot();
    switch (nSlot)
    {
        case FN_REPLY:
        case FN_POSTIT:
        case FN_DELETE_COMMENT:
        case FN_DELETE_COMMENT_THREAD:
        case FN_RESOLVE_NOTE:
        case FN_RESOLVE_NOTE_THREAD:
        case FN_PROMOTE_COMMENT:
            if ( pPostItMgr->HasActiveSidebarWin() )
                pPostItMgr->GetActiveSidebarWin()->ExecuteCommand(nSlot);
            break;
        case FN_DELETE_ALL_NOTES:
            pPostItMgr->Delete();
            break;
        case FN_FORMAT_ALL_NOTES:
            pPostItMgr->ExecuteFormatAllDialog(m_rView);
            break;
        case FN_DELETE_NOTE_AUTHOR:
        {
            const SfxStringItem* pItem = rReq.GetArg<SfxStringItem>(nSlot);
            if ( pItem )
                pPostItMgr->Delete( pItem->GetValue() );
            else if ( pPostItMgr->HasActiveSidebarWin() )
                pPostItMgr->Delete( pPostItMgr->GetActiveSidebarWin()->GetAuthor() );
            break;
        }
        case FN_HIDE_NOTE:
            break;
        case FN_HIDE_ALL_NOTES:
            pPostItMgr->Hide();
            break;
        case FN_HIDE_NOTE_AUTHOR:
        {
            const SfxStringItem* pItem = rReq.GetArg<SfxStringItem>(nSlot);
            if ( pItem )
                pPostItMgr->Hide( pItem->GetValue() );
            else if ( pPostItMgr->HasActiveSidebarWin() )
                pPostItMgr->Hide( pPostItMgr->GetActiveSidebarWin()->GetAuthor() );
        }
    }
}

void SwAnnotationShell::GetNoteState(SfxItemSet &rSet)
{
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    SfxWhichIter aIter(rSet);
    sal_uInt16 nWhich = aIter.FirstWhich();
    while(nWhich)
    {
        sal_uInt16 nSlotId = GetPool().GetSlotId( nWhich );
        switch( nSlotId )
        {
        case FN_POSTIT:
        case FN_DELETE_ALL_NOTES:
        case FN_FORMAT_ALL_NOTES:
        case FN_HIDE_NOTE:
        case FN_HIDE_ALL_NOTES:
        case FN_DELETE_COMMENT:
        case FN_DELETE_COMMENT_THREAD:
            {
                if( !pPostItMgr
                    || !pPostItMgr->HasActiveAnnotationWin() )
                {
                    rSet.DisableItem(nWhich);
                }
                break;
            }
        case FN_RESOLVE_NOTE:
            {
                if( !pPostItMgr
                    || !pPostItMgr->HasActiveAnnotationWin() )
                {
                    rSet.DisableItem(nWhich);
                }
                else
                {
                    SfxBoolItem aBool(nWhich, pPostItMgr->GetActiveSidebarWin()->IsResolved());
                    rSet.Put( aBool );
                }
                break;
            }
        case FN_RESOLVE_NOTE_THREAD:
            {
                if( !pPostItMgr
                    || !pPostItMgr->HasActiveAnnotationWin() )
                {
                    rSet.DisableItem(nWhich);
                }
                else
                {
                    SfxBoolItem aBool(nWhich, pPostItMgr->GetActiveSidebarWin()->IsThreadResolved());
                    rSet.Put( aBool );
                }
                break;
            }
        case FN_DELETE_NOTE_AUTHOR:
        case FN_HIDE_NOTE_AUTHOR:
        {
            if( !pPostItMgr
                || !pPostItMgr->HasActiveAnnotationWin() )
            {
                rSet.DisableItem(nWhich);
            }
            else
            {
                OUString aText( nSlotId == FN_DELETE_NOTE_AUTHOR ?
                                SwResId( STR_DELETE_NOTE_AUTHOR ) : SwResId( STR_HIDE_NOTE_AUTHOR ) );
                SwRewriter aRewriter;
                aRewriter.AddRule( UndoArg1, pPostItMgr->GetActiveSidebarWin()->GetAuthor() );
                aText = aRewriter.Apply( aText );
                SfxStringItem aItem( nSlotId, aText );
                rSet.Put( aItem );
            }
            break;
        }
        case FN_REPLY:
            {
                if ( !pPostItMgr ||
                     !pPostItMgr->HasActiveAnnotationWin() )
                {
                    rSet.DisableItem(nWhich);
                }
                else
                {
                    SvtUserOptions aUserOpt;
                    OUString sAuthor;
                    if( (sAuthor = aUserOpt.GetFullName()).isEmpty() &&
                        (sAuthor = aUserOpt.GetID()).isEmpty() )
                        sAuthor = SwResId( STR_REDLINE_UNKNOWN_AUTHOR );
                    if (sAuthor == pPostItMgr->GetActiveSidebarWin()->GetAuthor())
                        rSet.DisableItem(nWhich);
                }
                break;
            }
        case FN_PROMOTE_COMMENT:
            {
                if (!pPostItMgr || !pPostItMgr->HasActiveAnnotationWin()
                    || pPostItMgr->GetActiveSidebarWin()->IsRootNote())
                {
                    rSet.DisableItem(nWhich);
                }
                break;
            }
            default:
                rSet.InvalidateItem( nWhich );
                break;
        }

        if (pPostItMgr && pPostItMgr->HasActiveSidebarWin())
        {
            if ( (pPostItMgr->GetActiveSidebarWin()->IsReadOnlyOrProtected()) &&
                    ( (nSlotId==FN_DELETE_COMMENT) || (nSlotId==FN_REPLY) ) )
                rSet.DisableItem( nWhich );
        }
        nWhich = aIter.NextWhich();
    }
}

void SwAnnotationShell::ExecLingu(SfxRequest &rReq)
{
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if ( !pPostItMgr || !pPostItMgr->HasActiveSidebarWin() )
        return;

    OutlinerView* pOLV = pPostItMgr->GetActiveSidebarWin()->GetOutlinerView();
    sal_uInt16 nSlot = rReq.GetSlot();
    SwWrtShell &rSh = m_rView.GetWrtShell();
    bool bRestoreSelection = false;
    ESelection aOldSelection;

    switch (nSlot)
    {
        case SID_LANGUAGE_STATUS:
        {
            aOldSelection = pOLV->GetSelection();
            if (!pOLV->GetEditView().HasSelection())
            {
                pOLV->GetEditView().SelectCurrentWord();
            }

            bRestoreSelection = SwLangHelper::SetLanguageStatus(pOLV,rReq,m_rView,rSh);
            break;
        }
        case SID_THES:
        {
            OUString aReplaceText;
            const SfxStringItem* pItem2 = rReq.GetArg(FN_PARAM_THES_WORD_REPLACE);
            if (pItem2)
                aReplaceText = pItem2->GetValue();
            if (!aReplaceText.isEmpty())
                ReplaceTextWithSynonym( pOLV->GetEditView(), aReplaceText );
            break;
        }
        case SID_THESAURUS:
        {
            pOLV->StartThesaurus(rReq.GetFrameWeld());
            break;
        }
        case SID_HANGUL_HANJA_CONVERSION:
            pOLV->StartTextConversion(rReq.GetFrameWeld(), LANGUAGE_KOREAN, LANGUAGE_KOREAN, nullptr,
                    i18n::TextConversionOption::CHARACTER_BY_CHARACTER, true, false);
            break;

        case SID_CHINESE_CONVERSION:
        {
            //open ChineseTranslationDialog
            rtl::Reference< textconversiondlgs::ChineseTranslation_UnoDialog > xDialog(new textconversiondlgs::ChineseTranslation_UnoDialog({}));

            //execute dialog
            sal_Int16 nDialogRet = xDialog->execute();
            if( RET_OK == nDialogRet )
            {
                //get some parameters from the dialog
                bool bToSimplified = xDialog->getIsDirectionToSimplified();
                bool bCommonTerms = xDialog->getIsTranslateCommonTerms();

                //execute translation
                LanguageType nSourceLang = bToSimplified ? LANGUAGE_CHINESE_TRADITIONAL : LANGUAGE_CHINESE_SIMPLIFIED;
                LanguageType nTargetLang = bToSimplified ? LANGUAGE_CHINESE_SIMPLIFIED : LANGUAGE_CHINESE_TRADITIONAL;
                sal_Int32 nOptions       = !bCommonTerms ? i18n::TextConversionOption::CHARACTER_BY_CHARACTER : 0;

                vcl::Font aTargetFont = OutputDevice::GetDefaultFont( DefaultFontType::CJK_TEXT,
                            nTargetLang, GetDefaultFontFlags::OnlyOne );

                pOLV->StartTextConversion(rReq.GetFrameWeld(), nSourceLang, nTargetLang, &aTargetFont, nOptions, false, false);
            }
        }
        break;
    }

    if (bRestoreSelection)
    {
        // restore selection
        pOLV->GetEditView().SetSelection( aOldSelection );
    }
}

void SwAnnotationShell::GetLinguState(SfxItemSet &rSet)
{
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if ( !pPostItMgr || !pPostItMgr->HasActiveSidebarWin() )
        return;

    OutlinerView* pOLV = pPostItMgr->GetActiveSidebarWin()->GetOutlinerView();

    SfxWhichIter aIter(rSet);
    sal_uInt16 nWhich = aIter.FirstWhich();
    while(nWhich)
    {
        switch (nWhich)
        {
            case SID_LANGUAGE_STATUS:
            {
                SwLangHelper::GetLanguageStatus(pOLV,rSet);
                break;
            }

            case SID_THES:
            {
                OUString        aStatusVal;
                LanguageType    nLang = LANGUAGE_NONE;
                bool bIsLookUpWord = GetStatusValueForThesaurusFromContext( aStatusVal, nLang, pOLV->GetEditView() );
                rSet.Put( SfxStringItem( SID_THES, aStatusVal ) );

                // disable "Thesaurus" context menu entry if there is nothing to look up
                uno::Reference< linguistic2::XThesaurus >  xThes( ::GetThesaurus() );
                if (!bIsLookUpWord ||
                    !xThes.is() || nLang == LANGUAGE_NONE || !xThes->hasLocale( LanguageTag::convertToLocale( nLang ) ))
                    rSet.DisableItem( SID_THES );
                break;
            }

            // disable "Thesaurus" if the language is not supported
            case SID_THESAURUS:
            {
                TypedWhichId<SvxLanguageItem> nLangWhich = GetWhichOfScript( RES_CHRATR_LANGUAGE,
                            SvtLanguageOptions::GetI18NScriptTypeOfLanguage( GetAppLanguage() ) );
                const SvxLanguageItem &rItem = m_rView.GetWrtShell().GetDoc()->GetDefault(nLangWhich);
                LanguageType nLang = rItem.GetLanguage();
                uno::Reference< linguistic2::XThesaurus >  xThes( ::GetThesaurus() );
                if (!xThes.is() || nLang == LANGUAGE_NONE ||
                    !xThes->hasLocale( LanguageTag::convertToLocale( nLang ) ))
                    rSet.DisableItem( SID_THESAURUS );
            }
            break;
            case SID_HANGUL_HANJA_CONVERSION:
            case SID_CHINESE_CONVERSION:
            {
                if (!SvtCJKOptions::IsAnyEnabled())
                {
                    m_rView.GetViewFrame().GetBindings().SetVisibleState( nWhich, false );
                    rSet.DisableItem(nWhich);
                }
                else
                    m_rView.GetViewFrame().GetBindings().SetVisibleState( nWhich, true );
            }
            break;
        }

        if (pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus()==SwPostItHelper::DELETED)
            rSet.DisableItem( nWhich );

        nWhich = aIter.NextWhich();
    }
}

void SwAnnotationShell::ExecTransliteration(SfxRequest const &rReq)
{
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if (!pPostItMgr || !pPostItMgr->HasActiveSidebarWin())
        return;

    OutlinerView* pOLV = pPostItMgr->GetActiveSidebarWin()->GetOutlinerView();

    if (!pOLV)
        return;

    TransliterationFlags nMode = TransliterationFlags::NONE;

    switch( rReq.GetSlot() )
    {
        case SID_TRANSLITERATE_SENTENCE_CASE:
            nMode = TransliterationFlags::SENTENCE_CASE;
            break;
        case SID_TRANSLITERATE_TITLE_CASE:
            nMode = TransliterationFlags::TITLE_CASE;
            break;
        case SID_TRANSLITERATE_TOGGLE_CASE:
            nMode = TransliterationFlags::TOGGLE_CASE;
            break;
        case SID_TRANSLITERATE_UPPER:
            nMode = TransliterationFlags::LOWERCASE_UPPERCASE;
            break;
        case SID_TRANSLITERATE_LOWER:
            nMode = TransliterationFlags::UPPERCASE_LOWERCASE;
            break;
        case SID_TRANSLITERATE_HALFWIDTH:
            nMode = TransliterationFlags::FULLWIDTH_HALFWIDTH;
            break;
        case SID_TRANSLITERATE_FULLWIDTH:
            nMode = TransliterationFlags::HALFWIDTH_FULLWIDTH;
            break;
        case SID_TRANSLITERATE_HIRAGANA:
            nMode = TransliterationFlags::KATAKANA_HIRAGANA;
            break;
        case SID_TRANSLITERATE_KATAKANA:
            nMode = TransliterationFlags::HIRAGANA_KATAKANA;
            break;

        default:
            OSL_ENSURE(false, "wrong dispatcher");
    }

    if( nMode != TransliterationFlags::NONE )
        pOLV->TransliterateText( nMode );
}

void SwAnnotationShell::ExecRotateTransliteration( SfxRequest const & rReq )
{
    if( rReq.GetSlot() != SID_TRANSLITERATE_ROTATE_CASE )
        return;

    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if (!pPostItMgr || !pPostItMgr->HasActiveSidebarWin())
        return;

    OutlinerView* pOLV = pPostItMgr->GetActiveSidebarWin()->GetOutlinerView();

    if (!pOLV)
        return;

    pOLV->TransliterateText(m_aRotateCase.getNextMode());
}

void SwAnnotationShell::ExecUndo(SfxRequest &rReq)
{
    const SfxItemSet* pArgs = rReq.GetArgs();
    SfxUndoManager* pUndoManager = GetUndoManager();
    SwWrtShell &rSh = m_rView.GetWrtShell();
    SwUndoId nUndoId(SwUndoId::EMPTY);

    // tdf#147928 get these before "undo" which may delete this SwAnnotationShell
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    SfxBindings& rBindings = m_rView.GetViewFrame().GetBindings();

    tools::Long aOldHeight = pPostItMgr->HasActiveSidebarWin()
                      ? pPostItMgr->GetActiveSidebarWin()->GetPostItTextHeight()
                      : 0;

    sal_uInt16 nId = rReq.GetSlot();
    sal_uInt16 nCnt = 1;
    const SfxPoolItem* pItem=nullptr;
    if( pArgs && SfxItemState::SET == pArgs->GetItemState( nId, false, &pItem ) )
        nCnt = static_cast<const SfxUInt16Item*>(pItem)->GetValue();
    switch( nId )
    {
        case SID_UNDO:
        {
            rSh.GetLastUndoInfo(nullptr, &nUndoId);
            if (nUndoId == SwUndoId::CONFLICT)
            {
                rReq.SetReturnValue( SfxUInt32Item(nId, static_cast<sal_uInt32>(SID_REPAIRPACKAGE)) );
                break;
            }

            if ( pUndoManager )
            {
                sal_uInt16 nCount = pUndoManager->GetUndoActionCount();
                sal_uInt16 nSteps = nCnt;
                if ( nCount < nCnt )
                {
                    nCnt = nCnt - nCount;
                    nSteps = nCount;
                }
                else
                    nCnt = 0;

                while( nSteps-- )
                    pUndoManager->Undo();
            }

            if ( nCnt )
                rSh.Do( SwWrtShell::UNDO, nCnt );

            break;
        }

        case SID_REDO:
        {
            (void)rSh.GetFirstRedoInfo(nullptr, &nUndoId);
            if (nUndoId == SwUndoId::CONFLICT)
            {
                rReq.SetReturnValue( SfxUInt32Item(nId, static_cast<sal_uInt32>(SID_REPAIRPACKAGE)) );
                break;
            }

            if ( pUndoManager )
            {
                sal_uInt16 nCount = pUndoManager->GetRedoActionCount();
                sal_uInt16 nSteps = nCnt;
                if ( nCount < nCnt )
                {
                    nCnt = nCnt - nCount;
                    nSteps = nCount;
                }
                else
                    nCnt = 0;

                while( nSteps-- )
                    pUndoManager->Redo();
            }

            if ( nCnt )
                rSh.Do( SwWrtShell::REDO, nCnt );

            break;
        }
    }

    rBindings.InvalidateAll(false);

    if (pPostItMgr->HasActiveSidebarWin())
        pPostItMgr->GetActiveSidebarWin()->ResizeIfNecessary(aOldHeight, pPostItMgr->GetActiveSidebarWin()->GetPostItTextHeight());
}

void SwAnnotationShell::StateUndo(SfxItemSet &rSet)
{
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if ( !pPostItMgr || !pPostItMgr->HasActiveSidebarWin() )
        return;

    SfxWhichIter aIter(rSet);
    SwUndoId nUndoId(SwUndoId::EMPTY);
    sal_uInt16 nWhich = aIter.FirstWhich();
    SfxUndoManager* pUndoManager = GetUndoManager();
    SfxViewFrame& rSfxViewFrame = m_rView.GetViewFrame();
    SwWrtShell &rSh = m_rView.GetWrtShell();

    while( nWhich )
    {
        switch ( nWhich )
        {
        case SID_UNDO:
            {
                sal_uInt16 nCount = pUndoManager ? pUndoManager->GetUndoActionCount() : 0;
                if ( nCount )
                    rSfxViewFrame.GetSlotState( nWhich, rSfxViewFrame.GetInterface(), &rSet );
                else if (rSh.GetLastUndoInfo(nullptr, &nUndoId))
                {
                    rSet.Put( SfxStringItem( nWhich, rSh.GetDoString(SwWrtShell::UNDO)) );
                }
                else if (nUndoId == SwUndoId::CONFLICT)
                {
                    rSet.Put( SfxUInt32Item(nWhich, static_cast<sal_uInt32>(SID_REPAIRPACKAGE)) );
                }
                else
                    rSet.DisableItem(nWhich);
                break;
            }
        case SID_REDO:
            {
                sal_uInt16 nCount = pUndoManager ? pUndoManager->GetRedoActionCount() : 0;
                if ( nCount )
                    rSfxViewFrame.GetSlotState( nWhich, rSfxViewFrame.GetInterface(), &rSet );
                else if (rSh.GetFirstRedoInfo(nullptr, &nUndoId))
                {
                    rSet.Put(SfxStringItem( nWhich, rSh.GetDoString(SwWrtShell::REDO)) );
                }
                else if (nUndoId == SwUndoId::CONFLICT)
                {
                    rSet.Put( SfxUInt32Item(nWhich, static_cast<sal_uInt32>(SID_REPAIRPACKAGE)) );
                }
                else
                    rSet.DisableItem(nWhich);
                break;
            }
        case SID_GETUNDOSTRINGS:
        case SID_GETREDOSTRINGS:
            {
                if( pUndoManager )
                {
                    OUString (SfxUndoManager::*fnGetComment)( size_t, bool const ) const;

                    sal_uInt16 nCount;
                    if( SID_GETUNDOSTRINGS == nWhich )
                    {
                        nCount = pUndoManager->GetUndoActionCount();
                        fnGetComment = &SfxUndoManager::GetUndoActionComment;
                    }
                    else
                    {
                        nCount = pUndoManager->GetRedoActionCount();
                        fnGetComment = &SfxUndoManager::GetRedoActionComment;
                    }

                    OUStringBuffer sList;
                    if( nCount )
                    {
                        for( sal_uInt16 n = 0; n < nCount; ++n )
                            sList.append( (pUndoManager->*fnGetComment)( n, SfxUndoManager::TopLevel ) + "\n");
                    }

                    SfxStringListItem aItem( nWhich );
                    if ((nWhich == SID_GETUNDOSTRINGS) &&
                        rSh.GetLastUndoInfo(nullptr, nullptr))
                    {
                        rSh.GetDoStrings( SwWrtShell::UNDO, aItem );
                    }
                    else if ((nWhich == SID_GETREDOSTRINGS) &&
                             (rSh.GetFirstRedoInfo(nullptr, nullptr)))
                    {
                        rSh.GetDoStrings( SwWrtShell::REDO, aItem );
                    }

                    sList.append(aItem.GetString());
                    aItem.SetString( sList.makeStringAndClear() );
                    rSet.Put( aItem );
                }
                else
                    rSet.DisableItem( nWhich );
            }
            break;

        default:
            {
                rSfxViewFrame.GetSlotState( nWhich, rSfxViewFrame.GetInterface(), &rSet );
                break;
            }

        }

        if (pPostItMgr->GetActiveSidebarWin()->GetLayoutStatus()==SwPostItHelper::DELETED)
            rSet.DisableItem( nWhich );

        nWhich = aIter.NextWhich();
    }
}

void SwAnnotationShell::StateDisableItems( SfxItemSet &rSet )
{
    SfxWhichIter aIter(rSet);
    sal_uInt16 nWhich = aIter.FirstWhich();
    while (nWhich)
    {
        rSet.DisableItem( nWhich );
        nWhich = aIter.NextWhich();
    }
}

void SwAnnotationShell::InsertSymbol(SfxRequest& rReq)
{
    SwPostItMgr* pPostItMgr = m_rView.GetPostItMgr();
    if ( !pPostItMgr || !pPostItMgr->HasActiveSidebarWin() )
        return;

    OutlinerView* pOLV = pPostItMgr->GetActiveSidebarWin()->GetOutlinerView();

    const SfxItemSet *pArgs = rReq.GetArgs();
    const SfxStringItem* pCharMapItem = nullptr;
    if( pArgs )
        pCharMapItem = pArgs->GetItemIfSet(SID_CHARMAP, false);

    OUString sSym;
    OUString sFontName;
    if ( pCharMapItem )
    {
        sSym = pCharMapItem->GetValue();
        const SfxStringItem* pFontItem = pArgs->GetItemIfSet( SID_ATTR_SPECIALCHAR, false);
        if (pFontItem)
            sFontName = pFontItem->GetValue();
    }

    SfxItemSet aSet(pOLV->GetAttribs());
    SvtScriptType nScript = pOLV->GetSelectedScriptType();
    std::shared_ptr<SvxFontItem> aSetDlgFont(std::make_shared<SvxFontItem>(RES_CHRATR_FONT));
    {
        SvxScriptSetItem aSetItem( SID_ATTR_CHAR_FONT, *aSet.GetPool() );
        aSetItem.GetItemSet().Put( aSet, false );
        const SfxPoolItem* pI = aSetItem.GetItemOfScript( nScript );
        if( pI )
        {
            aSetDlgFont.reset(static_cast<SvxFontItem*>(pI->Clone()));
        }
        else
        {
            TypedWhichId<SvxFontItem> nFontWhich =
                GetWhichOfScript(
                        SID_ATTR_CHAR_FONT,
                        SvtLanguageOptions::GetI18NScriptTypeOfLanguage( GetAppLanguage() ) );
            aSetDlgFont.reset(aSet.Get(nFontWhich).Clone());
        }

        if (sFontName.isEmpty())
            sFontName = aSetDlgFont->GetFamilyName();
    }

    vcl::Font aFont(sFontName, Size(1,1));
    if( sSym.isEmpty() )
    {
        SvxAbstractDialogFactory* pFact = SvxAbstractDialogFactory::Create();

        SfxAllItemSet aAllSet( GetPool() );
        aAllSet.Put( SfxBoolItem( FN_PARAM_1, false ) );

        SwViewOption aOpt(*m_rView.GetWrtShell().GetViewOptions());
        const OUString& sSymbolFont = aOpt.GetSymbolFont();
        if( !sSymbolFont.isEmpty() )
            aAllSet.Put( SfxStringItem( SID_FONT_NAME, sSymbolFont ) );
        else
            aAllSet.Put( SfxStringItem( SID_FONT_NAME, aSetDlgFont->GetFamilyName() ) );

        // If character is selected then it can be shown.
        auto xFrame = m_rView.GetViewFrame().GetFrame().GetFrameInterface();
        VclPtr<SfxAbstractDialog> pDlg(pFact->CreateCharMapDialog(m_rView.GetFrameWeld(), aAllSet, xFrame));
        pDlg->StartExecuteAsync(
            [pDlg] (sal_Int32 /*nResult*/)->void
            {
                pDlg->disposeOnce();
            }
        );
        return;
    }

    // do not flicker
    pOLV->HideCursor();
    Outliner& rOutliner = pOLV->GetOutliner();
    rOutliner.SetUpdateLayout(false);

    SfxItemSet aOldSet( pOLV->GetAttribs() );
    SfxItemSet aFontSet(SfxItemSet::makeFixedSfxItemSet<
        EE_CHAR_FONTINFO, EE_CHAR_FONTINFO, EE_CHAR_FONTINFO_CJK,
        EE_CHAR_FONTINFO_CTL>(*aOldSet.GetPool()));
    aFontSet.Set( aOldSet );

    // Insert string
    pOLV->InsertText( sSym);

    // Attributing (set font)
    SfxItemSet aSetFont( *aFontSet.GetPool(), aFontSet.GetRanges() );
    SvxFontItem aFontItem (aFont.GetFamilyTypeMaybeAskConfig(), aFont.GetFamilyName(),
                            aFont.GetStyleName(), aFont.GetPitchMaybeAskConfig(),
                            aFont.GetCharSet(),
                            EE_CHAR_FONTINFO );
    SvtScriptType nScriptBreak = g_pBreakIt->GetAllScriptsOfText( sSym );
    if( SvtScriptType::LATIN & nScriptBreak )
        aSetFont.Put( aFontItem );
    if( SvtScriptType::ASIAN & nScriptBreak )
    {
        aFontItem.SetWhich(EE_CHAR_FONTINFO_CJK);
        aSetFont.Put( aFontItem );
    }
    if( SvtScriptType::COMPLEX & nScriptBreak )
    {
        aFontItem.SetWhich(EE_CHAR_FONTINFO_CTL);
        aSetFont.Put( aFontItem );
    }
    pOLV->SetAttribs(aSetFont);

    // Erase selection
    ESelection aSel(pOLV->GetSelection());
    aSel.CollapseToEnd();
    pOLV->SetSelection(aSel);

    // Restore old font
    pOLV->SetAttribs( aFontSet );

    // From now on show it again
    rOutliner.SetUpdateLayout(true);
    pOLV->ShowCursor();

    rReq.AppendItem( SfxStringItem( SID_CHARMAP, sSym ) );
    if(!aFont.GetFamilyName().isEmpty())
        rReq.AppendItem( SfxStringItem( SID_ATTR_SPECIALCHAR, aFont.GetFamilyName() ) );
    rReq.Done();

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
