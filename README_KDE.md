# Snow Apps for KDE

[中文说明](#中文说明)

This fork has experimental Snow Shot and Snow Image Viewer builds for CachyOS
x86-64 with KDE Plasma on Wayland. It is not a packaged Linux release.

## Install

On a machine where the project has already been built, create user-level KDE
application menu entries with:

```sh
scripts/install-linux-kde.sh
```

The entries are named **Snow Shot (KDE)** and **Snow Image Viewer (KDE)**. They
use the executables in this checkout's `build/` directory, so keep the project
and build directories in place. This does not install system packages or set a
default image application. To preview the files it would create, pass `--dry-run`.

For a fresh checkout, install the development prerequisites described in the
[Linux/KDE build guide](docs-linux-kde.md), then run:

```sh
mise install rust@1.97.1 rust@nightly-2026-09-08 python@3.14
scripts/build-linux-kde.sh
scripts/install-linux-kde.sh
```

The first build initializes the OCR submodule and installs the repository-local
vcpkg dependencies. It can take a long time. These are Debug builds.

## Run and update

Launch either application from the KDE application menu, or run:

```sh
scripts/run-snow-shot-linux.sh --show-main-window
scripts/run-snow-image-viewer-linux.sh /path/to/image.png
```

GitHub Actions synchronizes the fork with upstream on a schedule. It does not
update this checkout or rebuild installed executables. After saving local work,
update and rebuild with:

```sh
git pull --ff-only
git submodule update --init --recursive
scripts/build-linux-kde.sh
```

The menu entries continue to work after rebuilding in the same directory.
Snow Shot's **Launch at login** setting registers the current build with KDE's
user-level autostart. Disabling that entry in KDE is respected.
See the [port status and limitations](docs-linux-kde.md) before relying on it.
The real screenshot portal may request permission; multi-display capture,
screen recording and several platform-specific features are not yet supported.

### Import settings from Windows

Export the settings archive in Windows, then choose **Export & storage → Data
storage → Import configuration** in the Linux app. Linux keeps its own screenshot
and recording directories, manual save history, skin image and custom tray icon
paths, save path shortcuts, startup options, and process priority. The import
result lists these settings. Other compatible preferences are imported normally.

Global mouse gestures are not yet supported on Linux; keyboard shortcuts are
available through KDE's global shortcut service.

## 中文说明

本分支为 CachyOS x86-64、KDE Plasma 和 Wayland 提供实验性的 Snow Shot 与
Snow Image Viewer 构建，目前还不是正式的 Linux 安装包。

### 安装

如果项目已经编译，在项目根目录运行：

```sh
scripts/install-linux-kde.sh
```

安装后可在 KDE 应用菜单中找到 **Snow Shot (KDE)** 和 **Snow Image Viewer (KDE)**。
菜单入口直接使用当前项目 `build/` 目录里的程序，请保留项目和构建目录；它不会安装
系统软件包，也不会更改默认图片程序。加上 `--dry-run` 可以先查看将要创建的文件。

如果是全新克隆，请先按 [Linux/KDE 构建说明](docs-linux-kde.md) 准备开发依赖，
然后在项目根目录运行：

```sh
mise install rust@1.97.1 rust@nightly-2026-09-08 python@3.14
scripts/build-linux-kde.sh
scripts/install-linux-kde.sh
```

首次构建会初始化 OCR 子模块并安装项目本地的 vcpkg 依赖，可能需要较长时间；
当前生成的是 Debug 构建。

### 启动和更新

可以从 KDE 应用菜单启动，也可以在项目根目录执行：

```sh
scripts/run-snow-shot-linux.sh --show-main-window
scripts/run-snow-image-viewer-linux.sh /path/to/image.png
```

GitHub Actions 会定时将上游同步到 fork，但不会自动更新本地目录或重新编译程序。
保存好本地工作后，执行：

```sh
git pull --ff-only
git submodule update --init --recursive
scripts/build-linux-kde.sh
```

在同一目录重新构建后，菜单入口仍可使用。Snow Shot 的**登录时启动**设置会将当前
构建注册到 KDE 用户级自启项；在 KDE 中禁用该自启项后，程序不会自动重新启用。
首次截图时桌面门户可能请求授权；
多显示器截图、录屏和部分平台功能尚未支持，详情见 [移植状态与限制](docs-linux-kde.md)。

### 从 Windows 导入设置

在 Windows 版中导出配置压缩包，然后在 Linux 版中选择 **导出与存储 → 数据存储 →
导入配置**。截图和录屏保存目录、上次手动保存目录、皮肤和自定义托盘图标路径、保存路径快捷方式，
以及开机启动、管理员启动和进程优先级会保留本机设置；导入完成后会显示这些项目。
其他兼容的设置会正常导入。

Linux 版目前尚不支持全局鼠标手势；键盘快捷键可通过 KDE 全局快捷键服务使用。
