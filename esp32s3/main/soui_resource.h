/*
 * soui_resource.h -- resource ids of the soui_sim demo layout.
 *
 * On a desktop/wizard build this file is emitted by uiresbuilder from
 * uires/uires.idx (see soui_sim/soui_sim_res/resource.h).  The ESP32-S3 port
 * embeds the same resources as raw blobs (main/generated/uires_res.cpp), so
 * only the *ids* are needed here.  The numbering is the builder's stable
 * scheme -- keep it identical to soui_sim_res/resource.h or the
 * EVENT_ID_COMMAND handlers stop matching.
 */
#ifndef SOUI_ESP32S3_SOUI_RESOURCE_H_
#define SOUI_ESP32S3_SOUIRES_H_

namespace soui_demo_res
{

/* Layout id, resolved through IResProvider::GetRawBuffer("LAYOUT", "XML_MAINWND"). */
static const char *const kLayoutMain = "LAYOUT:XML_MAINWND";

/* uidef id: AddResProvider(appRes, "UIDEF:XML_INIT"). */
static const char *const kUidefInit = "UIDEF:XML_INIT";

/* Widget ids (R.id.* in the generated header). */
enum
{
    ID_TXT_TITLE = 65536,
    ID_BTN_MIN = 65537,
    ID_BTN_CLOSE = 65538,
    ID_IMG_VIEW = 65539,
    ID_CBX_FIELD = 65540,
    ID_CBX_SPEED = 65541,
    ID_BTN_TEST = 65542,
    ID_BTN_ANIM = 65543,
    ID_EDT_FIELD = 65544,
    ID_EDT_INPUT = 65545,
    ID_ANIM_BOX = 65546,
    ID_FADE_BOX = 65547,
};

/* Feeds SApplication::InitXmlNamedID(): two parallel arrays, same order.
 * It sorts internally, so the order below only has to be self-consistent. */
static const wchar_t *const kNames[] = {
    L"anim_box",   L"btn_anim",  L"btn_close",  L"btn_min",
    L"btn_test",   L"cbx_field", L"cbx_speed",  L"edt_field",
    L"edt_input",  L"fade_box",  L"img_view",   L"txt_title",
};
static const int kIds[] = {
    ID_ANIM_BOX, ID_BTN_ANIM,   ID_BTN_CLOSE,  ID_BTN_MIN,
    ID_BTN_TEST, ID_CBX_FIELD,  ID_CBX_SPEED,  ID_EDT_FIELD,
    ID_EDT_INPUT, ID_FADE_BOX,  ID_IMG_VIEW,   ID_TXT_TITLE,
};
static const int kNamedIdCount = (int)(sizeof(kIds) / sizeof(kIds[0]));

} // namespace soui_demo_res

#endif // SOUI_ESP32S3_SOUIRES_H_
