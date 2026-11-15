#ifndef slic3r_GUI_WebView_hpp_
#define slic3r_GUI_WebView_hpp_

#include <wx/webview.h>
#include <string>

class WebView
{
public:
    static wxWebView *CreateWebView(wxWindow *parent, wxString const &url);
    // SNORCA: local_only_eligible = Home/Device style page. While "Hide online models" is on, such a
    // view may only reach loopback and private-LAN addresses (see SetLocalOnly).
    static wxWebView *CreateWebView(wxWindow *parent, wxString const &url, bool local_only_eligible);

    // SNORCA: "Hide online models". Linux: a WebKit content filter (compiled once by InitLocalOnlyFilter,
    // before the main window exists) blocks every http(s)/ws(s) request to a non-local host in the
    // eligible views. Windows/macOS: not implemented, LocalOnlySupported() returns false there.
    static void InitLocalOnlyFilter(std::string const &storage_dir);
    static void SetLocalOnly(bool on);
    static bool LocalOnlySupported();
#if wxUSE_WEBVIEW_EDGE
    static bool CheckWebViewRuntime();
    static bool DownloadAndInstallWebViewRuntime();
#endif
    static void LoadUrl(wxWebView * webView, wxString const &url);

    static bool RunScript(wxWebView * webView, wxString const & msg);

    static void RecreateAll();
};

#endif // !slic3r_GUI_WebView_hpp_
