#pragma once

#include <QObject>
#include <QString>

#ifdef WITH_VELOPACK
#include <Velopack.hpp>
#include <optional>
#endif

namespace yingtu {

// Velopack 自动更新管理器（单例）。
// - release 构建（WITH_VELOPACK=ON）：从 GitHub Releases 检查/下载/应用更新。
// - dev 构建（WITH_VELOPACK=OFF）：no-op stub，保证主流程一致、菜单禁用。
class VpkUpdateManager : public QObject
{
    Q_OBJECT
public:
    static VpkUpdateManager& instance();

    // 是否编译了 Velopack 集成（release=true，dev=false）。
    bool isVelopackEnabled() const;

    // 当前安装版本号；Velopack 上下文不可用时回退 IMAGEPRO_VERSION。
    QString currentVersion() const;

    // 异步检查 GitHub Releases 是否有新版本（不阻塞 UI）。
    void checkForUpdates();

    // 下载已检查到的更新，并在进程退出时由 Velopack updater 应用并重启。
    void downloadAndApplyOnExit();

signals:
    void updateAvailable(const QString& version);
    void upToDate();
    void checkFailed(const QString& message);
    void downloadProgress(int percent);
    void applyReady();

#ifdef WITH_VELOPACK
private:
    static void progressCallback(void* userData, size_t progress);
    std::optional<Velopack::UpdateInfo> m_pendingUpdate;
#endif

private:
    VpkUpdateManager();
    Q_DISABLE_COPY(VpkUpdateManager)
};

} // namespace yingtu
