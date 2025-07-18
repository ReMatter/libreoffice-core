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

#ifndef INCLUDED_REPORTDESIGN_SOURCE_UI_INC_MARKEDSECTION_HXX
#define INCLUDED_REPORTDESIGN_SOURCE_UI_INC_MARKEDSECTION_HXX

#include <sal/types.h>

namespace rptui
{
class OSectionWindow;

enum class NearSectionAccess
{
    CURRENT = 0,
    PREVIOUS = -1,
    POST = 1
};

class IMarkedSection
{
public:
    /** returns the section which is currently marked.
        */
    virtual OSectionWindow* getMarkedSection(NearSectionAccess nsa
                                             = NearSectionAccess::CURRENT) const = 0;

    /** mark the section on the given position .
        *
        * \param _nPos the position is zero based.
        */
    virtual void markSection(const sal_uInt16 _nPos) = 0;

protected:
    ~IMarkedSection() {}
};

} // rptui

#endif // INCLUDED_REPORTDESIGN_SOURCE_UI_INC_MARKEDSECTION_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
