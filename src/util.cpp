#include <chrono>
#include <codecvt>
#include <locale>
#include <thread>

#include <stdio.h>

#include "util.h"

float ffsw::elapsed() {
	return (float)glfwGetTime();
}

bool ffsw::held_repeat_due(float held_seconds, float initial_delay, float interval, int& bucket) {
	if (held_seconds < initial_delay) {
		bucket = -1;
		return false;
	}
	int current_bucket = (int)((held_seconds - initial_delay) / interval);
	if (current_bucket > bucket) {
		bucket = current_bucket;
		return true;
	}
	return false;
}

char* ffsw::make_time(char* buf, float t, bool decimal) {
	int32_t divmod = (int32_t)(t * 10);
	int32_t tenths = divmod % 10;
	divmod /= 10;
	int32_t sec = abs(divmod % 60);
	divmod /= 60;
	int32_t min = abs(divmod % 60);
	divmod /= 60;
	int32_t hours = abs(divmod);
	if (decimal)
		sprintf(buf, "%02d:%02d:%02d.%d", hours, min, sec, tenths);
	else
		sprintf(buf, "%02d:%02d:%02d", hours, min, sec);

	return buf;
}

void ffsw::sleep(uint32_t milliseconds) {
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}


// glad.h (pulled in via util.h, above) already defines APIENTRY for its GL function pointer
// typedefs; undef it first so <windows.h> can define its own copy without a macro-redefinition
// warning. Both expand to the same thing (__stdcall), so this changes nothing at runtime.
#undef APIENTRY
#include <windows.h>
#include <shobjidl.h>

void _ffsw_util_check_result(HRESULT hr, const char* error) {
	if (!SUCCEEDED(hr)) {
		throw(error);
	}
}

void _ffsw_util_initialize_com() {
	static bool initialized = false;

	if (!initialized) {
		HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
		_ffsw_util_check_result(hr, "Unable to initialize COM (CoInitializeEx())");
		initialized = true;
	}
}

// Originally, I had this return a PWSTR, but doing so means that the header must #include <comdef.h>,
// which #defines min and max, which in turn, destroys glm::min and glm::max. Engineering at MS really
// is amateur hour, isn't it? Have you ever seen MS' example code for opening a file? It's about a dozen
// nested if blocks. __ffsw_util_check_result is my workaround for their freshman-level stairstep code.
//
// The format of extension may be L"foo;bar;biz;baz" to allow the four types.
// The format of extension may be L"foo;bar;biz;baz" to allow multiple types, e.g. L"telem;log".
// `title` becomes the dialog's window title/caption, so the user can tell what they're being
// asked for (e.g. "Locate CamBot Project File (*.ffsw)"); pass nullptr for a generic caption
// built from the extension list.
//
// NOTE: every current caller is asking the user to pick a file that ALREADY EXISTS (the
// project file, a video, or a telemetry log) -- never to name a brand-new one -- so this has
// to be an "Open" dialog (IFileOpenDialog / CLSID_FileOpenDialog), not a "Save" dialog. The
// previous code used CLSID_FileSaveDialog with no file-type filter, which is why the dialog
// looked so confusing: it popped up a "Save As"-style picker (a "Save" button, a free-typed
// filename box, no filtering of the file list) for what was really an "open an existing file"
// prompt.
std::string ffsw::file_dialog(const wchar_t* extension, const wchar_t* title, bool must_exist) {
    try {
        _ffsw_util_initialize_com();

        // Create the File Open Dialog Object
        IFileOpenDialog* pfd = NULL;
        HRESULT hr = CoCreateInstance(
            CLSID_FileOpenDialog,
            NULL,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&pfd));
        _ffsw_util_check_result(hr, "Failed to create instance (CoCreateInstance())");

        // Require a real filesystem path in a real (existing) folder. must_exist additionally
        // requires the file itself to already exist -- true for "locate this existing file"
        // callers (the video, the telemetry log), false for the one caller that lets the user
        // either pick an existing file or type the name of a new one (the project file itself).
        DWORD dwFlags;
        hr = pfd->GetOptions(&dwFlags);
        _ffsw_util_check_result(hr, "Failed to get current dialog options (IFileDialog::GetOptions())");

        dwFlags |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        if (must_exist) {
            dwFlags |= FOS_FILEMUSTEXIST;
        } else {
            // IFileOpenDialog comes with FOS_FILEMUSTEXIST set by default -- it has to be
            // explicitly cleared, not just left un-added, or a "new project" name still gets
            // rejected as if it had to already exist.
            dwFlags &= ~FOS_FILEMUSTEXIST;
        }
        hr = pfd->SetOptions(dwFlags);
        _ffsw_util_check_result(hr, "Failed to set dialog options (pfd->SetOptions())");

        // Turn "foo;bar" into a "*.foo;*.bar" filter pattern, so the file list is actually
        // restricted to (and defaults to showing) the extensions we're asking for. The old code
        // called SetDefaultExtension() only, which just fills in an extension if the user types a
        // bare filename -- it does nothing to filter what's shown.
        std::wstring exts(extension);
        std::wstring pattern;
        size_t pos = 0;
        while (true) {
            size_t sep = exts.find(L';', pos);
            std::wstring one_ext = exts.substr(pos, sep == std::wstring::npos ? std::wstring::npos : sep - pos);
            if (!pattern.empty()) pattern += L";";
            pattern += L"*." + one_ext;
            if (sep == std::wstring::npos) break;
            pos = sep + 1;
        }

        std::wstring generic_title = std::wstring(L"Locate File (") + pattern + L")";
        std::wstring caption = title ? std::wstring(title) : generic_title;

        COMDLG_FILTERSPEC filter_specs[] = {
            { caption.c_str(), pattern.c_str() },
            { L"All Files (*.*)",  L"*.*" },
        };
        hr = pfd->SetFileTypes(ARRAYSIZE(filter_specs), filter_specs);
        _ffsw_util_check_result(hr, "Failed to set file type filter (pfd->SetFileTypes())");
        pfd->SetFileTypeIndex(1); // 1-based: default to our filter, not "All Files".

        // Fill in the first listed extension if the user types a bare filename with none:
        std::wstring first_ext = exts.substr(0, exts.find(L';'));
        hr = pfd->SetDefaultExtension(first_ext.c_str());
        _ffsw_util_check_result(hr, "Failed to set file extension: (pfd->SetDefaultExtension())");

        // Give the dialog window an explicit title so it's clear what's being asked for, and use
        // a neutral "Select" button label instead of "Open" -- accurate whether or not the file
        // has to already exist.
        pfd->SetTitle(caption.c_str());
        pfd->SetOkButtonLabel(L"Select");

        // Pop up the dialog and interact with the user.
        hr = pfd->Show(NULL);
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            // The user pressed Cancel: not an error, just no answer.
            pfd->Release();
            return std::string("");
        }
        _ffsw_util_check_result(hr, "FileDialog failed during (pfd->Show())");

        // Get the file the user selected:
        IShellItem* psiResult;
        hr = pfd->GetResult(&psiResult);
        _ffsw_util_check_result(hr, "Failed to get result from pfd (pfd->GetResult())");

        PWSTR pszFilePath = NULL;
        hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
        _ffsw_util_check_result(hr, "Failed to get user's selected path (psiResult->GetDisplayName())");

        using convert_type = std::codecvt_utf8<wchar_t>;
        std::wstring_convert<convert_type, wchar_t> converter;
        std::string retval = converter.to_bytes(pszFilePath);
        CoTaskMemFree(pszFilePath);
        psiResult->Release();
        pfd->Release();
        return retval;
    }
    catch (const char* e) {
        fprintf(stderr, "ffsw::file_dialog() failed: %s\n", e);
        return std::string("");
    }
}

// The same dialog as file_dialog(), for picking several existing files at once.
std::vector<std::string> ffsw::file_dialog_multi(const wchar_t* extension, const wchar_t* title) {
    std::vector<std::string> paths;
    try {
        _ffsw_util_initialize_com();

        IFileOpenDialog* pfd = NULL;
        HRESULT hr = CoCreateInstance(
            CLSID_FileOpenDialog,
            NULL,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&pfd));
        _ffsw_util_check_result(hr, "Failed to create instance (CoCreateInstance())");

        DWORD dwFlags;
        hr = pfd->GetOptions(&dwFlags);
        _ffsw_util_check_result(hr, "Failed to get current dialog options (IFileDialog::GetOptions())");
        dwFlags |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT;
        hr = pfd->SetOptions(dwFlags);
        _ffsw_util_check_result(hr, "Failed to set dialog options (pfd->SetOptions())");

        // "foo;bar" -> "*.foo;*.bar", as in file_dialog().
        std::wstring exts(extension);
        std::wstring pattern;
        size_t pos = 0;
        while (true) {
            size_t sep = exts.find(L';', pos);
            std::wstring one_ext = exts.substr(pos, sep == std::wstring::npos ? std::wstring::npos : sep - pos);
            if (!pattern.empty()) pattern += L";";
            pattern += L"*." + one_ext;
            if (sep == std::wstring::npos) break;
            pos = sep + 1;
        }
        std::wstring caption = title ? std::wstring(title) : std::wstring(L"Select Files (") + pattern + L")";

        COMDLG_FILTERSPEC filter_specs[] = {
            { caption.c_str(), pattern.c_str() },
            { L"All Files (*.*)",  L"*.*" },
        };
        hr = pfd->SetFileTypes(ARRAYSIZE(filter_specs), filter_specs);
        _ffsw_util_check_result(hr, "Failed to set file type filter (pfd->SetFileTypes())");
        pfd->SetFileTypeIndex(1);
        pfd->SetTitle(caption.c_str());
        pfd->SetOkButtonLabel(L"Select");

        hr = pfd->Show(NULL);
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            pfd->Release();
            return paths;
        }
        _ffsw_util_check_result(hr, "FileDialog failed during (pfd->Show())");

        IShellItemArray* results = NULL;
        hr = pfd->GetResults(&results);
        _ffsw_util_check_result(hr, "Failed to get results from pfd (pfd->GetResults())");
        DWORD count = 0;
        hr = results->GetCount(&count);
        _ffsw_util_check_result(hr, "Failed to count the selected files (results->GetCount())");

        using convert_type = std::codecvt_utf8<wchar_t>;
        std::wstring_convert<convert_type, wchar_t> converter;
        for (DWORD i = 0; i < count; i++) {
            IShellItem* item = NULL;
            hr = results->GetItemAt(i, &item);
            _ffsw_util_check_result(hr, "Failed to get a selected file (results->GetItemAt())");
            PWSTR pszFilePath = NULL;
            hr = item->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
            _ffsw_util_check_result(hr, "Failed to get a selected path (item->GetDisplayName())");
            paths.push_back(converter.to_bytes(pszFilePath));
            CoTaskMemFree(pszFilePath);
            item->Release();
        }
        results->Release();
        pfd->Release();
    }
    catch (const char* e) {
        fprintf(stderr, "ffsw::file_dialog_multi() failed: %s\n", e);
        paths.clear();
    }
    return paths;
}

static std::wstring _ffsw_util_widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int count = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    std::wstring wide(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wide[0], count);
    return wide;
}

void ffsw::message_box(const std::string& title, const std::string& text, bool is_error) {
    fprintf(stderr, "%s: %s\n", title.c_str(), text.c_str());
    MessageBoxW(NULL, _ffsw_util_widen(text).c_str(), _ffsw_util_widen(title).c_str(),
        MB_OK | MB_SETFOREGROUND | (is_error ? MB_ICONERROR : MB_ICONINFORMATION));
}

ffsw::Answer ffsw::yes_no_cancel_box(const std::string& title, const std::string& text) {
    int answer = MessageBoxW(NULL, _ffsw_util_widen(text).c_str(), _ffsw_util_widen(title).c_str(),
        MB_YESNOCANCEL | MB_SETFOREGROUND | MB_ICONQUESTION);
    if (answer == IDYES) return ffsw::ANSWER_YES;
    if (answer == IDNO) return ffsw::ANSWER_NO;
    return ffsw::ANSWER_CANCEL;
}

bool ffsw::confirm_box(const std::string& title, const std::string& text) {
    int answer = MessageBoxW(NULL, _ffsw_util_widen(text).c_str(), _ffsw_util_widen(title).c_str(),
        MB_OKCANCEL | MB_SETFOREGROUND | MB_ICONQUESTION);
    return answer == IDOK;
}

// TODO(P2): C++20 allows std::format("%d", 12345); Use it wherever possible, but keep this for everyone else?
std::string ffsw::format(const char* fmt, ...) {
	const int buflen = 1024 * 16;
	char buf[buflen];
	va_list argptr;
	va_start(argptr, fmt);
	size_t size = vsprintf(buf, fmt, argptr);
	va_end(argptr);
	if (size >= buflen) {
		sprintf(buf, "formatted string too long for buffer. (size = %lld)", size);
		throw buf;
	}

	return std::string(buf);
}
