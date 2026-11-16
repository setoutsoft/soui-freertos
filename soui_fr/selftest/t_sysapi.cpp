/*
 * soui_fr/selftest/t_sysapi.cpp -- src/platform/common/sysapi.cpp 的裸机侧验证
 *
 * 这个文件由 posix 家族与 freertos 共用一份实现（见 swinx/AGENTS.md 的"中立层"），
 * 但 demos/fun_test 只在桌面平台跑，arm-none-eabi 这一侧无人验证。这里补上最容易
 * 被"顺手重写"的几条语义：整数字符串解析、MulDiv 的舍入与失败路径、度量缩放。
 *
 * 断言里的期望值与 demos/fun_test/test_strapi.cpp、test_misc.cpp 一一对应：
 * 两边都对着 Win32 的实测行为，任一边被改坏都会在这里或那里变红。
 */
#include "test_common.h"

#include <windows.h>

// ---------------------------------------------------------------------------
// StrToIntEx / StrToInt64Ex（shlwapi 的整数字符串解析）
// ---------------------------------------------------------------------------
static void strtoint_test(void)
{
    INT v = -999;

    // 十进制：0 前缀不是八进制（旧 freertos 版本是裸 strtol(,,0)，"010" 会读成 8）
    CHECK(StrToIntExA("010", 0, &v) && v == 10);
    CHECK(StrToIntExA("0755", 0, &v) && v == 755);

    // 0x 只有带 STIF_SUPPORT_HEX 才认；带 flag 时符号被丢弃（"-0x10" == 16）
    CHECK(StrToIntExA("0x1A", STIF_SUPPORT_HEX, &v) && v == 26);
    CHECK(StrToIntExA("0x1A", 0, &v) && v == 0);
    CHECK(StrToIntExA("-0x10", STIF_SUPPORT_HEX, &v) && v == 16);

    // 前导空白与正负号；'\v' 不是分隔符
    CHECK(StrToIntExA(" \t42", 0, &v) && v == 42);
    CHECK(!StrToIntExA("\v42", 0, &v));

    // 失败时把输出参数清 0（不是"保持原值"）
    v = 123;
    CHECK(!StrToIntExA("abc", 0, &v) && v == 0);

    // 宽版：A 版先 towstring 再调它，两条入口必须同口径
    CHECK(StrToIntExW(L"-17", 0, &v) && v == -17);
    CHECK(StrToIntExW(L"  +9", 0, &v) && v == 9);
    CHECK(StrToIntExW(L"010", 0, &v) && v == 10);
    CHECK(!StrToIntExW(L"0x", STIF_SUPPORT_HEX, &v) && v == 0);

    LONGLONG v64 = 0;
    CHECK(StrToInt64ExA("9223372036854775807", 0, &v64) && v64 == 9223372036854775807LL);
    CHECK(StrToInt64ExA("-9223372036854775807", 0, &v64) && v64 == -9223372036854775807LL);
    CHECK(StrToInt64ExW(L"0x7FFFFFFFFFFFFFFF", STIF_SUPPORT_HEX, &v64) && v64 == 9223372036854775807LL);

    tprint("[sysapi] StrToIntEx decimal/hex/wide OK\n");
}

// ---------------------------------------------------------------------------
// MulDiv
// ---------------------------------------------------------------------------
static void muldiv_test(void)
{
    // Win32 四舍五入（.5 远离 0）
    CHECK(MulDiv(10, 10, 5) == 20);
    CHECK(MulDiv(25, 5, 10) == 13);
    CHECK(MulDiv(-25, 5, 10) == -13);
    CHECK(MulDiv(1, 1, 2) == 1);

    // 除数为 0 / 结果溢出都是 -1，不是崩溃
    CHECK(MulDiv(1, 1, 0) == -1);
    CHECK(MulDiv(2147483647, 2, 1) == -1);
    CHECK(MulDiv(2147483647, 1, 1) == 2147483647);

    tprint("[sysapi] MulDiv rounding/failure OK\n");
}

// ---------------------------------------------------------------------------
// 度量与双击间隔（两者都要经 SConnection，故放在 conn 测试之后跑）
// ---------------------------------------------------------------------------
static void metric_test(void)
{
    // 非客户区厚度在 swinx 里恒 0，边框/边缘恒 1
    CHECK(GetSystemMetrics(SM_CYCAPTION) == 0);
    CHECK(GetSystemMetrics(SM_CYMENU) == 0);
    CHECK(GetSystemMetrics(SM_CXEDGE) == 1);
    CHECK(GetSystemMetrics(SM_CYEDGE) == 1);

    // 可缩放的度量 = 取值表 × DPI 百分比：缩放钩子返回 0 或漏乘都会对不上
    const int scale = GetSystemScale();
    CHECK(scale > 0);
    CHECK(GetSystemMetrics(SM_CXICON) == 32 * scale / 100);
    CHECK(GetSystemMetrics(SM_CXMINTRACK) == 6 * scale / 100);

    // 屏幕尺寸来自 SConnection；裸机是固定分辨率
    CHECK(GetSystemMetrics(SM_CXSCREEN) > 0);
    CHECK(GetSystemMetrics(SM_CYSCREEN) > 0);

    // 双击间隔唯一来源是 SConnection::GetDoubleClickTime()（旧代码另有一份硬编码 500）
    const UINT dbl = GetDoubleClickTime();
    CHECK(dbl >= 100 && dbl <= 5000);
    CHECK(GetDoubleClickTime() == dbl);

    tprint("[sysapi] GetSystemMetrics/GetDoubleClickTime OK\n");
}

void run_sysapi_tests(void)
{
    strtoint_test();
    muldiv_test();
    metric_test();
}
