// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <functional>
#include <string>

#include <QDir>
#include <QFile>
#include <QPainter>

#include "afterhours/paths.h"
#include "afterhours/trailer_view.h"

#ifdef _WIN32
#include <windows.h>
#include <wrl/client.h>
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#endif
#include "WebView2.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef __MINGW32__
// MinGW resolves __uuidof only for interfaces declared like this.
__CRT_UUID_DECL(ICoreWebView2_3, 0xA0D6DF20, 0x3B92, 0x416D, 0xAA, 0x0C, 0x43, 0x7A, 0x9C, 0x72, 0x78, 0x57)
__CRT_UUID_DECL(ICoreWebView2Controller2, 0xc979903e, 0xd4ca, 0x4228, 0x92, 0xeb, 0x47, 0xee, 0x3f, 0xa9, 0x6e, 0xab)
__CRT_UUID_DECL(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, 0x6c4819f3, 0xc9b7, 0x4260, 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c)
__CRT_UUID_DECL(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, 0x4e8a3389, 0xc9d8, 0x4bd2, 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d)
#endif

using Microsoft::WRL::ComPtr;

namespace {
/// The page is served from this virtual host, so YouTube's player sees an https origin and a
/// referrer (it refuses embeds without them).
constexpr wchar_t VirtualHost[] = L"afterhours.app";

constexpr char TrailerPage[] = R"(<!doctype html>
<html><head><meta charset="utf-8">
<style>html,body{margin:0;height:100%;background:#000;overflow:hidden}
iframe{position:absolute;inset:0;width:100%;height:100%;border:0}</style></head>
<body><script>
const query = new URLSearchParams(location.search);
const id = (query.get('v') || '').replace(/[^A-Za-z0-9_-]/g, '');
const muted = query.get('muted') === '1' ? 1 : 0;
const frame = document.createElement('iframe');
frame.allow = 'autoplay; encrypted-media; picture-in-picture; fullscreen';
frame.referrerPolicy = 'strict-origin-when-cross-origin';
frame.src = 'https://www.youtube-nocookie.com/embed/' + id + '?autoplay=1&mute=' + muted +
    '&controls=' + (muted ? 0 : 1) + '&rel=0&playsinline=1&loop=1&playlist=' + id +
    '&origin=' + encodeURIComponent(location.origin);
document.body.appendChild(frame);
</script></body></html>
)";

using CreateEnvironmentFn = HRESULT(STDAPICALLTYPE*)(
    PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

CreateEnvironmentFn LoadCreateEnvironment() {
    static CreateEnvironmentFn function = [] {
        HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
        return loader ? reinterpret_cast<CreateEnvironmentFn>(GetProcAddress(
                            loader, "CreateCoreWebView2EnvironmentWithOptions"))
                      : nullptr;
    }();
    return function;
}

/// The widget's area in device pixels, as WebView2 wants it.
RECT DeviceBounds(const QWidget* widget) {
    const qreal ratio = widget->devicePixelRatioF();
    return RECT{0, 0, static_cast<LONG>(widget->width() * ratio),
                static_cast<LONG>(widget->height() * ratio)};
}

/// A COM completion handler that calls `Fn` (one interface, one Invoke).
template <typename Interface, typename... Args>
class Handler final : public Interface {
public:
    using Fn = std::function<HRESULT(Args...)>;
    explicit Handler(Fn fn_) : fn{std::move(fn_)} {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (riid == IID_IUnknown || riid == __uuidof(Interface)) {
            *object = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references;
        if (left == 0) {
            delete this;
        }
        return left;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override {
        return fn(args...);
    }

private:
    Fn fn;
    std::atomic<ULONG> references{1};
};
} // Anonymous namespace

struct TrailerViewImpl {
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    bool creating{};
    bool failed{};
};

TrailerView::TrailerView(QWidget* parent) : QWidget{parent}, impl{new TrailerViewImpl} {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
}

TrailerView::~TrailerView() {
    if (impl->controller) {
        impl->controller->Close();
    }
    delete impl;
}

bool TrailerView::IsAvailable() {
    if (!LoadCreateEnvironment()) {
        return false;
    }
    // The Evergreen runtime is installed per machine or per user.
    LPWSTR version = nullptr;
    using GetVersionFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, LPWSTR*);
    static const auto get_version = reinterpret_cast<GetVersionFn>(
        GetProcAddress(GetModuleHandleW(L"WebView2Loader.dll"),
                       "GetAvailableCoreWebView2BrowserVersionString"));
    if (!get_version || FAILED(get_version(nullptr, &version)) || !version) {
        return false;
    }
    CoTaskMemFree(version);
    return true;
}

void TrailerView::EnsureCreated() {
    if (impl->controller || impl->creating || impl->failed) {
        return;
    }
    const CreateEnvironmentFn create = LoadCreateEnvironment();
    if (!create) {
        impl->failed = true;
        emit Failed();
        return;
    }
    impl->creating = true;
    const QString base = AfterHours::Paths::CacheDir();
    const QString data_folder = base + QStringLiteral("/webview2");
    const QString page_folder = base + QStringLiteral("/trailer");
    QDir{}.mkpath(page_folder);
    if (QFile page{page_folder + QStringLiteral("/index.html")};
        page.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        page.write(TrailerPage);
    }
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const std::wstring page_path = QDir::toNativeSeparators(page_folder).toStdWString();

    auto* on_environment =
        new Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT,
                    ICoreWebView2Environment*>(
            [this, hwnd, page_path](HRESULT result, ICoreWebView2Environment* environment) {
                if (FAILED(result) || !environment) {
                    impl->creating = false;
                    impl->failed = true;
                    emit Failed();
                    return S_OK;
                }
                impl->environment = environment;
                auto* on_controller =
                    new Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                                HRESULT, ICoreWebView2Controller*>(
                        [this, page_path](HRESULT controller_result,
                                          ICoreWebView2Controller* controller) {
                            impl->creating = false;
                            if (FAILED(controller_result) || !controller) {
                                impl->failed = true;
                                emit Failed();
                                return S_OK;
                            }
                            impl->controller = controller;
                            controller->get_CoreWebView2(&impl->webview);
                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(impl->webview->get_Settings(&settings))) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                            }
                            ComPtr<ICoreWebView2_3> webview3;
                            if (SUCCEEDED(impl->webview.As(&webview3))) {
                                webview3->SetVirtualHostNameToFolderMapping(
                                    VirtualHost, page_path.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                            }
                            // Dark until the player paints.
                            ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(impl->controller.As(&controller2))) {
                                controller2->put_DefaultBackgroundColor({255, 0, 0, 0});
                            }
                            controller->put_Bounds(DeviceBounds(this));
                            controller->put_IsVisible(isVisible() ? TRUE : FALSE);
                            Navigate();
                            return S_OK;
                        });
                environment->CreateCoreWebView2Controller(hwnd, on_controller);
                on_controller->Release();
                return S_OK;
            });
    const std::wstring data_path = QDir::toNativeSeparators(data_folder).toStdWString();
    if (FAILED(create(nullptr, data_path.c_str(), nullptr, on_environment))) {
        impl->creating = false;
        impl->failed = true;
        emit Failed();
    }
    on_environment->Release();
}

void TrailerView::Navigate() {
    if (!impl->webview) {
        return;
    }
    const QString url = video_id.isEmpty()
                            ? QStringLiteral("about:blank")
                            : QStringLiteral("https://%1/index.html?v=%2&muted=%3")
                                  .arg(QString::fromWCharArray(VirtualHost), video_id,
                                       muted ? QStringLiteral("1") : QStringLiteral("0"));
    impl->webview->Navigate(url.toStdWString().c_str());
}

void TrailerView::Play(const QString& video_id_, bool muted_) {
    video_id = video_id_;
    muted = muted_;
    EnsureCreated();
    Navigate();
}

void TrailerView::Stop() {
    video_id.clear();
    Navigate();
}

void TrailerView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (impl->controller) {
        impl->controller->put_Bounds(DeviceBounds(this));
    }
}

void TrailerView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (impl->controller) {
        impl->controller->put_IsVisible(TRUE);
    }
}

void TrailerView::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (impl->controller) {
        impl->controller->put_IsVisible(FALSE);
    }
}

#else

struct TrailerViewImpl {};

TrailerView::TrailerView(QWidget* parent) : QWidget{parent} {}
TrailerView::~TrailerView() = default;
bool TrailerView::IsAvailable() {
    return false;
}
void TrailerView::EnsureCreated() {}
void TrailerView::Navigate() {}
void TrailerView::Play(const QString& video_id_, bool muted_) {
    video_id = video_id_;
    muted = muted_;
    emit Failed();
}
void TrailerView::Stop() {}
void TrailerView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
}
void TrailerView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
}
void TrailerView::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
}

#endif

void TrailerView::paintEvent(QPaintEvent*) {
    QPainter painter{this};
    painter.fillRect(rect(), Qt::black);
}
