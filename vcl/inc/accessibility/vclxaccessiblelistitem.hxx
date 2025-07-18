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

#pragma once

#include <com/sun/star/accessibility/XAccessible.hpp>
#include <com/sun/star/accessibility/XAccessibleText.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <comphelper/accessibletexthelper.hxx>
#include <accessibility/vclxaccessiblelist.hxx>

class VCLXAccessibleListItem final
    : public cppu::ImplInheritanceHelper<
          comphelper::OAccessible, css::accessibility::XAccessibleText, css::lang::XServiceInfo>,
      public comphelper::OCommonAccessibleText
{
private:
    OUString                            m_sEntryText;
    sal_Int32                           m_nIndexInParent;
    bool                            m_bSelected;
    bool                            m_bVisible;

    rtl::Reference< VCLXAccessibleList >                     m_xParent;

    virtual ~VCLXAccessibleListItem() override = default;

    virtual void SAL_CALL disposing() override;

    // OCommonAccessibleText
    virtual OUString                        implGetText() override;
    virtual css::lang::Locale               implGetLocale() override;
    virtual void                            implGetSelection( sal_Int32& nStartIndex, sal_Int32& nEndIndex ) override;

    // OAccessible
    virtual css::awt::Rectangle implGetBounds() override;

    OUString getTextRangeImpl(sal_Int32 nStartIndex, sal_Int32 nEndIndex);

public:
    /** OAccessibleBase needs a valid view
        @param  _nIndexInParent
            is the position of the entry inside the listbox
        @param  _xParent
            is our parent accessible object
    */
    VCLXAccessibleListItem(sal_Int32 _nIndexInParent,
                           rtl::Reference< VCLXAccessibleList > _xParent);

    void                    NotifyAccessibleEvent(  sal_Int16 _nEventId, const css::uno::Any& _aOldValue, const css::uno::Any& _aNewValue );

    bool         IsSelected() const { return m_bSelected; }
    void                    SetSelected( bool _bSelected );
    void                    SetVisible( bool _bVisible );

    // XServiceInfo
    virtual OUString SAL_CALL getImplementationName() override;
    virtual sal_Bool SAL_CALL supportsService( const OUString& rServiceName ) override;
    virtual css::uno::Sequence< OUString > SAL_CALL getSupportedServiceNames() override;

    // XAccessibleContext
    virtual sal_Int64 SAL_CALL getAccessibleChildCount(  ) override;
    virtual css::uno::Reference< css::accessibility::XAccessible > SAL_CALL getAccessibleChild( sal_Int64 i ) override;
    virtual css::uno::Reference< css::accessibility::XAccessible > SAL_CALL getAccessibleParent(  ) override;
    virtual sal_Int64 SAL_CALL getAccessibleIndexInParent(  ) override;
    virtual sal_Int16 SAL_CALL getAccessibleRole(  ) override;
    virtual OUString SAL_CALL getAccessibleDescription(  ) override;
    virtual OUString SAL_CALL getAccessibleName(  ) override;
    virtual css::uno::Reference< css::accessibility::XAccessibleRelationSet > SAL_CALL getAccessibleRelationSet(  ) override;
    virtual sal_Int64 SAL_CALL getAccessibleStateSet(  ) override;
    virtual css::lang::Locale SAL_CALL getLocale(  ) override;

    // XAccessibleComponent
    virtual css::uno::Reference< css::accessibility::XAccessible > SAL_CALL getAccessibleAtPoint( const css::awt::Point& aPoint ) override;
    virtual void SAL_CALL grabFocus(  ) override;
    virtual sal_Int32 SAL_CALL getForeground() override;
    virtual sal_Int32 SAL_CALL getBackground() override;

    // XAccessibleText
    virtual sal_Int32 SAL_CALL getCaretPosition() override;
    virtual sal_Bool SAL_CALL setCaretPosition( sal_Int32 nIndex ) override;
    virtual sal_Unicode SAL_CALL getCharacter( sal_Int32 nIndex ) override;
    virtual css::uno::Sequence< css::beans::PropertyValue > SAL_CALL getCharacterAttributes( sal_Int32 nIndex, const css::uno::Sequence< OUString >& aRequestedAttributes ) override;
    virtual css::awt::Rectangle SAL_CALL getCharacterBounds( sal_Int32 nIndex ) override;
    virtual sal_Int32 SAL_CALL getCharacterCount() override;
    virtual sal_Int32 SAL_CALL getIndexAtPoint( const css::awt::Point& aPoint ) override;
    virtual OUString SAL_CALL getSelectedText() override;
    virtual sal_Int32 SAL_CALL getSelectionStart() override;
    virtual sal_Int32 SAL_CALL getSelectionEnd() override;
    virtual sal_Bool SAL_CALL setSelection( sal_Int32 nStartIndex, sal_Int32 nEndIndex ) override;
    virtual OUString SAL_CALL getText() override;
    virtual OUString SAL_CALL getTextRange( sal_Int32 nStartIndex, sal_Int32 nEndIndex ) override;
    virtual css::accessibility::TextSegment SAL_CALL getTextAtIndex( sal_Int32 nIndex, sal_Int16 aTextType ) override;
    virtual css::accessibility::TextSegment SAL_CALL getTextBeforeIndex( sal_Int32 nIndex, sal_Int16 aTextType ) override;
    virtual css::accessibility::TextSegment SAL_CALL getTextBehindIndex( sal_Int32 nIndex, sal_Int16 aTextType ) override;
    virtual sal_Bool SAL_CALL copyText( sal_Int32 nStartIndex, sal_Int32 nEndIndex ) override;
    virtual sal_Bool SAL_CALL scrollSubstringTo( sal_Int32 nStartIndex, sal_Int32 nEndIndex, css::accessibility::AccessibleScrollType aScrollType) override;
};


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
