#include "VpkUpdateManager.h"

#include <QMetaObject>
#include <QString>
#include <QtConcurrent>
#include <QFutureWatcher>

#include <exception>
#include <utility>

#ifndef IMAGEPRO_VERSION
#define IMAGEPRO_VERSION "0.0.0-unknown"
#endif

namespace yingtu {

// Velopack 从该 URL 拉取 releases.<platform>.json（由 vpk pack 上传到 GitHub Releases）。
// 传入 GitHub 仓库地址时，Velopack 会自动解析 Releases 资产。
static constexpr const char* kUpdateUrl = "https://github.com/Hermitweb/ImagePro";

VpkUpdateManager& VpkUpdateManager::instance()
{
    static VpkUpdateManager s_instance;
    return s_instance;
}

VpkUpdateManager::VpkUpdateManager() = default;

bool VpkUpdateManager::isVelopackEnabled() const
{
#ifdef WITH_VELOPACK
    return true;
#else
    return false;
#endif
}

QString VpkUpdateManager::currentVersion() const
{
    // 单点版本号由 CMake project(VERSION) 注入，about / 更新检查 / vpk pack 共用。
    return QString::fromLatin1(IMAGEPRO_VERSION);
}

#ifdef WITH_VELOPACK

// 工作线程返回的检查结果（不跨线程抛异常）。
struct CheckResult {
    bool ok = false;
    std::optional<Velopack::UpdateInfo> update;
    QString error;
};

// 工作线程返回的下载结果。
struct DownloadResult {
    bool ok = false;
    QString error;
};

void VpkUpdateManager::progressCallback(void* userData, size_t progress)
{
    // 由 Velopack 下载线程调用；marshal 回主线程发信号，避免 UI 线程问题。
    auto* self = static_cast<VpkUpdateManager*>(userData);
    if (!self)
        return;
    const int percent = static_cast<int>(progress);
    QMetaObject::invokeMethod(self, [self, percent]() {
        emit self->downloadProgress(percent);
    }, Qt::QueuedConnection);
}

void VpkUpdateManager::checkForUpdates()
{
    auto* watcher = new QFutureWatcher<CheckResult>(this);
    connect(watcher, &QFutureWatcher<CheckResult>::finished, this, [this, watcher]() {
        const CheckResult r = watcher->result();
        if (r.ok) {
            if (r.update) {
                m_pendingUpdate = r.update;
                const QString version =
                    QString::fromStdString(r.update->TargetFullRelease.Version);
                emit updateAvailable(version);
            } else {
                emit upToDate();
            }
        } else {
            emit checkFailed(r.error);
        }
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([]() -> CheckResult {
        CheckResult r;
        try {
            Velopack::UpdateManager mgr(kUpdateUrl);
            r.update = mgr.CheckForUpdates();
            r.ok = true;
        } catch (const std::exception& e) {
            r.ok = false;
            r.error = QString::fromLocal8Bit(e.what());
        } catch (...) {
            r.ok = false;
            r.error = VpkUpdateManager::tr("Unknown error while checking for updates.");
        }
        return r;
    }));
}

void VpkUpdateManager::downloadAndApplyOnExit()
{
    if (!m_pendingUpdate) {
        emit checkFailed(tr("No pending update to download."));
        return;
    }

    const auto info = *m_pendingUpdate;
    auto* watcher = new QFutureWatcher<DownloadResult>(this);
    connect(watcher, &QFutureWatcher<DownloadResult>::finished, this, [this, watcher, info]() {
        const DownloadResult r = watcher->result();
        if (!r.ok) {
            emit checkFailed(r.error);
            watcher->deleteLater();
            return;
        }
        // 下载完成后，告知 updater 等待本进程退出后应用并重启。
        try {
            Velopack::UpdateManager mgr(kUpdateUrl);
            mgr.WaitExitThenApplyUpdates(info);
            emit applyReady();
        } catch (const std::exception& e) {
            emit checkFailed(QString::fromLocal8Bit(e.what()));
        } catch (...) {
            emit checkFailed(tr("Unknown error while preparing update."));
        }
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([this, info]() -> DownloadResult {
        DownloadResult r;
        try {
            Velopack::UpdateManager mgr(kUpdateUrl);
            mgr.DownloadUpdates(info, &VpkUpdateManager::progressCallback, this);
            r.ok = true;
        } catch (const std::exception& e) {
            r.ok = false;
            r.error = QString::fromLocal8Bit(e.what());
        } catch (...) {
            r.ok = false;
            r.error = tr("Unknown error while downloading update.");
        }
        return r;
    }));
}

#else // !WITH_VELOPACK: no-op stub，保证 dev 构建链接干净、主流程一致。

void VpkUpdateManager::checkForUpdates()
{
    // dev 构建无 Velopack 集成；UI 应通过 isVelopackEnabled() 先行拦截。
}

void VpkUpdateManager::downloadAndApplyOnExit()
{
    // no-op
}

#endif // WITH_VELOPACK

} // namespace yingtu
