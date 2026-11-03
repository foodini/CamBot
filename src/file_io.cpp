#include "file_io.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace fileio {

namespace {

#ifdef _WIN32
std::wstring to_wide(const std::string& utf8) {
	if (utf8.empty())
		return std::wstring();
	int count = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
	std::wstring wide(count, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wide[0], count);
	return wide;
}
#endif

FILE* open_file(const std::string& path, const char* mode) {
#ifdef _WIN32
	std::wstring wide_mode;
	for (const char* c = mode; *c; c++)
		wide_mode += (wchar_t)*c;
	FILE* file = NULL;
	if (_wfopen_s(&file, to_wide(path).c_str(), wide_mode.c_str()) != 0)
		return NULL;
	return file;
#else
	return fopen(path.c_str(), mode);
#endif
}

void remove_file(const std::string& path) {
#ifdef _WIN32
	_wremove(to_wide(path).c_str());
#else
	remove(path.c_str());
#endif
}

void set_error(std::string* error, const std::string& message) {
	if (error)
		*error = message;
}

// Replaces `to` with `from` (a rename that overwrites). On failure *detail says what the
// system reported.
bool replace_file(const std::string& from, const std::string& to, std::string* detail) {
#ifdef _WIN32
	std::wstring wide_from = to_wide(from);
	std::wstring wide_to = to_wide(to);
	DWORD code = 0;
	// A virus scanner, search indexer, sync client or editor can hold a file for a moment right
	// after it is written or while it is open, which makes the swap fail for no lasting reason.
	// Try for about a second before giving up.
	for (int attempt = 0; attempt < 20; attempt++) {
		if (MoveFileExW(wide_from.c_str(), wide_to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			return true;
		code = GetLastError();
		if (code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION && code != ERROR_LOCK_VIOLATION)
			break;
		Sleep(50);
	}
	*detail = "Windows error " + std::to_string((unsigned long)code);
	return false;
#else
	if (rename(from.c_str(), to.c_str()) == 0)
		return true;
	*detail = strerror(errno);
	return false;
#endif
}

}  // namespace

bool file_exists(const std::string& path) {
	if (path.empty())
		return false;
	FILE* file = open_file(path, "rb");
	if (file == NULL)
		return false;
	fclose(file);
	return true;
}

bool read_text(const std::string& path, std::string* text, std::string* error) {
	FILE* file = open_file(path, "rb");
	if (file == NULL) {
		set_error(error, "Could not open " + path + " for reading.");
		return false;
	}
	text->clear();
	char buffer[4096];
	while (true) {
		size_t got = fread(buffer, 1, sizeof(buffer), file);
		if (got > 0)
			text->append(buffer, got);
		if (got < sizeof(buffer))
			break;
	}
	bool failed = ferror(file) != 0;
	fclose(file);
	if (failed) {
		set_error(error, "Error while reading " + path + ".");
		return false;
	}
	return true;
}

bool write_text_atomic(const std::string& path, const std::string& text, std::string* error) {
	std::string temp_path = path + ".tmp";
	FILE* file = open_file(temp_path, "wb");
	if (file == NULL) {
		set_error(error, "Could not create " + temp_path + ". Is the folder writable?");
		return false;
	}
	size_t written = fwrite(text.data(), 1, text.size(), file);
	bool flushed = fflush(file) == 0;
	bool closed = fclose(file) == 0;
	if (written != text.size() || !flushed || !closed) {
		remove_file(temp_path);
		set_error(error, "Could not write " + temp_path + ". Is the disk full?");
		return false;
	}
	std::string detail;
	if (!replace_file(temp_path, path, &detail)) {
		remove_file(temp_path);
		set_error(error, "Could not replace " + path + " with the new version (" + detail +
			"). Another program may have the file open.");
		return false;
	}
	return true;
}

bool delete_file(const std::string& path) {
	if (!file_exists(path))
		return true;
	remove_file(path);
	return !file_exists(path);
}

int64_t modified_time(const std::string& path) {
#ifdef _WIN32
	WIN32_FILE_ATTRIBUTE_DATA data;
	if (!GetFileAttributesExW(to_wide(path).c_str(), GetFileExInfoStandard, &data))
		return 0;
	ULARGE_INTEGER time;
	time.LowPart = data.ftLastWriteTime.dwLowDateTime;
	time.HighPart = data.ftLastWriteTime.dwHighDateTime;
	return (int64_t)time.QuadPart;
#else
	struct stat info;
	if (stat(path.c_str(), &info) != 0)
		return 0;
	return (int64_t)info.st_mtim.tv_sec * 1000000000LL + (int64_t)info.st_mtim.tv_nsec;
#endif
}

bool copy_file(const std::string& from, const std::string& to, std::string* error) {
	std::string contents;
	if (!read_text(from, &contents, error))
		return false;
	return write_text_atomic(to, contents, error);
}

std::string base_name(const std::string& path) {
	size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string directory_of(const std::string& path) {
	size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string join(const std::string& directory, const std::string& name) {
	if (directory.empty())
		return name;
	char last = directory[directory.size() - 1];
	if (last == '/' || last == '\\')
		return directory + name;
	// Use whichever slash the directory already has; Windows takes both.
	char slash = directory.find('\\') != std::string::npos ? '\\' : '/';
	return directory + slash + name;
}

std::string extension_of(const std::string& path) {
	std::string name = base_name(path);
	size_t dot = name.find_last_of('.');
	if (dot == std::string::npos)
		return std::string();
	std::string ext = name.substr(dot + 1);
	for (size_t i = 0; i < ext.size(); i++)
		ext[i] = (char)tolower((unsigned char)ext[i]);
	return ext;
}

}  // namespace fileio
