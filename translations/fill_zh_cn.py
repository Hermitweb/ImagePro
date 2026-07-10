#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import xml.etree.ElementTree as ET
from pathlib import Path

TRANSLATIONS = {
    # MainWindow menus & toolbar
    "&File": "文件(&F)",
    "&Add Images": "添加图片(&A)",
    "E&xit": "退出(&X)",
    "&Edit": "编辑(&E)",
    "&Undo": "撤销(&U)",
    "&Redo": "重做(&R)",
    "&Tools": "工具(&T)",
    "&Stitch": "拼接(&S)",
    "&Convert": "转换(&C)",
    "&Compress": "压缩(&M)",
    "&Watermark": "水印(&W)",
    "&Resize": "尺寸(&I)",
    "&Batch": "批处理(&B)",
    "&View": "视图(&V)",
    "Toggle &Theme": "切换主题(&H)",
    "&Language": "语言(&L)",
    "&Help": "帮助(&H)",
    "&About": "关于(&A)",
    "About 影图 ImagePro": "关于 影图 ImagePro",
    "Language Changed": "语言已更改",
    "The language setting has been changed. Please restart the application to apply.": "语言设置已更改，请重启应用生效。",

    # Toolbar / common actions
    "Add Images": "添加图片",
    "Remove": "移除",
    "Clear": "清空",
    "Stitch (F1)": "拼接(F1)",
    "Convert (F2)": "转换(F2)",
    "Compress (F3)": "压缩(F3)",
    "Watermark (F4)": "水印(F4)",
    "Edit (F5)": "编辑(F5)",
    "Resize (F6)": "尺寸(F6)",
    "Batch (F7)": "批处理(F7)",
    "PDF (F8)": "PDF(F8)",
    "Zoom In": "放大",
    "Zoom Out": "缩小",
    "Fit": "适应",
    "100%": "100%",
    "Original Size": "原始尺寸",
    "Rotate Left": "向左旋转",
    "Rotate Right": "向右旋转",
    "Flip Horizontal": "水平翻转",
    "Flip Vertical": "垂直翻转",
    "Delete": "删除",

    # Property panel - Stitch
    "Stitch Settings": "拼接设置",
    "Grid Presets": "网格预设",
    "Output Settings": "输出设置",
    "Direction:": "方向：",
    "Vertical": "纵向",
    "Horizontal": "横向",
    "Grid": "九宫格",
    "Spacing:": "间距：",
    "Background:": "背景：",
    "Transparent": "透明",
    "White": "白色",
    "Custom": "自定义",
    "BG Color:": "背景颜色：",
    "Uniform Width": "统一宽度",
    "Remove White Edges": "移除白边",
    "Auto Crop Edges": "自动裁边",
    "Grid:": "网格：",
    "Format:": "格式：",
    "Quality:": "质量：",
    "Category:": "分类：",
    "Preset:": "预设：",
    "+ Add Preset": "+ 添加预设",
    "All": "全部",
    "Social": "社交分享",
    "ID Photo": "证件照片",
    "Output Dir:": "输出目录：",
    "Browse...": "浏览...",
    "Create": "创建",
    "File Name:": "文件名：",
    "Preview": "预览",
    "Start": "开始",

    # Validation / preview
    "Preview: %1": "预览：%1",
    "At least 2 images are required": "至少需要 2 张图片",
    "Output directory is empty": "输出目录为空",
    "Output directory does not exist": "输出目录不存在",
    "Output directory is not writable": "输出目录不可写",

    # Other panels
    "Target Format:": "目标格式：",
    "Output Format:": "输出格式：",
    "Tile Spacing:": "平铺间距：",
    "Add Custom Preset": "添加自定义预设",
}

def main():
    ts_path = Path(__file__).with_name("ImagePro_zh_CN.ts")
    tree = ET.parse(ts_path)
    root = tree.getroot()

    updated = 0
    for context in root.findall("context"):
        for message in context.findall("message"):
            source = message.find("source")
            translation = message.find("translation")
            if source is None or translation is None:
                continue
            text = source.text or ""
            if text in TRANSLATIONS:
                # Preserve @type="unfinished" handling: remove unfinished attr
                if translation.get("type") == "unfinished":
                    del translation.attrib["type"]
                translation.text = TRANSLATIONS[text]
                updated += 1

    tree.write(ts_path, encoding="utf-8", xml_declaration=True)
    print(f"Updated {updated} translations in {ts_path}")

if __name__ == "__main__":
    main()
