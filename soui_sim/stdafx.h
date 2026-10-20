/*
 * stdafx.h -- shared includes of the SOUI-on-FreeRTOS simulator demo.
 *
 * Same role as wizard/CmakeApp/stdafx.h: every translation unit starts here,
 * so the SDK, the static-COM selection and the uiresbuilder-generated ids are
 * identical everywhere.  It is also the precompiled header of the target.
 */
#ifndef SOUISIM_STDAFX_H_
#define SOUISIM_STDAFX_H_

// -----------------------------------------------------------------------------
// static COM components
//
// soui_fr_link_app() links the *static* COM components (soui4_fr,
// imgdecoder_stb_fr, render-gdi).  SComMgr2 only emits the branches listed in
// SCOM_MASK, so the mask must be exactly the set that is linked -- a wider mask
// would reference factories that were never built.
// -----------------------------------------------------------------------------
#include <commask.h>
#define SCOM_MASK (scom_mask_render_gdi | scom_mask_imgdecoder_stb)

#include <souistd.h>
#include <core/SHostWnd.h>
#include <helper/SDpiHelper.hpp>
#include <control/SouiCtrls.h>
#include <res.mgr/SObjDefAttr.h>
#include <SouiFactory.h>

// uiresbuilder output: "LAYOUT:..."/"UIDEF:..." constants, plus the UIRES and
// R tables (R.id.* / R.string.* / R.color.*).  Generated into the build tree by
// soui_fr_add_resources(), the directory is on this target's include path.
#include "resource.h"

using namespace SNS;

#endif // SOUISIM_STDAFX_H_
