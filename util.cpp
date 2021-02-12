#include <chrono>
#include <codecvt>
#include <locale>
#include <thread>

#include <stdio.h>

#include "util.h"

float ffsw::elapsed() {
	return (float)glfwGetTime();
}

char* ffsw::make_time(char* buf, float t, bool decimal) {
	int32_t divmod = t * 10;
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
std::string ffsw::file_dialog(const wchar_t* extension) {
	try {
		_ffsw_util_initialize_com();

		// Create the File Open Dialog Object
		IFileDialog* pfd = NULL;
		HRESULT hr = CoCreateInstance(
			CLSID_FileSaveDialog,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&pfd));
		_ffsw_util_check_result(hr, "Failed to create instance (CoCreateInstance())");

		// Set the options on the dialog.
		DWORD dwFlags;
		hr = pfd->GetOptions(&dwFlags);
		_ffsw_util_check_result(hr, "Failed to get current dialog options (IFileDialog::GetOptions())");

		// We allow selection of files that don't already exist:
		hr = pfd->SetOptions(dwFlags & (~FOS_FORCEFILESYSTEM));
		_ffsw_util_check_result(hr, "Failed to set ~FOS_FORCEFILESYSTEM (pfd->SetOptions())");

		// Default to using `extension` as the file extension:
		hr = pfd->SetDefaultExtension(extension);
		_ffsw_util_check_result(hr, "Failed to set file extension: (pfd->SetDefaultExtension())");

		// Pop up the dialog and interact with the user.
		hr = pfd->Show(NULL);
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
		return retval;
	}
	catch (char* e) {
		fprintf(stderr, "ffsw::file_dialog() failed: %s\n", e);
		return std::string("");
	}
}
