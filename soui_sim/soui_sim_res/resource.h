//stamp:0775cc670cbcacf5
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
		 const wchar_t * btn_kb;
		 const wchar_t * btn_test;
		 const wchar_t * cbx_field;
		 const wchar_t * cbx_speed;
		 const wchar_t * edt_field;
		 const wchar_t * edt_input;
		 const wchar_t * fade_box;
		 const wchar_t * fps_label;
		 const wchar_t * ico_demo;
		 const wchar_t * img_view;
		 const wchar_t * softkb;
		 const wchar_t * txt_title;
	}name;
	struct _id{
		int anim_box;
		int btn_anim;
		int btn_kb;
		int btn_test;
		int cbx_field;
		int cbx_speed;
		int edt_field;
		int edt_input;
		int fade_box;
		int fps_label;
		int ico_demo;
		int img_view;
		int softkb;
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
		int edt_cue;
		int hello;
		int sub;
		int title;
	}string;
#ifdef __cplusplus
	struct {
		enum {
			anim_box = 65546,
			btn_anim = 65542,
			btn_kb = 65543,
			btn_test = 65541,
			cbx_field = 65539,
			cbx_speed = 65540,
			edt_field = 65544,
			edt_input = 65545,
			fade_box = 65547,
			fps_label = 65548,
			ico_demo = 65536,
			img_view = 65538,
			softkb = 65549,
			txt_title = 65537
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
		L"btn_kb",
		L"btn_test",
		L"cbx_field",
		L"cbx_speed",
		L"edt_field",
		L"edt_input",
		L"fade_box",
		L"fps_label",
		L"ico_demo",
		L"img_view",
		L"softkb",
		L"txt_title"
	}
	,
	{
		65546,
		65542,
		65543,
		65541,
		65539,
		65540,
		65544,
		65545,
		65547,
		65548,
		65536,
		65538,
		65549,
		65537
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
		8,
		9
	}
	
};
#else
extern struct _R R;
#endif//INIT_R_DATA
