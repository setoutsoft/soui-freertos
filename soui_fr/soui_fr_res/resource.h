//stamp:03bae9451e024a99
/*<------------------------------------------------------------------------------------------------->*/
/*该文件由uiresbuilder生成，请不要手动修改*/
/*<------------------------------------------------------------------------------------------------->*/
#ifndef _UIRES_H_
#define _UIRES_H_
	struct _UIRES{
		struct _UIDEF{
			const TCHAR * XML_INIT;
			}UIDEF;
		struct _LAYOUT{
			const TCHAR * XML_MAINWND;
			}LAYOUT;
	};
#endif//_UIRES_H_
#ifdef INIT_R_DATA
struct _UIRES UIRES={
		{
			_T("UIDEF:XML_INIT"),
		},
		{
			_T("LAYOUT:XML_MAINWND"),
		},
	};
#else
extern struct _UIRES UIRES;
#endif//INIT_R_DATA

#ifndef _R_H_
#define _R_H_
struct _R{
	struct _name{
		 const wchar_t * ico_demo16;
		 const wchar_t * ico_demo32;
		 const wchar_t * txt_caption;
		 const wchar_t * txt_status;
		 const wchar_t * txt_title;
		 const wchar_t * wnd_banner;
	}name;
	struct _id{
		int ico_demo16;
		int ico_demo32;
		int txt_caption;
		int txt_status;
		int txt_title;
		int wnd_banner;
	}id;
#ifdef __cplusplus
	struct {
		enum {
			ico_demo16 = 65538,
			ico_demo32 = 65539,
			txt_caption = 65540,
			txt_status = 65541,
			txt_title = 65537,
			wnd_banner = 65536
		};
	}ID;
#endif//__cplusplus

};
#endif//_R_H_
#ifdef INIT_R_DATA
struct _R R={
	{
		L"ico_demo16",
		L"ico_demo32",
		L"txt_caption",
		L"txt_status",
		L"txt_title",
		L"wnd_banner"
	}
	,
	{
		65538,
		65539,
		65540,
		65541,
		65537,
		65536
	}
	,
};
#else
extern struct _R R;
#endif//INIT_R_DATA
