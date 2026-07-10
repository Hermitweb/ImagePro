#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import xml.etree.ElementTree as ET
from pathlib import Path

TRANSLATIONS = {
    # MainWindow menus
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
    "Stitch": "拼接",
    "Convert": "转换",
    "Compress": "压缩",
    "Watermark": "水印",
    "Edit": "编辑",
    "Resize": "尺寸",
    "Batch": "批处理",
    "PDF": "PDF",
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
    "Delete Current Image": "删除当前图片",
    "Rotate Current Left": "当前图片向左旋转",
    "Rotate Current Right": "当前图片向右旋转",
    "Copy Image": "复制图片",
    "Save Image": "保存图片",
    "Fit to Window": "适应窗口",
    "Image Info": "图片信息",
    "File: %1\nSize: %2x%3": "文件：%1\n尺寸：%2x%3",
    "Save PDF": "保存 PDF",
    "PDF Files (*.pdf)": "PDF 文件 (*.pdf)",
    "Exporting PDF...": "正在导出 PDF...",
    "Exported PDF: %1": "已导出 PDF：%1",
    "PDF Export Complete": "PDF 导出完成",
    "Successfully exported %1 images to PDF": "成功导出 %1 张图片到 PDF",

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
    "Add Custom Preset": "添加自定义预设",

    # Validation / preview
    "Preview: %1": "预览：%1",
    "At least 2 images are required": "至少需要 2 张图片",
    "Output directory is empty": "输出目录为空",
    "Output directory does not exist": "输出目录不存在",
    "Output directory is not writable": "输出目录不可写",
    "No images selected": "未选择图片",
    "Selected %1 image(s)": "已选择 %1 张图片",

    # Convert panel
    "<b>Convert Settings</b>": "<b>转换设置</b>",
    "Target Format:": "目标格式：",
    "Keep EXIF": "保留 EXIF",
    "Convert to sRGB": "转换为 sRGB",

    # Compress panel
    "<b>Compress Settings</b>": "<b>压缩设置</b>",
    "Mode:": "模式：",
    "Quality": "质量",
    "Size": "大小",
    "Smart": "智能",
    "Strength:": "强度：",
    "Scale:": "缩放：",
    "Target Size:": "目标大小：",
    "MB": "MB",
    "KB": "KB",
    "Show Original": "显示原图",

    # Watermark panel
    "<b>Watermark Settings</b>": "<b>水印设置</b>",
    "Type:": "类型：",
    "Text": "文字",
    "Image": "图片",
    "Text:": "文字：",
    "Image:": "图片：",
    "Select Watermark Image": "选择水印图片",
    "Font Family:": "字体：",
    "Font Size:": "字号：",
    "Color:": "颜色：",
    "Opacity:": "不透明度：",
    "Rotation:": "旋转：",
    "Position:": "位置：",
    "Top Left": "左上",
    "Top Center": "中上",
    "Top Right": "右上",
    "Center Left": "左中",
    "Center": "居中",
    "Center Right": "右中",
    "Bottom Left": "左下",
    "Bottom Center": "中下",
    "Bottom Right": "右下",
    "Tile": "平铺",
    "Tile Spacing:": "平铺间距：",
    "Margin:": "边距：",
    "Output Format:": "输出格式：",
    "Original": "原格式",
    "Select Output Directory": "选择输出目录",

    # Edit panel
    "<b>Edit Tools</b>": "<b>编辑工具</b>",
    "Tool:": "工具：",
    "Rectangle": "矩形",
    "Ellipse": "椭圆",
    "Arrow": "箭头",
    "Pen": "画笔",
    "Mosaic": "马赛克",
    "Crop": "裁剪",
    "Filter": "滤镜",
    "Filter:": "滤镜：",
    "Grayscale": "灰度",
    "Sepia": "复古",
    "Warm": "暖色",
    "Cool": "冷色",
    "High Contrast": "高对比",
    "Blur": "模糊",
    "Sharpen": "锐化",
    "Red": "红色",
    "Blue": "蓝色",
    "Green": "绿色",
    "Yellow": "黄色",
    "Black": "黑色",
    "Color:": "颜色：",
    "Line Width:": "线宽：",
    "Fill Style:": "填充样式：",
    "No Fill": "无填充",
    "Semi Fill": "半透明填充",
    "Solid Fill": "实心填充",
    "Undo": "撤销",
    "Redo": "重做",

    # Resize panel
    "<b>Resize Settings</b>": "<b>尺寸设置</b>",
    "Mode:": "模式：",
    "Percentage": "百分比",
    "Pixel": "像素",
    "Preset": "预设",
    "Percentage:": "百分比：",
    "Size:": "尺寸：",
    "x": "x",
    "Lock aspect ratio": "锁定比例",
    "Interpolation:": "插值：",
    "Nearest": "最近邻",
    "Bilinear": "双线性",
    "Bicubic": "双三次",
    "Lanczos": "Lanczos",
    "Do not exceed original size": "不超过原图尺寸",
    "Add": "添加",
    "Delete": "删除",

    # Batch panel
    "<b>Batch Processing</b>": "<b>批量处理</b>",
    "Target Tool:": "目标工具：",

    # PDF panel
    "<b>PDF Export</b>": "<b>PDF 导出</b>",
    "Page Size:": "页面大小：",
    "A4": "A4",
    "A5": "A5",
    "Letter": "Letter",
    "Layout:": "布局：",
    "Single Per Page": "每页单张",
    "Fit to Page": "适应页面",
    "Grid 2x2": "网格 2x2",
    "Grid 3x3": "网格 3x3",
    "Resolution:": "分辨率：",
    "Margins:": "边距：",
    "Output:": "输出：",

    # Dialogs / other
    "Select Output Directory": "选择输出目录",
    "Error": "错误",
    "Failed to create output directory.": "创建输出目录失败。",
    "e.g. {name}_{index}": "例如：{name}_{index}",
    " Ready": " 就绪",
    " images": " 张图片",
    " image": " 张图片",
    "0 张图片": "0 张图片",
    "%1 images, %2 selected": "%1 张图片，已选择 %2 张",
    "Drop images here or click to add": "拖拽图片到此处或点击添加",

    # Status / labels
    "Ready": "就绪",
    "Processing": "处理中",
    "Error": "错误",
    "Warning": "警告",
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
            # 如果原文已经是中文且没有翻译，则直接复制原文
            if not translation.text and not text.strip().isascii() and text not in TRANSLATIONS:
                translation.text = text
                if translation.get("type") == "unfinished":
                    del translation.attrib["type"]
                updated += 1
                continue
            if text in TRANSLATIONS:
                if translation.get("type") == "unfinished":
                    del translation.attrib["type"]
                translation.text = TRANSLATIONS[text]
                updated += 1

    tree.write(ts_path, encoding="utf-8", xml_declaration=True)
    print(f"Updated {updated} translations in {ts_path}")

if __name__ == "__main__":
    main()
