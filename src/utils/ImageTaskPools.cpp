#include "ImageTaskPools.h"

#include <QThread>
#include <QThreadPool>

namespace yingtu {

namespace {

// 缩略图：单张解码本身在 libvips 内部还有核数/2 的并行，外层 2-4 个
// worker 足以喂饱 IO 与解码；再多只会放大线程乘积。
int thumbnailThreadCount()
{
    const int cores = QThread::idealThreadCount();
    return qBound(2, qMax(1, cores / 2), 4);
}

QThreadPool* makePool(int threadCount)
{
    auto* pool = new QThreadPool();
    // 无父对象的进程级单例：随进程退出回收（与 libvips 不做 vips_shutdown
    // 的理由一致——静态析构顺序不可控）。
    pool->setMaxThreadCount(threadCount);
    return pool;
}

} // namespace

namespace ImageTaskPools {

QThreadPool* thumbnailPool()
{
    static QThreadPool* pool = makePool(thumbnailThreadCount());
    return pool;
}

QThreadPool* previewPool()
{
    static QThreadPool* pool = makePool(1);
    return pool;
}

QThreadPool* enginePool()
{
    static QThreadPool* pool = makePool(1);
    return pool;
}

int batchFileConcurrency()
{
    const int cores = QThread::idealThreadCount();
    return qBound(2, qMax(1, cores / 2), 4);
}

} // namespace ImageTaskPools

} // namespace yingtu
