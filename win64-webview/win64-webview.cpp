#include <windows.h>
#include <WebView2.h>
#include <comdef.h>
#include <string>
#include <vector>
#include <functional>
#include <propsys.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <wil/com.h>
#include <iostream>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "Propsys.lib")
using namespace std;

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int argc;
wchar_t** argv;

void SetPreventPinning(HWND hwnd, BOOL preventPinning)
{
	wil::com_ptr<IPropertyStore> propStore;
	if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&propStore))))
	{
		PROPVARIANT var;
		InitPropVariantFromBoolean(preventPinning, &var);

		PROPERTYKEY key{};
		key.fmtid = { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }; // FMTID_AppUserModel
		key.pid = 9; // PKEY_AppUserModel_PreventPinning

		propStore->SetValue(key, var);
		propStore->Commit();

		PropVariantClear(&var);
	}
}

static void CenterWindow(HWND hwnd, HWND parent) {
	RECT rcParent{};
	if (parent) GetWindowRect(parent, &rcParent);

	RECT rect;
	GetWindowRect(hwnd, &rect);
	auto w = rect.right - rect.left, h = rect.bottom - rect.top;
	if (parent) {
		auto w2 = rcParent.right - rcParent.left, h2 = rcParent.bottom - rcParent.top;
		rect.left = rcParent.left + w2 / 2 - w / 2;
		rect.top = rcParent.top + h2 / 2 - h / 2;
	}
	else {
		rect.left = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
		rect.top = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
	}
	UINT flags = SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER;
	flags |= (IsWindowVisible(hwnd) ? 0 : SWP_HIDEWINDOW);
	SetWindowPos(hwnd, HWND_TOP, rect.left, rect.top, 1, 1, flags);
}
static std::vector<std::wstring>& str_split(
	const std::wstring& src,
	const std::wstring separator,
	std::vector<std::wstring>& dest
) {
	wstring str = src;
	wstring substring;
	wstring::size_type start = 0, index;
	dest.clear();
	index = str.find_first_of(separator, start);
	do {
		if (index == wstring::npos) break;
		substring = str.substr(start, index - start);
		dest.push_back(substring);
		start = index + separator.size();
		index = str.find(separator, start);
		if (start == wstring::npos) break;
	} while (index != wstring::npos);

	substring = str.substr(start);
	dest.push_back(substring);
	return dest;
}

HWND g_mainWindow = nullptr;
wil::com_ptr<ICoreWebView2Controller> g_webviewController;
wil::com_ptr<ICoreWebView2> g_webview;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_SIZE:
		if (g_webviewController)
		{
			RECT bounds;
			GetClientRect(hwnd, &bounds);
			g_webviewController->put_Bounds(bounds);
		}
		return DefWindowProc(hwnd, msg, wParam, lParam);
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return FALSE;
}

class TitleChangedHandler : public ICoreWebView2DocumentTitleChangedEventHandler
{
public:
	TitleChangedHandler(HWND hwnd) : hwnd_(hwnd), refCount_(1) {}

	// IUnknown
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG r = --refCount_; if (r == 0) delete this; return r; }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		if (riid == IID_IUnknown || riid == __uuidof(ICoreWebView2DocumentTitleChangedEventHandler))
		{
			*ppvObject = static_cast<ICoreWebView2DocumentTitleChangedEventHandler*>(this);
			AddRef();
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	// Callback
	HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, IUnknown* args) override
	{
		PWSTR title;
		if (SUCCEEDED(sender->get_DocumentTitle(&title)))
		{
			SetWindowTextW(hwnd_, title);
			CoTaskMemFree(title);
		}
		return S_OK;
	}

private:
	HWND hwnd_;
	ULONG refCount_;
};

class ControllerHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler
{
	std::wstring url_;
public:
	ControllerHandler(const std::wstring& url) : url_(url), refCount_(1) {}
	// IUnknown
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG r = --refCount_; if (r == 0) delete this; return r; }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		if (riid == IID_IUnknown || riid == __uuidof(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler))
		{
			*ppvObject = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this);
			AddRef();
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}
	// Callback
	HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override
	{
		if (controller)
		{
			g_webviewController = controller;
			g_webviewController->get_CoreWebView2(&g_webview);
		}
		RECT bounds;
		GetClientRect(g_mainWindow, &bounds);
		if (g_webviewController)
			g_webviewController->put_Bounds(bounds);

		if (g_webview) {
			g_webview->Navigate(url_.c_str());
			EventRegistrationToken token;
			g_webview->add_DocumentTitleChanged(
				new TitleChangedHandler(g_mainWindow),
				&token
			);
		}
		return S_OK;
	}
private:
	ULONG refCount_;
};

// COM callback：ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
class EnvironmentHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
{
	std::wstring url_;
	std::wstring userDataDir_;
public:
	EnvironmentHandler(const std::wstring& url, const std::wstring& userDataDir)
		: url_(url), userDataDir_(userDataDir), refCount_(1) {
	}
	// IUnknown
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG r = --refCount_; if (r == 0) delete this; return r; }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		if (riid == IID_IUnknown || riid == __uuidof(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler))
		{
			*ppvObject = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this);
			AddRef();
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}
	// Callback
	HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override
	{
		if (FAILED(result) || !env) {
			MessageBoxW(nullptr, L"WebView2 load failed. This is because either runtime is not installed or the parameters are incorrect. Please install it from Microsoft, or check the parameters.", L"Error", MB_ICONERROR);
			PostQuitMessage(1);
			return result;
		}
		if (env)
		{
			env->CreateCoreWebView2Controller(
				g_mainWindow,
				new ControllerHandler(url_)
			);
		}
		return S_OK;
	}
private:
	ULONG refCount_;
};


static ATOM ClassReg(wstring name) {
	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEXW);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = GetModuleHandleW(NULL);
	wcex.hIcon = wcex.hIconSm = LoadIconW(LoadLibraryW(L"imageres.dll"), MAKEINTRESOURCEW(15));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = name.c_str();

	return RegisterClassExW(&wcex);
}

int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd
) {
	argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	class _argv_free_ { public: ~_argv_free_() { LocalFree(argv); } } _argv_free;

	if (argc < 3) {
		MessageBoxW(nullptr, L"Usage: webview.exe <Url> <UserDataDir> [<width>x<height>] [<AppUserModelId>] [<Window Class>]", L"Info", MB_ICONINFORMATION);
		return 0;
	}

	std::wstring url = argv[1];
	std::wstring userDataDir = argv[2];
	long width = 1280, height = 720;
	if (argc >= 4) {
		vector<wstring> str;
		str_split(argv[3], L"x", str);
		if (str.size() >= 2) {
			wchar_t* endptr{};
			width = wcstol(str[0].c_str(), &endptr, 10);
			if (width == LONG_MAX || width == LONG_MIN || (*endptr != L'\0')) {
				width = 1280;
			}
			height = wcstol(str[1].c_str(), &endptr, 10);
			if (height == LONG_MAX || height == LONG_MIN || (*endptr != L'\0')) {
				height = 720;
			}
		}
	}
	if (argc >= 5) {
		std::wstring appModelId = argv[4];
		// Set AppUserModelID
		HRESULT hr = SetCurrentProcessExplicitAppUserModelID(appModelId.c_str());
		if (FAILED(hr)) {
			cerr << "Failed to set AppUserModelID" << endl;
		}
	}
	wstring className = L"win64-webview";
	if (argc >= 6) {
		className = argv[5];
	}

	if (!ClassReg(className)) {
		MessageBoxW(nullptr, L"Class Registration Failed", L"Fatal Error", MB_ICONERROR);
		return GetLastError();
	}
	g_mainWindow = CreateWindowExW(
		0,
		className.c_str(),
		url.c_str(),
		WS_OVERLAPPEDWINDOW,
		0, 0, width, height,
		nullptr,
		nullptr,
		hInstance,
		nullptr
	);
	SetPreventPinning(g_mainWindow, TRUE);
	CenterWindow(g_mainWindow, NULL);
	ShowWindow(g_mainWindow, nShowCmd);

	if (FAILED(CreateCoreWebView2EnvironmentWithOptions(
		nullptr,
		userDataDir.c_str(),
		nullptr,
		new EnvironmentHandler(url, userDataDir)
	))) {
		MessageBoxW(nullptr, L"WebView2 creation failed. This is because either runtime is not installed or the parameters are incorrect. Please install it from Microsoft, or check the parameters.", L"Error", MB_ICONERROR);
		DestroyWindow(g_mainWindow);
		return 1;
	}

	MSG msg = {};
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}
