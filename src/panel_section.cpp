#include "panel_section.h"

#include "imgui/imgui.h"

namespace panel_section {

float indent_width() {
	return ImGui::GetWindowWidth() * INDENT_FRACTION;
}

bool header(const char* label) {
	const float indent = indent_width();
	ImGui::Unindent(indent);
	// A colour of its own (the buttons and the rest are blue), a little darker for the plain header and lighter under the mouse.
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.36f, 0.34f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.17f, 0.46f, 0.43f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.21f, 0.53f, 0.50f, 1.0f));
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * HEADER_TEXT_SCALE);
	const bool open = ImGui::CollapsingHeader(label);
	ImGui::PopFont();
	ImGui::PopStyleColor(3);
	ImGui::Indent(indent);
	return open;
}

}  // namespace panel_section
