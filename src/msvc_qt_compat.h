// 兼容层：VS 18（Visual Studio 2026）移除了 stdext 命名空间，
// 而 Qt 5.15 的 qlist.h / qvector.h 仍在用
// stdext::make_checked_array_iterator，于是报
//     error C3861: 'stdext': identifier not found
//
// 处理办法：在 Qt 头文件把宏定义好之后，用它自己的等价写法覆盖掉。
// 强制包含（/FI）保证这段代码在所有 Qt 头文件之前执行：
// 先包含 qcompilerdetection.h（让它的包含哨兵生效，后续再包含就是空操作），
// 再把两个宏改写成「直接用指针」的版本 ——
// std::equal(b, e, vb) 本来就只需要一个输入迭代器，普通指针完全满足。
//
// 仅在 MSVC 18 及以上生效：更早的 MSVC 自带 stdext，不需要这层补丁。
//
// 用法（见 CMakeLists.txt）：target_compile_options(... PUBLIC "/FI<此文件路径>")

#pragma once

#if defined(_MSC_VER) && _MSC_VER >= 1950

#include <QtCore/qcompilerdetection.h>

#undef QT_MAKE_CHECKED_ARRAY_ITERATOR
#define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) (x)

#undef QT_MAKE_UNCHECKED_ARRAY_ITERATOR
#define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(x) (x)

#endif  // _MSC_VER >= 1950
