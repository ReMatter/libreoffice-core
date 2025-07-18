# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_UnpackedTarball_UnpackedTarball,icu))

$(eval $(call gb_UnpackedTarball_set_tarball,icu,$(ICU_TARBALL)))

$(eval $(call gb_UnpackedTarball_update_autoconf_configs,icu,source))

# Data zip contains data/... and needs to end up in icu/source/data/...
# Only data/misc/icudata.rc is needed for a Cygwin/MSVC build.
$(eval $(call gb_UnpackedTarball_set_pre_action,icu,\
	unzip -q -d source -o $(gb_UnpackedTarget_TARFILE_LOCATION)/$(ICU_DATA_TARBALL) data/misc/icudata.rc \
))

$(eval $(call gb_UnpackedTarball_set_patchlevel,icu,0))

$(eval $(call gb_UnpackedTarball_add_patches,icu,\
	external/icu/icu4c-build.patch.1 \
	external/icu/icu4c-warnings.patch.1 \
	external/icu/icu4c-macosx.patch.1 \
	external/icu/icu4c-solarisgcc.patch.1 \
	external/icu/icu4c-mkdir.patch.1 \
	external/icu/icu4c-ubsan.patch.1 \
	external/icu/icu4c-scriptrun.patch.1 \
	external/icu/icu4c-rtti.patch.1 \
	external/icu/icu4c-clang-cl.patch.1 \
	external/icu/c++20-comparison.patch.1 \
	external/icu/Wdeprecated-copy-dtor.patch \
	external/icu/icu4c-windows-cygwin-cross.patch.1 \
	external/icu/icu4c-emscripten-cross.patch.1 \
	external/icu/icu4c-use-pkgdata-single-ccode-file-mode.patch.1 \
	external/icu/icu4c-khmerbreakengine.patch.1 \
	external/icu/icu4c-$(if $(filter ANDROID,$(OS)),android,rpath).patch.1 \
	$(if $(filter-out ANDROID,$(OS)),external/icu/icu4c-icudata-stdlibs.patch.1) \
	external/icu/no-python.patch \
	external/icu/Wunnecessary-virtual-specifier.patch \
	external/icu/0001-ICU-23054-const-up-struct-that-gencmn-outputs.patch.2 \
))

$(eval $(call gb_UnpackedTarball_add_file,icu,source/data/brkitr/khmerdict.dict,external/icu/khmerdict.dict))

# cannot use post_action since $(file ..) would be run when the recipe is parsed, i.e. would always
# happen before the tarball is unpacked
$(gb_UnpackedTarball_workdir)/icu/icu-uc-uninstalled.pc: $(call gb_UnpackedTarball_get_target,icu)
	$(file >$@,$(call gb_pkgconfig_file,icu-uc,$(ICU_MAJOR).$(ICU_MINOR),$(ICU_CFLAGS),$(ICU_LIBS)))

# vim: set noet sw=4 ts=4:
