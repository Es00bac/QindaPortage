// SPDX-License-Identifier: GPL-3.0-or-later
#include "ops/dependencyhints.h"

#include "core/repoindex.h"

#include <QJsonObject>
#include <QRegularExpression>

namespace QindaPortage::Ops {

QString DependencyResolution::howName(How h)
{
    switch (h) {
    case Hint: return QStringLiteral("hint");
    case Index: return QStringLiteral("index");
    case Eclass: return QStringLiteral("eclass");
    case Vendored: return QStringLiteral("vendored");
    case Unresolved: return QStringLiteral("unresolved");
    }
    return {};
}

namespace DependencyHints {

namespace {

// Qt 6 module (CMake component) -> package and the qtbase USE flag it needs.
struct QtModule { const char *component; const char *package; const char *use; };
const QtModule qtModules[] = {
    {"Core", "dev-qt/qtbase:6", nullptr}, {"Gui", "dev-qt/qtbase:6", "gui"},
    {"Widgets", "dev-qt/qtbase:6", "widgets"}, {"Network", "dev-qt/qtbase:6", "network"},
    {"Sql", "dev-qt/qtbase:6", "sql"}, {"DBus", "dev-qt/qtbase:6", "dbus"},
    {"OpenGL", "dev-qt/qtbase:6", "opengl"}, {"OpenGLWidgets", "dev-qt/qtbase:6", "opengl"},
    {"Concurrent", "dev-qt/qtbase:6", "concurrent"}, {"Xml", "dev-qt/qtbase:6", "xml"},
    {"PrintSupport", "dev-qt/qtbase:6", "gui"}, {"Test", "dev-qt/qtbase:6", nullptr},
    {"Qml", "dev-qt/qtdeclarative:6", nullptr}, {"Quick", "dev-qt/qtdeclarative:6", nullptr},
    {"QuickControls2", "dev-qt/qtdeclarative:6", nullptr}, {"Svg", "dev-qt/qtsvg:6", nullptr},
    {"SvgWidgets", "dev-qt/qtsvg:6", nullptr}, {"Multimedia", "dev-qt/qtmultimedia:6", nullptr},
    {"WaylandClient", "dev-qt/qtwayland:6", nullptr}, {"LinguistTools", "dev-qt/qttools:6", nullptr},
    {"Tools", "dev-qt/qttools:6", nullptr}, {"WebEngineCore", "dev-qt/qtwebengine:6", nullptr},
    {"WebEngineWidgets", "dev-qt/qtwebengine:6", nullptr}, {"WebEngineQuick", "dev-qt/qtwebengine:6", nullptr},
    {"Core5Compat", "dev-qt/qt5compat:6", nullptr}, {"ShaderTools", "dev-qt/qtshadertools:6", nullptr},
    {"WebSockets", "dev-qt/qtwebsockets:6", nullptr}, {"Positioning", "dev-qt/qtpositioning:6", nullptr},
    {"SerialPort", "dev-qt/qtserialport:6", nullptr}, {"NetworkAuth", "dev-qt/qtnetworkauth:6", nullptr},
};

QStringList qtAtoms(const QStringList &components)
{
    QMap<QString, QStringList> byPackage; // package -> USE flags
    const QStringList wanted = components.isEmpty() ? QStringList{QStringLiteral("Core")} : components;
    for (const QString &c : wanted) {
        for (const QtModule &m : qtModules) {
            if (c.compare(QString::fromLatin1(m.component), Qt::CaseInsensitive) != 0) continue;
            QStringList &flags = byPackage[QString::fromLatin1(m.package)];
            if (m.use && !flags.contains(QString::fromLatin1(m.use))) flags.append(QString::fromLatin1(m.use));
        }
    }
    if (byPackage.isEmpty()) byPackage.insert(QStringLiteral("dev-qt/qtbase:6"), {});
    QStringList out;
    for (auto it = byPackage.cbegin(); it != byPackage.cend(); ++it) {
        out.append(it.value().isEmpty() ? it.key()
                                        : QStringLiteral("%1[%2]").arg(it.key(), it.value().join(QLatin1Char(','))));
    }
    return out;
}

// Build tools the eclass already brings in.
bool providedByEclass(const DeclaredDependency &d)
{
    static const QStringList python = {QStringLiteral("setuptools"), QStringLiteral("wheel"),
                                       QStringLiteral("hatchling"), QStringLiteral("flit_core"),
                                       QStringLiteral("flit-core"), QStringLiteral("poetry-core"),
                                       QStringLiteral("pdm-backend"), QStringLiteral("maturin"),
                                       QStringLiteral("scikit-build-core"), QStringLiteral("meson-python"),
                                       QStringLiteral("setuptools-scm"), QStringLiteral("setuptools_scm")};
    if (d.kind == DeclaredDependency::Python && d.build) return python.contains(d.name.toLower());
    static const QStringList cmake = {QStringLiteral("threads"), QStringLiteral("pkgconfig"),
                                      QStringLiteral("git"), QStringLiteral("gnuinstalldirs")};
    return d.kind == DeclaredDependency::CMakePackage && cmake.contains(d.name.toLower());
}

} // namespace

const QMap<QString, QString> &table()
{
    static const QMap<QString, QString> map = [] {
        QMap<QString, QString> m;
        const char *pairs[][2] = {
            {"zlib", "virtual/zlib:="}, {"openssl", "dev-libs/openssl:="}, {"libssl", "dev-libs/openssl:="},
            {"libcrypto", "dev-libs/openssl:="}, {"curl", "net-misc/curl"}, {"libcurl", "net-misc/curl"},
            {"sdl2", "media-libs/libsdl2"}, {"sdl3", "media-libs/libsdl3"}, {"glfw3", "media-libs/glfw"},
            {"glfw", "media-libs/glfw"}, {"vulkan", "media-libs/vulkan-loader"},
            {"vulkanheaders", "dev-util/vulkan-headers"}, {"x11", "x11-libs/libX11"},
            {"xrandr", "x11-libs/libXrandr"}, {"xi", "x11-libs/libXi"}, {"xcursor", "x11-libs/libXcursor"},
            {"xinerama", "x11-libs/libXinerama"}, {"xcb", "x11-libs/libxcb:="},
            {"xkbcommon", "x11-libs/libxkbcommon"}, {"wayland-client", "dev-libs/wayland"},
            {"wayland", "dev-libs/wayland"}, {"wayland-protocols", "dev-libs/wayland-protocols"},
            {"gtk+-3.0", "x11-libs/gtk+:3"}, {"gtk3", "x11-libs/gtk+:3"}, {"gtk4", "gui-libs/gtk:4"},
            {"libadwaita-1", "gui-libs/libadwaita:1"}, {"png", "media-libs/libpng:="},
            {"libpng", "media-libs/libpng:="}, {"jpeg", "media-libs/libjpeg-turbo:="},
            {"libjpeg", "media-libs/libjpeg-turbo:="}, {"freetype", "media-libs/freetype"},
            {"freetype2", "media-libs/freetype"}, {"fontconfig", "media-libs/fontconfig"},
            {"sqlite3", "dev-db/sqlite:3"}, {"sqlite", "dev-db/sqlite:3"}, {"boost", "dev-libs/boost:="},
            {"fmt", "dev-libs/libfmt:="}, {"spdlog", "dev-libs/spdlog:="}, {"nlohmann_json", "dev-cpp/nlohmann_json"},
            {"yaml-cpp", "dev-cpp/yaml-cpp:="}, {"libusb-1.0", "dev-libs/libusb:1"}, {"libusb", "dev-libs/libusb:1"},
            {"libpipewire-0.3", "media-video/pipewire:="}, {"pipewire", "media-video/pipewire:="},
            {"alsa", "media-libs/alsa-lib"}, {"asound", "media-libs/alsa-lib"}, {"libpulse", "media-libs/libpulse"},
            {"pulseaudio", "media-libs/libpulse"}, {"libavcodec", "media-video/ffmpeg:="},
            {"libavformat", "media-video/ffmpeg:="}, {"libavutil", "media-video/ffmpeg:="},
            {"libswscale", "media-video/ffmpeg:="}, {"libswresample", "media-video/ffmpeg:="},
            {"ffmpeg", "media-video/ffmpeg:="}, {"opencv", "media-libs/opencv:="}, {"opencv4", "media-libs/opencv:="},
            {"eigen3", "dev-cpp/eigen:3"}, {"gtest", "dev-cpp/gtest"}, {"googletest", "dev-cpp/gtest"},
            {"python3", "dev-lang/python:3"}, {"python", "dev-lang/python:3"}, {"glib-2.0", "dev-libs/glib:2"},
            {"gio-2.0", "dev-libs/glib:2"}, {"gobject-2.0", "dev-libs/glib:2"}, {"cairo", "x11-libs/cairo"},
            {"pango", "x11-libs/pango"}, {"harfbuzz", "media-libs/harfbuzz:="}, {"libxml-2.0", "dev-libs/libxml2:="},
            {"libxml2", "dev-libs/libxml2:="}, {"expat", "dev-libs/expat"}, {"json-c", "dev-libs/json-c:="},
            {"jsoncpp", "dev-libs/jsoncpp:="}, {"libevent", "dev-libs/libevent:="}, {"libuv", "dev-libs/libuv:="},
            {"libssh2", "net-libs/libssh2"}, {"dbus-1", "sys-apps/dbus"}, {"dbus", "sys-apps/dbus"},
            {"libsystemd", "sys-apps/systemd:="}, {"libudev", "virtual/libudev:="}, {"libinput", "dev-libs/libinput:="},
            {"gl", "virtual/opengl"}, {"opengl", "virtual/opengl"}, {"egl", "media-libs/libglvnd"},
            {"glesv2", "media-libs/libglvnd"}, {"epoxy", "media-libs/libepoxy"}, {"libdrm", "x11-libs/libdrm"},
            {"protobuf", "dev-libs/protobuf:="}, {"icu", "dev-libs/icu:="}, {"icu-uc", "dev-libs/icu:="},
            {"libzstd", "app-arch/zstd:="}, {"zstd", "app-arch/zstd:="}, {"liblzma", "app-arch/xz-utils"},
            {"lzma", "app-arch/xz-utils"}, {"bzip2", "app-arch/bzip2:="}, {"liblz4", "app-arch/lz4:="},
            {"libpcre2-8", "dev-libs/libpcre2:="}, {"pcre2", "dev-libs/libpcre2:="}, {"ncurses", "sys-libs/ncurses:="},
            {"ncursesw", "sys-libs/ncurses:="}, {"curses", "sys-libs/ncurses:="}, {"readline", "sys-libs/readline:="},
            {"libgit2", "dev-libs/libgit2:="}, {"ogg", "media-libs/libogg"}, {"vorbis", "media-libs/libvorbis"},
            {"opus", "media-libs/opus"}, {"flac", "media-libs/flac:="}, {"sndfile", "media-libs/libsndfile"},
            {"libsodium", "dev-libs/libsodium:="}, {"gnutls", "net-libs/gnutls:="}, {"tinyxml2", "dev-libs/tinyxml2:="},
            {"pugixml", "dev-libs/pugixml"}, {"libwebp", "media-libs/libwebp:="}, {"tiff", "media-libs/tiff:="},
            {"libtiff-4", "media-libs/tiff:="}, {"cli11", "dev-cpp/cli11"}, {"catch2", "dev-cpp/catch"},
            {"absl", "dev-cpp/abseil-cpp:="}, {"re2", "dev-libs/re2:="}, {"ecm", "kde-frameworks/extra-cmake-modules"},
            {"portaudio-2.0", "media-libs/portaudio"}, {"openal", "media-libs/openal"}, {"libzip", "dev-libs/libzip:="},
            {"hidapi", "dev-libs/hidapi"}, {"libcap", "sys-libs/libcap"}, {"libffi", "dev-libs/libffi:="},
            {"doxygen", "app-text/doxygen"}, {"pkg-config", "virtual/pkgconfig"}, {"ninja", "dev-build/ninja"},
            {"z", "virtual/zlib:="}, {"onig", "dev-libs/oniguruma:="}, {"pixman-1", "x11-libs/pixman"},
            {"wayland-cursor", "dev-libs/wayland"}, {"wayland-server", "dev-libs/wayland"},
            {"wayland-egl", "dev-libs/wayland"}, {"wayland-scanner", "dev-util/wayland-scanner"},
            {"systemd", "sys-apps/systemd:="}, {"scdoc", "app-text/scdoc"},
            {"epoll-shim", ""}, {"oniguruma", "dev-libs/oniguruma:="}, {"m", ""}, {"dl", ""}, {"pthread", ""}, {"rt", ""},
            // Cargo crates that link a system library.
            {"openssl-sys", "dev-libs/openssl:="}, {"libgit2-sys", "dev-libs/libgit2:="},
            {"zstd-sys", "app-arch/zstd:="}, {"libz-sys", "virtual/zlib:="}, {"libsqlite3-sys", "dev-db/sqlite:3"},
            {"curl-sys", "net-misc/curl"}, {"libssh2-sys", "net-libs/libssh2"}, {"onig_sys", "dev-libs/oniguruma:="},
            {"alsa-sys", "media-libs/alsa-lib"}, {"libdbus-sys", "sys-apps/dbus"}, {"libudev-sys", "virtual/libudev:="},
            {"freetype-sys", "media-libs/freetype"}, {"servo-fontconfig-sys", "media-libs/fontconfig"},
            {"wayland-sys", "dev-libs/wayland"}, {"libpulse-sys", "media-libs/libpulse"}};
        for (const auto &p : pairs) m.insert(QString::fromLatin1(p[0]), QString::fromLatin1(p[1]));
        return m;
    }();
    return map;
}

QVector<DependencyResolution> resolve(const QVector<DeclaredDependency> &deps, const Core::RepoIndex *index)
{
    QVector<DependencyResolution> out;
    for (const DeclaredDependency &d : deps) {
        DependencyResolution r;
        r.dep = d;
        const QString lower = d.name.toLower();
        if (providedByEclass(d)) {
            r.how = DependencyResolution::Eclass;
            r.note = QStringLiteral("provided by the eclass");
        } else if (d.kind == DeclaredDependency::CMakePackage && (lower == QLatin1String("qt6") || lower.startsWith(QLatin1String("qt6")))) {
            QStringList components = d.components;
            if (components.isEmpty() && lower.size() > 3) components.append(d.name.mid(3));
            r.atoms = qtAtoms(components);
            r.how = DependencyResolution::Hint;
        } else if (auto it = table().constFind(lower); it != table().cend()) {
            r.how = DependencyResolution::Hint;
            if (!it.value().isEmpty()) r.atoms.append(it.value());
            else r.note = QStringLiteral("no package needed on Linux (libc or not applicable)");
        } else if (d.kind == DeclaredDependency::Cargo || d.kind == DeclaredDependency::Go) {
            r.how = DependencyResolution::Vendored;
            r.note = d.kind == DeclaredDependency::Cargo ? QStringLiteral("crate, fetched through CRATES")
                                                         : QStringLiteral("Go module, vendored");
        } else if (index) {
            QStringList candidates;
            if (d.kind == DeclaredDependency::Python) {
                QString pn = lower;
                pn.replace(QLatin1Char('_'), QLatin1Char('-')).replace(QLatin1Char('.'), QLatin1Char('-'));
                candidates << QStringLiteral("dev-python/") + pn;
            } else {
                // pkg-config / CMake name: an exact package name anywhere,
                // also without a pkg-config API suffix ("pixman-1", "gtk-4.0").
                QString base = lower;
                static const QRegularExpression apiSuffix(QStringLiteral("-\\d+(\\.\\d+)?$"));
                QString bare = base;
                bare.remove(apiSuffix);
                QStringList names{QStringLiteral("lib") + base, base};
                if (bare != base) names << QStringLiteral("lib") + bare << bare;
                for (const QString &p : names) {
                    for (const Core::AvailablePackage &pkg : index->packages()) {
                        if (pkg.name.toLower() == p) candidates << pkg.key;
                    }
                }
            }
            for (const QString &key : candidates) {
                if (index->find(key)) {
                    QString atom = key;
                    if (d.kind == DeclaredDependency::Python) atom += QStringLiteral("[${PYTHON_USEDEP}]");
                    r.atoms.append(atom);
                    r.how = DependencyResolution::Index;
                    break;
                }
            }
            if (r.atoms.isEmpty()) r.note = QStringLiteral("no package found; ask the assistant");
        } else {
            r.note = QStringLiteral("not in the hint table");
        }
        out.append(r);
    }
    return out;
}

QMap<QString, QString> atomMap(const QVector<DependencyResolution> &resolutions)
{
    QMap<QString, QString> out;
    for (const DependencyResolution &r : resolutions) {
        if (!r.atoms.isEmpty()) out.insert(r.dep.name, r.atoms.join(QLatin1Char(' ')));
    }
    return out;
}

QJsonArray toJson(const QVector<DependencyResolution> &resolutions)
{
    QJsonArray out;
    for (const DependencyResolution &r : resolutions) {
        QJsonObject o = r.dep.toJson();
        o[QStringLiteral("resolvedBy")] = DependencyResolution::howName(r.how);
        if (!r.atoms.isEmpty()) o[QStringLiteral("atoms")] = QJsonArray::fromStringList(r.atoms);
        if (!r.note.isEmpty()) o[QStringLiteral("note")] = r.note;
        out.append(o);
    }
    return out;
}

} // namespace DependencyHints

} // namespace QindaPortage::Ops
