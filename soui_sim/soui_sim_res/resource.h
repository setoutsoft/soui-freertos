//stamp:0775a5841b13837f
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
		struct _PNG{
			const TCHAR * TEST_PNG;
			const TCHAR * LOGO_PNG;
			}PNG;
		struct _SVG{
			const TCHAR * TEST_SVG;
			const TCHAR * focus_ani;
			}SVG;
		struct _values{
			const TCHAR * string;
			const TCHAR * color;
			const TCHAR * skin;
			}values;
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
		{
			_T("PNG:TEST_PNG"),
			_T("PNG:LOGO_PNG"),
		},
		{
			_T("SVG:TEST_SVG"),
			_T("SVG:focus_ani"),
		},
		{
			_T("values:string"),
			_T("values:color"),
			_T("values:skin"),
		},
	};
#else
extern struct _UIRES UIRES;
#endif//INIT_R_DATA

#ifndef _R_H_
#define _R_H_
struct _R{
	struct _name{
		 const wchar_t * anim_box;
		 const wchar_t * btn_anim;
		 const wchar_t * btn_close;
		 const wchar_t * btn_min;
		 const wchar_t * btn_test;
		 const wchar_t * cbx_field;
		 const wchar_t * cbx_speed;
		 const wchar_t * fade_box;
		 const wchar_t * img_view;
		 const wchar_t * txt_title;
	}name;
	struct _id{
		int anim_box;
		int btn_anim;
		int btn_close;
		int btn_min;
		int btn_test;
		int cbx_field;
		int cbx_speed;
		int fade_box;
		int img_view;
		int txt_title;
	}id;
	struct _color{
		int blue;
		int orange;
	}color;
	struct _string{
		int anim_label;
		int btn_anim;
		int btn_switch;
		int cbx_fast;
		int cbx_normal;
		int cbx_slow;
		int hello;
		int sub;
		int title;
	}string;
#ifdef __cplusplus
	struct {
		enum {
			anim_box = 65544,
			btn_anim = 65543,
			btn_close = 65538,
			btn_min = 65537,
			btn_test = 65542,
			cbx_field = 65540,
			cbx_speed = 65541,
			fade_box = 65545,
			img_view = 65539,
			txt_title = 65536
		};
	}ID;
#endif//__cplusplus

};
#endif//_R_H_
#ifdef INIT_R_DATA
struct _R R={
	{
		L"anim_box",
		L"btn_anim",
		L"btn_close",
		L"btn_min",
		L"btn_test",
		L"cbx_field",
		L"cbx_speed",
		L"fade_box",
		L"img_view",
		L"txt_title"
	}
	,
	{
		65544,
		65543,
		65538,
		65537,
		65542,
		65540,
		65541,
		65545,
		65539,
		65536
	}
	,
	{
		0,
		1
	}
	,
	{
		0,
		1,
		2,
		3,
		4,
		5,
		6,
		7,
		8
	}
	
};
#else
extern struct _R R;
#endif//INIT_R_DATA
