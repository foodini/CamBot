#include "edit_history.h"

void EditHistory::clear() {
	m_undo.clear();
	m_redo.clear();
}

void EditHistory::record(const EditState& before) {
	m_redo.clear();
	m_undo.push_back(before);
	if ((int)m_undo.size() > MAX_STEPS)
		m_undo.erase(m_undo.begin());
}

bool EditHistory::undo(const EditState& current, EditState* restored) {
	if (m_undo.empty())
		return false;
	*restored = m_undo.back();
	m_undo.pop_back();
	m_redo.push_back(current);
	return true;
}

bool EditHistory::redo(const EditState& current, EditState* restored) {
	if (m_redo.empty())
		return false;
	*restored = m_redo.back();
	m_redo.pop_back();
	m_undo.push_back(current);
	return true;
}
