#pragma once

class QThreadPool;

namespace yingtu {

/**
 * 图像任务的专用线程池：按任务类别隔离，避免互相饿死。
 *
 * 此前所有异步工作都挤在 QtConcurrent 的全局池上：批量加载 100 张图的
 * 缩略图任务会占满全部池线程，而每个 worker 内部的 libvips 又有自己的
 * 线程池（核数/2）——结果是缩略图批次独占资源期间，预览生成、编辑器
 * 加载、批处理引擎全部排队，用户点哪儿都没反应。
 *
 * 拆成三个池后互不抢占：
 * - thumbnailPool：列表缩略图批量解码。并发刻意压低（libvips 内部还有
 *   核数/2 的线程），总线程数受控，避免「N 个 Qt 线程 × N/2 个 vips 线程」
 *   的乘积超订。
 * - previewPool：拼接/工具预览与编辑器单图加载。单线程串行 + 上层的
 *   代数检查，过期的预览任务不再排队堆积。
 * - enginePool：批处理引擎的外层驱动。文件级并行由引擎内部自行控制。
 */
namespace ImageTaskPools {

QThreadPool* thumbnailPool();
QThreadPool* previewPool();
QThreadPool* enginePool();

/**
 * 批处理引擎的文件级并发度：同一时刻处理多少个文件。
 * 2-4 个文件 × libvips 内部核数/2 线程 —— 足以吃满 IO 与 CPU，
 * 又不会让内存峰值（单文件解码级别 × 并发数）失控。
 */
int batchFileConcurrency();

} // namespace ImageTaskPools

} // namespace yingtu
