#pragma once

// The projects the user has opened lately, most recent first, for the File > Recent Projects menu.
// Just a list of paths (UTF-8) kept short, with a project that is opened again moving to the top
// instead of appearing twice. No file access in here, so it can be tested; the menu is what checks
// that the files still exist.

#include <string>
#include <vector>

class RecentProjects {
public:
	static const int MAX_ENTRIES = 10;

	RecentProjects() {}

	int                count() const { return (int)m_paths.size(); }
	const std::string& path(int index) const { return m_paths[index]; }

	// `path` is the project just opened: it goes to the top of the list.
	void note_opened(const std::string& path) {
		if (path.empty())
			return;
		remove(path);
		m_paths.insert(m_paths.begin(), path);
		if ((int)m_paths.size() > MAX_ENTRIES)
			m_paths.resize(MAX_ENTRIES);
	}

	// Adds to the bottom, for reading the list back in order. Ignored if it is already there or the list is full.
	void append(const std::string& path) {
		if (path.empty() || (int)m_paths.size() >= MAX_ENTRIES)
			return;
		for (size_t i = 0; i < m_paths.size(); i++) {
			if (same_path(m_paths[i], path))
				return;
		}
		m_paths.push_back(path);
	}

	void remove(const std::string& path) {
		for (size_t i = 0; i < m_paths.size(); ) {
			if (same_path(m_paths[i], path))
				m_paths.erase(m_paths.begin() + i);
			else
				i++;
		}
	}

	void clear() { m_paths.clear(); }

	// Whether two paths name the same file as far as spelling can tell: Windows file names don't
	// care about case, and either kind of slash will do.
	static bool same_path(const std::string& a, const std::string& b) {
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); i++) {
			if (fold(a[i]) != fold(b[i]))
				return false;
		}
		return true;
	}

private:
	static char fold(char c) {
		if (c == '\\')
			return '/';
		if (c >= 'A' && c <= 'Z')
			return (char)(c - 'A' + 'a');
		return c;
	}

	std::vector<std::string> m_paths;
};
