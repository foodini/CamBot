#pragma once

// Small file helpers the project code needs: reading and writing whole text files without losing
// the old contents if something goes wrong halfway, and finding a file that has been moved.
//
// All paths are UTF-8 strings (what the file dialogs hand back). On Windows they are converted
// to wide characters before touching the disk, so file names with accents, CJK, etc. work.
//
// Every function that can fail returns false and fills in *error (if given) with a sentence a
// person can read.

#include <stdint.h>
#include <string>
#include <vector>

namespace fileio {

bool file_exists(const std::string& path);

// Reads the whole file into *text.
bool read_text(const std::string& path, std::string* text, std::string* error);

// Writes the whole file. The new contents go to "<path>.tmp" first and then replace the old
// file in one step, so a crash or a full disk leaves the previous version intact instead of a
// half-written one.
bool write_text_atomic(const std::string& path, const std::string& text, std::string* error);

// Deletes a file. True if it's gone afterwards (including if it was never there).
bool delete_file(const std::string& path);

// When the file was last written, as a number that is only good for comparing with other values
// from this function (a bigger number is later). 0 if the file doesn't exist.
int64_t modified_time(const std::string& path);

// Makes the folder, and any of the folders above it that are missing. True if it is there afterwards.
bool make_directories(const std::string& path);

// A file found by list_files.
struct FileEntry {
	std::string path;       // The directory that was searched + what is under it.
	int64_t     bytes;
	int64_t     modified;   // As modified_time() gives it.
};

// Adds every file under `directory`, in its folders too, to *files. (Nothing is added if there is no such folder.)
void list_files(const std::string& directory, std::vector<FileEntry>* files);

// Copies one file over another (the destination is replaced if it exists).
bool copy_file(const std::string& from, const std::string& to, std::string* error);

// "C:\videos\flight.mp4" -> "flight.mp4"; either kind of slash works.
std::string base_name(const std::string& path);

// "C:\videos\flight.mp4" -> "C:\videos" (no trailing slash); "" if the path has no directory.
std::string directory_of(const std::string& path);

// directory + the kind of slash the directory already uses + name. An empty directory gives
// just the name.
std::string join(const std::string& directory, const std::string& name);

// The ending of a file name, without the dot and in lower case: "Flight.MP4" -> "mp4".
std::string extension_of(const std::string& path);

}  // namespace fileio
