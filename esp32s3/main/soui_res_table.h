/*
 * soui_res_table.h -- (type, name) -> raw blob table for the in-memory
 * IResProvider on ESP32-S3.
 *
 * The table itself is generated at development time by tools/gen_uires.py
 * (main/generated/uires_res.cpp).  Keeping the payload out of the PE resource
 * chain is deliberate: uires -> windres lands in ".section .rdata", which the
 * ESP-IDF linker scripts do not collect.
 *
 * `type`/`name` are the exact strings SOUI passes to
 * IResProvider::GetRawBuffer(type, name):
 *     _LoadXmlDocment(L"SYS_XML_SKIN", L"XML", ...)  -> ("XML",    "SYS_XML_SKIN")
 *     AddResProvider(appRes, L"UIDEF:XML_INIT")      -> ("UIDEF",  "XML_INIT")
 *     <string src="values:string"/>                  -> ("values", "string")
 */
#ifndef SOUI_ESP32S3_RES_TABLE_H_
#define SOUI_ESP32S3_RES_TABLE_H_

struct SouiResEntry
{
    const char *type;
    const char *name;
    const char *data;
    unsigned int len;
};

/* Application uires (layouts, values, images) -- soui_sim/uires. */
extern const SouiResEntry g_appResTable[];
extern const int g_appResTable_count;

/* SOUI system resources -- soui_sys_res/theme_sys_res (SVG variant). */
extern const SouiResEntry g_sysResTable[];
extern const int g_sysResTable_count;

#endif // SOUI_ESP32S3_RES_TABLE_H_
