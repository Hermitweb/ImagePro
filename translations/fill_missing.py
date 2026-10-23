#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""填充 .ts 文件中缺失的翻译。

按 (context, source) 精确匹配 —— 同一个源串在不同上下文里可能有不同译法，
按源串全局替换会出错。

用法：
    python fill_missing.py <zh_CN.ts> <en.ts>
"""
import re
import sys

# (context, source) -> 中文译文
ZH = {
    ("yingtu::CompressEngine", "No images to compress"): "没有可压缩的图片",
    ("yingtu::CompressEngine", "Failed to create output file"): "无法创建输出文件",
    ("yingtu::StitchEngine", "Failed to create output file"): "无法创建输出文件",
    ("yingtu::ImageItemDelegate", "Load failed"): "加载失败",
    ("yingtu::ImageItemDelegate", "Retry"): "重试",
    ("yingtu::ImageListWidget", "Drag images here or click Add Images"): "拖拽图片到此处或点击「添加图片」",
    ("yingtu::ImageListWidget", "Show"): "显示",
    ("yingtu::ImageListWidget", "Hide"): "隐藏",
    ("yingtu::ImageListWidget", "Duplicate"): "复制",
    ("yingtu::ImageListWidget", "Copy to clipboard"): "复制到剪贴板",
    ("yingtu::ImageListWidget", "Move to front"): "移到最前",
    ("yingtu::ImageListWidget", "Move to back"): "移到最后",
    ("yingtu::ImageListWidget", "Rotate Left 90°"): "向左旋转 90°",
    ("yingtu::ImageListWidget", "Flip Horizontal"): "水平翻转",
    ("yingtu::ImageListWidget", "Flip Vertical"): "垂直翻转",
    ("yingtu::ImageListWidget", "Move Up"): "上移",
    ("yingtu::ImageListWidget", "Move Down"): "下移",
    ("yingtu::ImageListWidget", "Show in Explorer"): "在资源管理器中显示",
    ("yingtu::ImageListWidget", "Quick Look"): "快速查看",
    ("yingtu::ImageListWidget", "Remove"): "移除",
    ("yingtu::ImageListWidget", "Add Images"): "添加图片",
    ("yingtu::ImageListWidget", "Select All"): "全选",
    ("yingtu::ImageListWidget", "Clear List"): "清空列表",
    ("yingtu::ImageProApp", "Failed to initialize the image processing engine (libvips).\n"
                            "Image loading, conversion, resizing and stitching will not work.\n\n"
                            "Please verify the application was deployed correctly."):
        "图片处理引擎（libvips）初始化失败。\n"
        "图片加载、转换、缩放与拼接功能将无法使用。\n\n"
        "请确认应用程序是否已正确部署。",
    ("yingtu::MainWindow", "ImagePro"): "影图",
    ("yingtu::MainWindow", "About ImagePro"): "关于影图",
    ("yingtu::PdfEngine", "Failed to write PDF: %1"): "写入 PDF 失败：%1",
    ("yingtu::StitchCanvas", "Input %1: %2×%3"): "输入图 %1：%2×%3",
    ("yingtu::StitchCanvas", "Release to add to the canvas"): "释放以加入拼接",
    ("yingtu::StitchCanvas", "Click or drag images here"): "点击或拖入图片到合成图",
    ("yingtu::StitchCanvas", "Rotate Left"): "左旋",
    ("yingtu::StitchCanvas", "Rotate Right"): "右旋",
    ("yingtu::StitchCanvas", "Flip Horizontal"): "水平翻转",
    ("yingtu::StitchCanvas", "Flip Vertical"): "垂直翻转",
    ("yingtu::StitchCanvas", "Remove"): "移除",
    ("yingtu::StitchCanvas", "Info"): "信息",
    ("yingtu::StitchCanvas", "Input %1"): "输入图 %1",
    ("yingtu::StitchCanvas", "No image"): "无图片",
    # 以下条目此前被填成了英文原文（译文 == 源串），中文界面里直接露英文。
    ("yingtu::MainWindow", "Batch: please add images first"): "批处理：请先添加图片",
    ("yingtu::MainWindow", "Batch: please select an output directory"): "批处理：请先选择输出目录",
    ("yingtu::MainWindow", "Batch: failed to create output directory %1"): "批处理：创建输出目录 %1 失败",
    ("yingtu::MainWindow", "Batch: unsupported target tool"): "批处理：不支持的目标工具",
    ("yingtu::PropertyPanel", "Filter"): "滤镜",
    ("yingtu::PropertyPanel", "Filter:"): "滤镜：",
    ("yingtu::PropertyPanel", "Grayscale"): "灰度",
    ("yingtu::PropertyPanel", "Sepia"): "复古",
    ("yingtu::PropertyPanel", "Warm"): "暖色",
    ("yingtu::PropertyPanel", "Cool"): "冷色",
    ("yingtu::PropertyPanel", "High Contrast"): "高对比度",
    ("yingtu::PropertyPanel", "Sharpen"): "锐化",
    ("yingtu::PropertyPanel", "Preview: %1"): "预览：%1",
    ("yingtu::PropertyPanel", "Output directory is empty"): "输出目录为空",
    ("yingtu::PropertyPanel", "Output directory does not exist"): "输出目录不存在",
    ("yingtu::PropertyPanel", "Output directory is not writable"): "输出目录不可写",
    ("yingtu::PropertyPanel", "Output path is too long"): "输出路径过长",
    ("yingtu::PropertyPanel", "File name template is empty"): "文件名模板为空",
    # 工具按钮的图标字形：中文界面用汉字，英文界面用首字母。
    ("yingtu::ToolBarWidget", "Icon: Stitch"): "拼",
    ("yingtu::ToolBarWidget", "Icon: Convert"): "转",
    ("yingtu::ToolBarWidget", "Icon: Compress"): "压",
    ("yingtu::ToolBarWidget", "Icon: Watermark"): "水",
    ("yingtu::ToolBarWidget", "Icon: Edit"): "编",
    ("yingtu::ToolBarWidget", "Icon: Resize"): "尺",
    ("yingtu::ToolBarWidget", "Icon: Batch"): "批",
    ("yingtu::ToolBarWidget", "Icon: PDF"): "P",
    # 工具按钮的标签。lupdate 只能收录 tr() 的字面量，
    # 而语言切换需要英文源串来重新取译文，故源码里两者分开传。
    ("yingtu::ToolBarWidget", "Stitch"): "拼接",
    ("yingtu::ToolBarWidget", "Convert"): "转换",
    ("yingtu::ToolBarWidget", "Compress"): "压缩",
    ("yingtu::ToolBarWidget", "Watermark"): "水印",
    ("yingtu::ToolBarWidget", "Edit"): "编辑",
    ("yingtu::ToolBarWidget", "Resize"): "尺寸",
    ("yingtu::ToolBarWidget", "Batch"): "批处理",
    ("yingtu::ToolBarWidget", "PDF"): "PDF",
}

# 英文版的图标字形：取首字母（Convert 用 V 以区别于 Compress 的 Z）。
EN_OVERRIDES = {
    ("yingtu::ToolBarWidget", "Icon: Stitch"): "S",
    ("yingtu::ToolBarWidget", "Icon: Convert"): "V",
    ("yingtu::ToolBarWidget", "Icon: Compress"): "Z",
    ("yingtu::ToolBarWidget", "Icon: Watermark"): "W",
    ("yingtu::ToolBarWidget", "Icon: Edit"): "E",
    ("yingtu::ToolBarWidget", "Icon: Resize"): "R",
    ("yingtu::ToolBarWidget", "Icon: Batch"): "B",
    ("yingtu::ToolBarWidget", "Icon: PDF"): "P",
}

# 英文版：译文即源串（英语是源语言），图标字形另行指定。
EN = {key: key[1] for key in ZH}
EN.update(EN_OVERRIDES)


def unescape(text):
    return (text.replace("&lt;", "<").replace("&gt;", ">")
                .replace("&quot;", '"').replace("&apos;", "'")
                .replace("&amp;", "&"))


def escape(text):
    return (text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


CONTEXT_RE = re.compile(r"<context>(.*?)</context>", re.S)
NAME_RE = re.compile(r"<name>(.*?)</name>", re.S)
MESSAGE_RE = re.compile(r"(<message>(.*?)</message>)", re.S)
SOURCE_RE = re.compile(r"<source>(.*?)</source>", re.S)
TRANSLATION_RE = re.compile(
    r"<translation(?P<attrs>[^>]*)>(?P<text>.*?)</translation>", re.S)


def fill(path, table):
    with open(path, "r", encoding="utf-8") as handle:
        content = handle.read()

    filled = 0
    inserted = 0
    missing = []

    def fix_context(match):
        nonlocal filled, inserted
        block = match.group(1)
        name_match = NAME_RE.search(block)
        if not name_match:
            return match.group(0)
        context = unescape(name_match.group(1))

        def fix_message(msg_match):
            nonlocal filled
            whole, body = msg_match.group(1), msg_match.group(2)
            src_match = SOURCE_RE.search(body)
            if not src_match:
                return whole
            source = unescape(src_match.group(1))
            translation = table.get((context, source))
            if translation is None:
                # 只报告「确实还没译」的条目；已译好的不该刷屏。
                if 'type="unfinished"' in body:
                    missing.append((context, source))
                return whole
            new_body = TRANSLATION_RE.sub(
                lambda t: "<translation>" + escape(translation) + "</translation>",
                body, count=1)
            if new_body != body:
                filled += 1
            return "<message>" + new_body + "</message>"

        new_block = MESSAGE_RE.sub(fix_message, block)

        # 表里有、文件里没有的条目（例如 lupdate 因源串不是字面量而删掉的），
        # 重新插回该上下文 —— 否则翻译会静默丢失。
        existing = set()
        for m in SOURCE_RE.finditer(new_block):
            existing.add(unescape(m.group(1)))
        additions = []
        for (ctx_name, source), translation in table.items():
            if ctx_name != context or source in existing:
                continue
            additions.append(
                "    <message>\n"
                "        <source>%s</source>\n"
                "        <translation>%s</translation>\n"
                "    </message>\n" % (escape(source), escape(translation)))
        if additions:
            inserted += len(additions)
            new_block = new_block.rstrip() + "\n" + "".join(additions)

        return "<context>" + new_block + "</context>"

    content = CONTEXT_RE.sub(fix_context, content)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(content)

    print(f"{path}: filled {filled}, inserted {inserted}")
    for context, source in missing:
        print(f"  UNMAPPED [{context}] {source!r}")


if __name__ == "__main__":
    fill(sys.argv[1], ZH)
    fill(sys.argv[2], EN)
