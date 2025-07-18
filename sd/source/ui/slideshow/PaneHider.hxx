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

#include <com/sun/star/uno/Reference.hxx>
#include <rtl/ref.hxx>

namespace sd::framework
{
class Configuration;
}
namespace sd::framework
{
class ConfigurationController;
}

namespace sd
{
class ViewShell;
class SlideshowImpl;

/** Hide the windows of the side panes and restore the original visibility
    later. Used by the in-window slide show in order to use the whole frame
    window for the show.
*/
class PaneHider
{
public:
    /** The constructor hides all side panes that belong to the
        ViewShellBase of the given view shell.
    */
    PaneHider(const ViewShell& rViewShell, SlideshowImpl* pSlideShow);

    /** Restore the original visibility of the side panes.
    */
    ~PaneHider();

private:
    /** Remember whether the visibility states of the windows of the  panes
        has been modified and have to be restored.
    */

    rtl::Reference<sd::framework::ConfigurationController> mxConfigurationController;
    rtl::Reference<sd::framework::Configuration> mxConfiguration;
};

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
