#include "version.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <stdio.h>
#include <string>
#include <thread>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "glm/glm.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"

#include "stb_image.h"
#include "stb_image_write.h"

#include "env_config.h"
#include "file_io.h"
#include "flight_summary_overlay.h"
#include "font_manager.h"
#include "interaction_manager.h"
#include "lines.h"
#include "project_file_manager.h"
#include "media_container_manager.h"
#include "render_target.h"
#include "edit_model.h"
#include "media_decoder.h"
#include "project_data.h"
#include "telemetry.h"
#include "timecode.h"
#include "timeline_editor.h"
#include "timeline_view.h"
#include "util.h"
#include "widget.h"
#include "window_layout.h"

//TODO(P1): remove the std::thread use. You can't terminate a running thread with std::thread, and I need
//          to be able to kill the parse thread if it's still running when the program terminates. Also,
//          I just want the thing to go away when it finishes. I'd rather not have to join it.

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow* window);

// settings
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;
const unsigned int UI_HEIGHT = 134;        // Divisible by FOUR, RIGHT?
const unsigned int MENU_HEIGHT = 19;       // What the ImGui main menu bar measures with the default font and style
                                            // (FontSize 13 + FramePadding.y 3 * 2). Only the first frame goes by
                                            // it; after that the layout uses the height ImGui reports.
const int MIN_WINDOW_WIDTH = 900;           // Smaller than this and the transport bar no longer fits.
const int MIN_WINDOW_HEIGHT = 600;

bool paused = false;

// Takes down the GUI libraries and GLFW, for leaving before the main loop ever starts (the user
// cancelled the project dialog, or the video could not be opened).
static void shut_down_gui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
}

// Turns a request to close the window into a question when the project has unsaved changes.
// True when it's time to leave for real.
// Used before leaving the open project, whether for good (exit) or for another one: if it has
// unsaved changes, asks whether to save them. True if it is all right to go on: nothing to save, or
// saved, or the user chose to let the changes go. False if they cancelled (or the save failed).
static bool confirm_leaving_project(EnvConfig& env_config, ProjectFileManager& project_file_mgr) {
    env_config.poll_project();   // So changes from the last frame count.
    if (!env_config.project_unsaved())
        return true;
    ffsw::Answer answer = ffsw::yes_no_cancel_box("CamBot",
        "Save changes to \"" + fileio::base_name(project_file_mgr.get_project_file_path()) + "\" before closing?");
    if (answer == ffsw::ANSWER_NO)
        return true;
    return answer == ffsw::ANSWER_YES && env_config.save_project();
}

static bool exit_confirmed(GLFWwindow* window, EnvConfig& env_config, ProjectFileManager& project_file_mgr) {
    if (!glfwWindowShouldClose(window))
        return false;
    if (confirm_leaving_project(env_config, project_file_mgr))
        return true;
    glfwSetWindowShouldClose(window, GLFW_FALSE);   // Cancelled, or the save failed.
    return false;
}

// Opens the project CamBot starts with: the one named on the command line, or else the one that
// was open when it was last used. If there is neither, or it has gone, or it would not open (the
// user has been told why), the user is asked which project to use.
static bool open_startup_project(ProjectFileManager* project, const std::string& command_line_path, const std::string& last_project) {
    const std::string& wanted = !command_line_path.empty() ? command_line_path : last_project;
    // Said on the console so that it can be seen why the chooser came up when it did.
    printf("Startup project: command line \"%s\", last used \"%s\"\n", command_line_path.c_str(), last_project.c_str());
    if (!wanted.empty()) {
        if (project->open_path(wanted)) {
            printf("Opened \"%s\" without asking.\n", wanted.c_str());
            return true;
        }
        printf("Could not open \"%s\" (it is missing, or would not load); asking instead.\n", wanted.c_str());
        *project = ProjectFileManager();   // Whatever the attempt left behind.
    }
    return project->open_interactive();
}

static bool ctrl_is_down(GLFWwindow* window) {
    return glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
}

static bool shift_is_down(GLFWwindow* window) {
    return glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
}

// The Help > Keyboard Shortcuts window: every key and mouse binding, until the interface
// explains itself well enough not to need it.
static void draw_shortcuts_window(bool* open) {
    if (!*open)
        return;
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin("Keyboard Shortcuts", open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    // The letter shortcuts are bound by the character they make (see InteractionMgr::bind_char),
    // so they follow the user's keyboard layout and are named here by that character.
    const std::string rotate_flip_key = "F";
    const std::string rotate_left_key = ",";
    const std::string rotate_right_key = ".";

    ImGui::TextDisabled("Letter keys follow your keyboard layout: they are the keys printed with these letters.");
    ImGui::Spacing();
    if (ImGui::BeginTable("shortcut_keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Does", ImGuiTableColumnFlags_WidthStretch);
        struct Row { std::string key; const char* does; };
        const Row rows[] = {
            { "Space", "Play / pause" },
            { "Left / Right", "Step one frame back / forward (hold to repeat)" },
            { "Up / Down", "Nudge the telemetry sync one sample (hold to repeat)" },
            { rotate_flip_key, "Rotate the video 180 degrees" },
            { rotate_left_key + " / " + rotate_right_key, "Rotate the video a little left / right (hold to repeat)" },
            { "Ctrl+S", "Save the project" },
            { "S", "Split the clip at the playhead (the playhead's frame starts the second piece)" },
            { "[", "Trim Left: delete the clip from its start up to the playhead's frame" },
            { "]", "Trim Right: delete the clip from after the playhead's frame to its end" },
            { "Delete", "Delete the clip under the playhead" },
            { "Ctrl+Z", "Undo the last edit" },
            { "Ctrl+Y or Ctrl+Shift+Z", "Redo" },
            { "Esc", "Cancel an export in progress" },
        };
        for (const Row& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.key.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row.does);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::Text("On the video bar and the telemetry bar:");
    ImGui::BulletText("Left-click and hold: jump there. On the telemetry bar this re-syncs\nthe telemetry to the frame on screen.");
    ImGui::BulletText("Right-click and drag: scan forward / back. The farther you drag from\nwhere you clicked, the faster it goes.");
    ImGui::End();
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and 
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);

    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    //
    // Somehow, we need to figure out the new size of the media and the UI. I would like to prevent
    // manual resizing & replace w/ a couple simple fractions; 1/2, 3/4, etc.
    //
    // MAKE SURE YOU TELL EnvConfig ABOUT THE CHANGE AND HAVE IT CALL recompute_projections().
    //
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!



}

// Captures the (0, UI_HEIGHT, SCR_WIDTH, SCR_HEIGHT) rectangle -- in pixel space, exactly the
// rectangle the video is rendered into (see `extents` in main(), and the existing recording
// capture below, both of which read this same rectangle) -- from the picture the frame is being
// drawn into, and writes it out as a PNG at `path`. No-op if `path` is empty, so callers can
// just call this unconditionally once per frame and let it decide. Called at two different
// points in the same frame's render sequence (see the call sites in the render loop below) so
// each capture sees exactly the pixels it's supposed to: right after the video renders (raw, no
// overlay yet) or right after every widget/text has (telemetry overlay baked in).
static void save_frame_as_png(const RenderTarget& canvas, const std::string& path) {
    if (path.empty())
        return;
    std::vector<uint8_t> buf((size_t)SCR_WIDTH * SCR_HEIGHT * 3);
    canvas.read_rgb(0, UI_HEIGHT, SCR_WIDTH, SCR_HEIGHT, buf.data());
    // The first row read is the bottom of the image; PNG (and stb_image_write) expect the
    // first row to be the top, so flip on the way out rather than changing how we read.
    stbi_flip_vertically_on_write(1);
    stbi_write_png(path.c_str(), SCR_WIDTH, SCR_HEIGHT, 3, buf.data(), SCR_WIDTH * 3);
}

// The "Climb Rate Settings" window: which causal filter smooths the climb rate (see
// climb_filter.h), how long its window is, and the +/- ft/min the map/indicator colors span.
// Because the telemetry is already in memory, changing the filter just re-runs it over every
// slice and rebuilds the map's course -- but that takes a moment, so the sliders keep their own
// working copy and only apply it on release (a drag would redo all of it every frame).
static void draw_climb_settings_window(bool* open, bool* resync, bool exporting) {
    EnvConfig* env_config = EnvConfig::instance;
    TelemetryMgr* telemetry_mgr = TelemetryMgr::instance;

    static ClimbFilterSettings edit;
    static float edit_range = 800.0f;
    if (*resync) {
        edit = env_config->climb_filter_settings();
        edit_range = env_config->climb_color_range();
        *resync = false;
    }

    if (!ImGui::Begin("Climb Rate Settings", open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    const bool loading = !telemetry_mgr->parsing_done();
    if (loading) {
        ImGui::TextWrapped("Telemetry is still loading. The filter settings unlock when it finishes.");
    } else if (exporting) {
        ImGui::TextWrapped("The filter settings are locked while exporting.");
    }

    ImGui::BeginDisabled(loading || exporting);

    bool apply_filter = false;
    const char* filter_names[CLIMB_FILTER_TYPE_COUNT] = { "Trailing-window slope", "Alpha-beta (critically damped)" };
    if (ImGui::Combo("Filter", &edit.type, filter_names, CLIMB_FILTER_TYPE_COUNT)) {
        apply_filter = true;
    }

    const bool windowed = (edit.type == CLIMB_FILTER_WINDOWED_SLOPE);
    float* param = windowed ? &edit.window_seconds : &edit.time_constant_seconds;
    ImGui::SliderFloat(windowed ? "Window (s)" : "Time constant (s)", param,
        windowed ? 0.2f : 0.1f, windowed ? 20.0f : 10.0f, "%.2f",
        ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        apply_filter = true;
    }

    // What a setting costs. Longer smooths more but runs further behind reality; shorter is
    // quicker but noisier. The noise figures are a floor: they assume independent (white)
    // barometer noise of the size measured from this recording's center sensor, and the real
    // noise is partly correlated, which averaging removes much less well.
    const float sample_hz = (float)TELEMETRY_FREQUENCY;
    const float sigma_m = telemetry_mgr->alt_noise_sigma_m();
    const float fpm = METERS_PER_SECOND_TO_FEET_PER_MINUTE;
    ImGui::Spacing();
    ImGui::Text("Selected:  noise floor ~%.1f ft/min,  lag ~%.1f s",
        climb_filter_expected_noise_mps(edit.type, *param, sigma_m, sample_hz) * fpm,
        climb_filter_expected_lag_seconds(edit.type, *param, sample_hz));
    ImGui::TextDisabled("Noise floor assumes independent barometer noise of %.2f m rms (measured from this flight's center sensor).", sigma_m);
    ImGui::TextDisabled("Real jitter runs higher, especially for long windows, because that noise is partly correlated.");
    ImGui::TextDisabled("Longer = smoother but later. Shorter = quicker but noisier.");
    ImGui::Spacing();

    if (ImGui::BeginTable("climb_filter_hints", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn(windowed ? "Window" : "Time constant");
        ImGui::TableSetupColumn("Noise floor (ft/min)");
        ImGui::TableSetupColumn("Lag (s)");
        ImGui::TableHeadersRow();

        // For reference: how the climb rate was computed before these filters existed.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("old: 0.5 s difference");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.1f", sigma_m * sqrtf(2.0f) / 0.5f * fpm);
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.2f", 0.25f);

        static const float windows[6] = { 0.5f, 1.0f, 2.0f, 3.0f, 5.0f, 10.0f };
        static const float time_constants[6] = { 0.25f, 0.5f, 1.0f, 1.5f, 2.5f, 5.0f };
        for (int row = 0; row < 6; row++) {
            const float value = windowed ? windows[row] : time_constants[row];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%.2f s", value);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.1f", climb_filter_expected_noise_mps(edit.type, value, sigma_m, sample_hz) * fpm);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f", climb_filter_expected_lag_seconds(edit.type, value, sample_hz));
        }
        ImGui::EndTable();
    }

    ImGui::EndDisabled();

    if (apply_filter) {
        env_config->climb_filter_settings(edit);
    }

    // The color scale is just a shader uniform, so it follows the slider live; the project file
    // is only written when the slider is let go.
    ImGui::Separator();
    if (ImGui::SliderFloat("Color range (+/- ft/min)", &edit_range, 100.0f, 2000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
        env_config->climb_color_range(edit_range);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        env_config->save_user_settings();
    }

    ImGui::End();
}

// The "Map Settings" window: how wide the map's course trace is drawn for the most recent
// stretch of flight, how wide for everything older, how long "recent" is, and how far the map
// may zoom in and out. The trace settings are just shader uniforms, so they show immediately;
// the zoom limits re-fit every telemetry slice, which takes a moment, so those apply when a
// slider is let go. Everything is saved to the user settings (not the project) on release.
static void draw_map_settings_window(bool* open, bool* resync, bool zoom_locked) {
    EnvConfig* env_config = EnvConfig::instance;

    static MapTraceSettings trace;
    static MapZoomSettings zoom;
    if (*resync) {
        trace = env_config->map_trace_settings();
        zoom = env_config->map_zoom_settings();
        *resync = false;
    }

    if (!ImGui::Begin("Map Settings", open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Trace");
    bool trace_changed = false;
    bool trace_released = false;
    trace_changed |= ImGui::SliderFloat("Recent trace width (px)", &trace.recent_width_px, 2.0f, 8.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    trace_released |= ImGui::IsItemDeactivatedAfterEdit();
    trace_changed |= ImGui::SliderFloat("Older trace width (px)", &trace.old_width_px, 2.0f, 8.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    trace_released |= ImGui::IsItemDeactivatedAfterEdit();
    trace_changed |= ImGui::SliderFloat("Recent period (s)", &trace.recent_seconds, 30.0f, 600.0f, "%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    trace_released |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::TextDisabled("The most recent period is drawn at the recent width, easing to the older width over");
    ImGui::TextDisabled("the 10 s after it. Trace the glider hasn't flown yet is muted, at the older width.");
    if (trace_changed) {
        env_config->map_trace_settings(trace);
    }
    if (trace_released) {
        env_config->save_user_settings();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Zoom");
    const bool loading = !TelemetryMgr::instance->parsing_done();
    if (loading) {
        ImGui::TextWrapped("Telemetry is still loading. The zoom settings unlock when it finishes.");
    } else if (zoom_locked) {
        ImGui::TextWrapped("The zoom settings are locked while exporting.");
    }
    ImGui::BeginDisabled(loading || zoom_locked);
    bool zoom_released = false;
    // The closest zoom can't be wider than the farthest: whichever slider is moved past the
    // other pushes it along.
    if (ImGui::SliderFloat("Closest zoom: map width (km)", &zoom.min_width_km, 0.2f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
        zoom.max_width_km = (std::max)(zoom.max_width_km, zoom.min_width_km);
    }
    zoom_released |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::SliderFloat("Farthest zoom: map width (km)", &zoom.max_width_km, 0.2f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
        zoom.min_width_km = (std::min)(zoom.min_width_km, zoom.max_width_km);
    }
    zoom_released |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Zoom transition (s)", &zoom.transition_seconds, 2.0f, 60.0f, "%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    zoom_released |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::TextDisabled("In straight flight the map shows the farthest zoom. After a full circle within 45 s it");
    ImGui::TextDisabled("zooms in to fit the last 45 s of path (never past the closest zoom), and back out once");
    ImGui::TextDisabled("the turning stops. Always centered on the pilot.");
    ImGui::EndDisabled();

    if (zoom_released && !loading && !zoom_locked) {
        env_config->map_zoom_settings(zoom);
    }

    ImGui::End();
}

// The "Flight Summary" window: how long the summary stays up at the end of the video, and whether
// it includes the cross-country statistics. Both are the user's (saved with the other user
// settings). Locked while exporting so the summary can't change partway through a render.
static void draw_summary_settings_window(bool* open, bool* resync, bool exporting) {
    EnvConfig* env_config = EnvConfig::instance;

    static SummarySettings edit;
    if (*resync) {
        edit = env_config->summary_settings();
        *resync = false;
    }

    if (!ImGui::Begin("Flight Summary", open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    if (exporting) {
        ImGui::TextWrapped("The summary settings are locked while exporting.");
    }
    ImGui::BeginDisabled(exporting);
    bool changed = false;
    bool released = false;
    changed |= ImGui::SliderFloat("Show for the last (s)", &edit.seconds, 3.0f, 60.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    released |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::Checkbox("Cross-country statistics", &edit.show_xc)) {
        changed = true;
        released = true;
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Adds distance covered, best ground glide, circles, and time circling.");
    ImGui::TextDisabled("Statistics cover the same stretch of flight as the altitude graph.");

    if (changed && !exporting) {
        env_config->summary_settings(edit);
    }
    if (released && !exporting) {
        env_config->save_user_settings();
    }

    ImGui::End();
}

// The "Altitude Reference" window: the true altitude (ft) at the start and at the end of the
// video, which the barometer is gently re-based on (see altitude_reference.h). It's the
// project's, not the user's, since it describes where this video was shot. A change applies as
// soon as the number is committed (Enter, or clicking away) or a box is ticked.
static void draw_altitude_window(bool* open, bool* resync, bool exporting) {
    EnvConfig* env_config = EnvConfig::instance;
    TelemetryMgr* telemetry_mgr = TelemetryMgr::instance;

    static bool use_start = false;
    static bool use_end = false;
    static bool same_as_start = false;
    static float start_ft = 0.0f;
    static float end_ft = 0.0f;
    if (*resync) {
        const AltitudeReferences& refs = env_config->altitude_references();
        use_start = refs.has_start();
        start_ft = use_start ? refs.start_ft : 0.0f;
        use_end = AltitudeReferences::is_set(refs.end_ft);
        end_ft = use_end ? refs.end_ft : 0.0f;
        same_as_start = refs.end_same_as_start;
        *resync = false;
    }

    if (!ImGui::Begin("Altitude Reference", open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    const bool loading = !telemetry_mgr->parsing_done();
    if (loading) {
        ImGui::TextWrapped("Telemetry is still loading. This unlocks when it finishes.");
    } else if (exporting) {
        ImGui::TextWrapped("The altitude references are locked while exporting.");
    }

    ImGui::TextWrapped("Enter the true altitude at the start and at the end of the video. The barometer "
        "is shifted to match, and the shift changes smoothly from one to the other, so slow drift "
        "disappears. Enter just one to shift the whole flight by a constant.");
    ImGui::Spacing();

    ImGui::BeginDisabled(loading || exporting);
    bool changed = false;

    changed |= ImGui::Checkbox("Start of video (ft)", &use_start);
    ImGui::SameLine();
    ImGui::BeginDisabled(!use_start);
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputFloat("##start_ft", &start_ft, 0.0f, 0.0f, "%.1f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::EndDisabled();

    changed |= ImGui::Checkbox("End of video (ft)", &use_end);
    ImGui::SameLine();
    ImGui::BeginDisabled(!use_end || same_as_start);
    ImGui::SetNextItemWidth(110.0f);
    float shown_end_ft = same_as_start ? start_ft : end_ft;
    if (ImGui::InputFloat("##end_ft", &shown_end_ft, 0.0f, 0.0f, "%.1f") && !same_as_start) {
        end_ft = shown_end_ft;
    }
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::EndDisabled();
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Same as start", &same_as_start);

    ImGui::EndDisabled();

    if (changed && !loading && !exporting) {
        AltitudeReferences refs;
        refs.start_ft = use_start ? start_ft : ALTITUDE_REF_UNSET;
        refs.end_ft = use_end ? end_ft : ALTITUDE_REF_UNSET;
        refs.end_same_as_start = same_as_start;
        env_config->altitude_references(refs);
    }

    if (!loading) {
        const AltitudeCorrectionInfo& info = telemetry_mgr->altitude_correction_info();
        const float ft_per_m = 3.28084f;
        ImGui::Separator();
        if (!info.has_start && !info.has_end) {
            ImGui::TextDisabled("No correction applied: the overlay shows the barometer as logged.");
        }
        if (info.has_start) {
            ImGui::Text("Start: the barometer read %.0f ft here; shifted by %+.1f ft.", info.start_baro_m * ft_per_m, info.start_offset_m * ft_per_m);
        }
        if (info.has_end) {
            ImGui::Text("End: the barometer read %.0f ft here; shifted by %+.1f ft.", info.end_baro_m * ft_per_m, info.end_offset_m * ft_per_m);
        }
        if (info.has_start && info.has_end) {
            const float drift_m = info.drift_m();
            ImGui::Text("The barometer drifted %+.1f ft (%+.1f m) between the two.", drift_m * ft_per_m, drift_m);
            if (fabsf(drift_m) > 15.0f) {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                    "That's more than sensor drift usually accounts for. Check the numbers, or the weather moved a lot.");
            }
        }
        ImGui::TextDisabled("The climb rate always uses the barometer as logged.");
    }

    ImGui::End();
}

// The panel down the left of the window. For now just its places: the Media list and the
// telemetry sync controls arrive in later steps.
static void draw_left_panel(const LayoutRect& rect) {
    if (rect.w < 1.0f || rect.h < 1.0f)
        return;
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
    ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("Panel", nullptr, flags)) {
        if (ImGui::CollapsingHeader("Media", ImGuiTreeNodeFlags_DefaultOpen))
            ImGui::TextDisabled("The video files will be listed here.");
        if (ImGui::CollapsingHeader("Telemetry", ImGuiTreeNodeFlags_DefaultOpen))
            ImGui::TextDisabled("Sync controls will go here.");
    }
    ImGui::End();
}

// The bar under the preview: where the video is now and the two screenshot buttons (the same as the
// File menu's, they set the path the render loop saves to), and below them the buttons to move around
// and play.
// Reverse, slow and fast play are shown but off until playback can do them. While an export runs
// the controls are off, as it needs the video to itself.
static void draw_transport_bar(const LayoutRect& rect, MediaContainerMgr& media, std::string* raw_screenshot_path,
    std::string* overlay_screenshot_path) {
    if (rect.w < 1.0f || rect.h < 1.0f)
        return;
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
    ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("Transport", nullptr, flags)) {
        const ImVec2 button_size(60.0f, 0.0f);
        const bool exporting = media.recording();
        const ImGuiStyle& style = ImGui::GetStyle();

        // Top row: where the video is, and how long it is (hours:minutes:seconds#frame, from the
        // frames' own times), with the two screenshot buttons pinned to the right end of the bar.
        // Their places come from the bar's width and their own sizes, never from the text, so they
        // stay put as the text changes length.
        const AVRational tick = media.tick_time_base();
        const Timecode now_time = timecode::for_ticks(media.current_ticks(), tick.num, tick.den, media.frame_ticks());
        // A one-digit frame number gets a space after it, so the text after it stays where it is.
        const std::string now = timecode::format(now_time, 2) + (now_time.frame < 10 ? " " : "");
        const std::string total = timecode::format(timecode::for_ticks(media.total_ticks(), tick.num, tick.den, media.frame_ticks()), 2);
        const std::string rate = timecode::format_frame_rate(tick.num, tick.den, media.frame_ticks());
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s / %s (@%sfps)", now.c_str(), total.c_str(), rate.c_str());

        const char* raw_label = "Save Frame...";
        const char* overlay_label = "Save Frame with Telemetry...";
        const float raw_width = ImGui::CalcTextSize(raw_label).x + 2.0f * style.FramePadding.x;
        const float overlay_width = ImGui::CalcTextSize(overlay_label).x + 2.0f * style.FramePadding.x;
        ImGui::SameLine(ImGui::GetWindowWidth() - style.WindowPadding.x - overlay_width - style.ItemSpacing.x - raw_width);
        if (ImGui::Button(raw_label)) {
            *raw_screenshot_path = ffsw::file_dialog(L"png", L"Save Raw Video Frame As (*.png)", false);
        }
        ImGui::SameLine();
        if (ImGui::Button(overlay_label)) {
            *overlay_screenshot_path = ffsw::file_dialog(L"png", L"Save Frame with Telemetry Overlay As (*.png)", false);
        }

        // Bottom row: moving around and playing.
        ImGui::BeginDisabled(exporting);
        if (ImGui::Button("Start", button_size)) {
            paused = true;
            media.advance_to_parametric(0.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("-1", button_size)) {
            paused = true;
            media.rewind_by(1);
        }
        ImGui::SameLine();
        if (ImGui::Button(paused ? "Play" : "Pause", button_size))
            paused = !paused;
        ImGui::SameLine();
        if (ImGui::Button("+1", button_size)) {
            paused = true;
            media.advance_by(1);
        }
        ImGui::SameLine();
        if (ImGui::Button("End", button_size)) {
            paused = true;
            media.advance_to_parametric(1.0f);
        }
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 24.0f);
        ImGui::BeginDisabled(true);
        ImGui::Button("Rev", button_size);
        ImGui::SameLine();
        ImGui::Button("Slow", button_size);
        ImGui::SameLine();
        ImGui::Button("Fast", button_size);
        ImGui::EndDisabled();
    }
    ImGui::End();
}

// The divider between the upper part of the window and the timeline: drag it to make the timeline
// taller or shorter. It follows the mouse, so the timeline's top edge is wherever the pointer is.
static void draw_splitter(const LayoutRect& rect, float* timeline_height) {
    if (rect.w < 1.0f || rect.h < 1.0f)
        return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
    ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("Splitter", nullptr, flags)) {
        ImGui::InvisibleButton("##splitter", ImVec2(rect.w, rect.h));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (hovered || active)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (active) {
            ImGuiIO& io = ImGui::GetIO();
            *timeline_height = io.DisplaySize.y - io.MousePos.y - rect.h / 2.0f;
        }
        ImU32 color = active ? IM_COL32(120, 160, 220, 255) : (hovered ? IM_COL32(90, 100, 120, 255) : IM_COL32(45, 45, 52, 255));
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h), color);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// The divider between the left panel and the preview: drag it to make the panel wider (and the
// picture smaller). It follows the mouse; the layout stops it where the picture is as big as the
// window allows, as the panel already has all the room the picture doesn't use.
static void draw_panel_splitter(const LayoutRect& rect, float widest_panel, float* panel_width) {
    if (rect.w < 1.0f || rect.h < 1.0f)
        return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
    ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("PanelSplitter", nullptr, flags)) {
        ImGui::InvisibleButton("##panel_splitter", ImVec2(rect.w, rect.h));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (hovered || active)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (active) {
            // Held to the most the layout allows, so a drag past it doesn't wind up.
            float wanted = ImGui::GetIO().MousePos.x - rect.w / 2.0f;
            *panel_width = wanted < widest_panel ? wanted : widest_panel;
        }
        ImU32 color = active ? IM_COL32(120, 160, 220, 255) : (hovered ? IM_COL32(90, 100, 120, 255) : IM_COL32(45, 45, 52, 255));
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h), color);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

int main()
{
    // glfw: initialize and configure
    // ------------------------------
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // glfw window creation
    // --------------------
    // It opens the way it was left: the same size, and maximized if it was. (Only the window's
    // size is wanted from the user settings here; EnvConfig reads the rest of them later.)
    UserSettings startup_settings;
    WindowSettings window_state = startup_settings.window;
    int start_width = window_state.width;
    int start_height = window_state.height;
    GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor();
    if (primary_monitor != nullptr) {
        // A window saved on a bigger screen than this one is cut down to fit.
        int work_x = 0, work_y = 0, work_width = 0, work_height = 0;
        glfwGetMonitorWorkarea(primary_monitor, &work_x, &work_y, &work_width, &work_height);
        if (work_width > 0 && start_width > work_width)
            start_width = work_width;
        if (work_height > 0 && start_height > work_height)
            start_height = work_height;
    }
    if (start_width < MIN_WINDOW_WIDTH)
        start_width = MIN_WINDOW_WIDTH;
    if (start_height < MIN_WINDOW_HEIGHT)
        start_height = MIN_WINDOW_HEIGHT;
    glfwWindowHint(GLFW_MAXIMIZED, window_state.maximized ? GLFW_TRUE : GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(start_width, start_height, "CamBot", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwSetWindowSizeLimits(window, MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // glad: load all OpenGL function pointers
    // ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // Dear ImGui: create the context and wire up the GLFW/OpenGL3 backends. This is what
    // lets us draw the File/Edit/View/Help menu bar on top of the scene each frame.
    // -----------------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    float bottom = (float)(1.0 - 2.0 * SCR_HEIGHT / (SCR_HEIGHT + UI_HEIGHT));
    glm::vec3 extents[4];  //bl, br, tl, tr
    extents[0] = glm::vec3(-1.0, bottom, 0.0);
    extents[1] = glm::vec3(1.0, bottom, 0.0);
    extents[2] = glm::vec3(-1.0, 1.0, 0.0);
    extents[3] = glm::vec3(1.0, 1.0, 0.0);

    ProjectFileManager project_file_mgr;
    if (!open_startup_project(&project_file_mgr, ffsw::command_line_project(), startup_settings.last_project)) {
        // Cancelled, or the project had a problem (which the user has already been told about).
        shut_down_gui();
        return 0;
    }

    // All the video files of the flight, played as one. This is where files that are damaged,
    // or that don't belong together, turn up.
    MediaDecoder media_decoder;
    std::string open_error;
    if (!media_decoder.open(project_file_mgr.get_video_files(), &open_error)) {
        ffsw::message_box("CamBot - Cannot Open Video", open_error);
        shut_down_gui();
        return 1;
    }

    // What the user has made of those files: their order, the clips and how they are tied to the
    // telemetry (see edit_model.h). Built from the project, then handed to the decoder.
    EditModel edit_model;
    {
        std::vector<int64_t> media_durations;
        for (int media = 0; media < media_decoder.media_count(); media++)
            media_durations.push_back(media_decoder.media_duration_ticks(media));
        AVRational tick = media_decoder.time_base();
        std::string model_error;
        if (!project_io::apply_to_model(project_file_mgr.get_data(), media_durations, tick.num, tick.den, &edit_model, &model_error)) {
            ffsw::message_box("CamBot - Cannot Open Project", model_error);
            shut_down_gui();
            return 1;
        }
        if (!media_decoder.set_clips(edit_model.edit_list().clips(), 0)) {
            ffsw::message_box("CamBot - Cannot Open Project", "The video could not be set up the way this project describes.");
            shut_down_gui();
            return 1;
        }
        // Reading a project can fill in what it left out (or convert an older one); that is not a
        // change the user made, so it must not show up as unsaved.
        ProjectData normalized = project_file_mgr.get_data();
        project_io::fill_from_model(edit_model, tick.num, tick.den, &normalized);
        project_file_mgr.adopt_model_state(normalized);
    }

    // The frame is drawn into this picture and then copied to the window, rather than drawn into
    // the window directly: exports and screenshots read the picture, so they don't depend on what
    // the window is doing. It is the whole canvas the widgets are laid out on (the video, and the
    // strip under it that holds the scrub bars).
    RenderTarget canvas;
    if (!canvas.create(SCR_WIDTH, SCR_HEIGHT + UI_HEIGHT)) {
        ffsw::message_box("CamBot - Cannot Start", "The graphics driver could not make the off-screen picture CamBot draws each frame into.");
        shut_down_gui();
        return 1;
    }

    MediaContainerMgr media_container_mgr(&media_decoder, "3.3.shader.vert", "3.3.shader.frag", extents);
    // Restore whatever rotation was saved with this project (see EnvConfig::save_project()
    // and the "Save Project" menu item below for where it's written back out).
    media_container_mgr.rotation_angle(project_file_mgr.get_rotation_angle());
    FontManager font_manager("c:\\Windows\\Fonts\\courbd.ttf", 48, SCR_WIDTH, SCR_HEIGHT + UI_HEIGHT);
    //TODO(P1) Hand the telemetry_mgr, instead of its vector, into the env_config.
    EnvConfig env_config(&media_container_mgr, &font_manager, &project_file_mgr, &edit_model, (float)SCR_WIDTH, (float)SCR_HEIGHT + (float)UI_HEIGHT, (float)UI_HEIGHT);
    env_config.note_project_opened(project_file_mgr.get_project_file_path());

    DateTimeWidget date_time_widget(.29f, .18f, 1.0f-.29f-.015f, 1.0f-.18f-.01f);
    TimelineView timeline_view;
    TimelineEditor timeline_editor;
    TelemetryScrubWidget telemetry_scrub_widget(1.96f, 0.04f, -.98f, -.885f);

    MapWidget map_widget(.35f, .35f*SCR_WIDTH/(SCR_HEIGHT+UI_HEIGHT), 0.6f, -0.765f);
    ClimbWidget climb_widget(.02f, .35f * SCR_WIDTH / (SCR_HEIGHT + UI_HEIGHT), 0.9625f, -0.765f);
    // The flight summary's last line sits at the graph's bottom edge, so both are the same distance off the bottom of the video.
    const float GRAPH_BOTTOM_NDC = -1.0f + .24f;
    GraphWidget graph_widget(.5f, .3f, -0.99f, GRAPH_BOTTOM_NDC);
    FlightSummaryOverlay flight_summary_overlay(GRAPH_BOTTOM_NDC);
    //TODO(P0): This is dumb. Have each widget know whether it needs a poly call. The TelemetryMgr can call all widgets and
    //construct the list of the ones that need data.
    std::vector<WidgetBase*> polygonalized_widgets;
    polygonalized_widgets.push_back(&map_widget);
    polygonalized_widgets.push_back(&graph_widget);

    // Every widget drawn over the video, generically -- used by the click-to-pause check below
    // so a click on any of them (map, date/time, climb rate, either scrubber) doesn't also toggle
    // pause. The distance-flown text is intentionally NOT in here: it isn't a tracked widget, so
    // a click there falls through and does pause/unpause the video.
    std::vector<WidgetBase*> all_widgets;
    all_widgets.push_back(&date_time_widget);
    all_widgets.push_back(&telemetry_scrub_widget);
    all_widgets.push_back(&map_widget);
    all_widgets.push_back(&climb_widget);
    all_widgets.push_back(&graph_widget);

    TelemetryMgr telemetry_mgr(project_file_mgr.get_telemetry_file_path(), &polygonalized_widgets, &edit_model, project_file_mgr.get_window_start_elapsed(), env_config.user_settings().climb_filter, env_config.user_settings().map_zoom);

    InteractionMgr* interaction_mgr = InteractionMgr::instance();
    // Pause toggle stays owned here for now rather than by a specific subsystem -- see the
    // discussion in the project history for why (nothing else is an obvious owner yet).
    interaction_mgr->bind_key(GLFW_KEY_SPACE,
        []() { paused = !paused; },
        nullptr,
        nullptr);
    // The edit keys. They are named by the character they make, so they follow the user's keyboard
    // layout (see the note on S below), and they stand aside while a text box has the keyboard.
    // Ctrl+S saves the project, and S by itself splits the clip at the playhead.
    interaction_mgr->bind_char('s', GLFW_KEY_S,
        [window, &env_config, &timeline_editor, &edit_model, &media_container_mgr]() {
            if (ImGui::GetIO().WantTextInput)
                return;
            if (ctrl_is_down(window))
                env_config.save_project();
            else
                timeline_editor.split(&edit_model, &media_container_mgr);
        },
        nullptr,
        nullptr);
    interaction_mgr->bind_char('[', GLFW_KEY_LEFT_BRACKET,
        [&timeline_editor, &edit_model, &media_container_mgr]() {
            if (!ImGui::GetIO().WantTextInput)
                timeline_editor.trim_left(&edit_model, &media_container_mgr);
        },
        nullptr,
        nullptr);
    interaction_mgr->bind_char(']', GLFW_KEY_RIGHT_BRACKET,
        [&timeline_editor, &edit_model, &media_container_mgr]() {
            if (!ImGui::GetIO().WantTextInput)
                timeline_editor.trim_right(&edit_model, &media_container_mgr);
        },
        nullptr,
        nullptr);
    interaction_mgr->bind_key(GLFW_KEY_DELETE,
        [&timeline_editor, &edit_model, &media_container_mgr]() {
            if (!ImGui::GetIO().WantTextInput)
                timeline_editor.remove_clip(&edit_model, &media_container_mgr);
        },
        nullptr,
        nullptr);
    // Ctrl+Z undoes (with Shift, redoes); Ctrl+Y redoes.
    interaction_mgr->bind_char('z', GLFW_KEY_Z,
        [window, &timeline_editor, &edit_model, &media_container_mgr]() {
            if (ImGui::GetIO().WantTextInput || !ctrl_is_down(window))
                return;
            if (shift_is_down(window))
                timeline_editor.redo(&edit_model, &media_container_mgr);
            else
                timeline_editor.undo(&edit_model, &media_container_mgr);
        },
        nullptr,
        nullptr);
    interaction_mgr->bind_char('y', GLFW_KEY_Y,
        [window, &timeline_editor, &edit_model, &media_container_mgr]() {
            if (!ImGui::GetIO().WantTextInput && ctrl_is_down(window))
                timeline_editor.redo(&edit_model, &media_container_mgr);
        },
        nullptr,
        nullptr);

    float frame_time = ffsw::elapsed();
    float prev_frame_time = frame_time;
    float duration_avg = -1.0;
    bool show_about_popup = false;
    // Switching projects (File > New, Open, Recent, Close) works by shutting this copy of CamBot down
    // and starting another that opens the chosen project, as so much of the program is built around the
    // one project it was started with. These say that is what's happening, and which project is next.
    bool switching_project = false;
    std::string next_project_path;   // Empty: start the usual way (which, after Close, asks).
    bool show_shortcuts_window = false;
    std::string window_title;          // What the title bar currently says (see the render loop).
    // The Climb Rate Settings window (View menu): open flag, plus a request for the window to
    // reload its sliders from the live settings the next time it draws.
    bool show_climb_settings = false;
    bool climb_settings_resync = false;
    bool show_map_settings = false;
    bool map_settings_resync = false;
    bool show_altitude_settings = false;
    bool altitude_settings_resync = false;
    bool show_summary_settings = false;
    bool summary_settings_resync = false;
    // Set by the two "Save ... Frame" menu items below; consumed (and cleared) at the exact
    // point in this same frame's render sequence that produces the right pixels -- see the two
    // save_frame_as_png() call sites further down.
    std::string pending_raw_screenshot_path;
    std::string pending_overlay_screenshot_path;

    // Export (what used to be the 'R' key) state. was_recording lets us notice the instant
    // an export stops -- either finishing normally at end-of-video or being cancelled with
    // Escape (see MediaContainerMgr's constructor) -- so we can put up the "done" message.
    // export_display_name is just the chosen file's own name, not the full path, for that
    // message and the in-progress one. The ETA is estimated from how much of the export has
    // gone by, wall-clock, since it started, versus how much video is left to encode.
    bool was_recording = false;
    bool show_export_done = false;
    std::string export_display_name;
    float export_start_wallclock = 0.0f;
    float export_start_video_elapsed = 0.0f;
    // Export runs flat-out rather than redrawing every frame -- see do_present below -- but
    // still needs a full render + readback of every single frame to encode it, so this buffer
    // is allocated once up front instead of new[]/delete[]-ing it every frame.
    uint8_t* export_frame_buf = new uint8_t[SCR_WIDTH * SCR_HEIGHT * 4];
    // Throttles how often we actually compose/present a frame to the screen while exporting
    // (the encode itself never skips a frame -- see the recording block below). Not used at
    // all outside of export, where we present every frame as always.
    float last_present_time = ffsw::elapsed();

    // Where the parts of the window go, worked out afresh each frame from the window's size (see
    // window_layout.h). The preview shows the whole canvas, scaled to fit with its shape kept.
    const float canvas_aspect = (float)SCR_WIDTH / (float)(SCR_HEIGHT + UI_HEIGHT);
    float menu_height = (float)MENU_HEIGHT;
    float timeline_height = (float)window_state.timeline_height;   // What the user asked for; the layout may hold it to less.
    float panel_width = (float)window_state.panel_width;          // Likewise: the least the left panel should be.
    WindowLayout layout = window_layout::compute((float)start_width, (float)start_height, menu_height, canvas_aspect, panel_width, timeline_height);

    // render loop
    // -----------
    while (!switching_project && !exit_confirmed(window, env_config, project_file_mgr))
    {
        telemetry_mgr.tick();
        env_config.poll_project();

        // The title bar shows the project and a * while it has unsaved changes.
        std::string wanted_title = "CamBot - " + fileio::base_name(project_file_mgr.get_project_file_path()) +
            (env_config.project_unsaved() ? " *" : "");
        if (wanted_title != window_title) {
            window_title = wanted_title;
            glfwSetWindowTitle(window, window_title.c_str());
        }

        // Lay out the window, and tell the mouse handling where the canvas is shown so the cursor
        // can be turned into canvas coordinates. A minimized window keeps its last layout.
        {
            int window_width = 0, window_height = 0;
            glfwGetWindowSize(window, &window_width, &window_height);
            bool minimized = glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0;
            bool maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0;
            if (!minimized && window_width > 0 && window_height > 0) {
                layout = window_layout::compute((float)window_width, (float)window_height, menu_height, canvas_aspect, panel_width, timeline_height);
                timeline_height = layout.timeline.h;   // So a drag past a limit doesn't wind up.
                // What to remember for next time: the normal size, and whether it was maximized.
                if (!maximized) {
                    window_state.width = window_width;
                    window_state.height = window_height;
                }
                window_state.maximized = maximized;
            }
            interaction_mgr->set_canvas_rect(layout.preview.x, layout.preview.y, layout.preview.w, layout.preview.h);
        }

        interaction_mgr->tick(window);
        //TODO(P1) Find a way for these things to get their updates automatically, so I don't have to remember
        //         to do it for each widget that receives input.
        telemetry_scrub_widget.handle_input();

        // Notice the instant an export stops -- normal completion at end-of-video and an
        // Escape-cancel (handled above, inside interaction_mgr->tick()) both just drop
        // recording() back to false, so either way this is where we catch it and put up the
        // "done" message. The reverse edge is caught down in the File menu below, right where
        // the user actually starts one.
        bool now_recording = media_container_mgr.recording();
        if (!now_recording && was_recording) {
            show_export_done = true;
        }
        was_recording = now_recording;

        // While exporting, don't bother presenting every single frame to the screen -- the
        // encode below still happens every frame regardless, this just throttles how often
        // we redraw the window/menu bar/export status text, since nobody needs to watch it
        // tick by frame-by-frame and skipping most of those redraws is a meaningful chunk of
        // export time back. Outside of export we present every frame, same as always.
        bool do_present = true;
        if (now_recording) {
            float now = ffsw::elapsed();
            do_present = (now - last_present_time) >= 1.0f;
            if (do_present) {
                last_present_time = now;
            }
        }

        // Start the ImGui frame and draw the File/Edit/View/Help menu bar. Skipped on a
        // throttled-away frame -- see do_present above -- along with the matching Render()/
        // RenderDrawData()/SwapBuffers() further down; ImGui requires NewFrame() and Render()
        // to be called in pairs, so these must always be skipped or run together.
        if (do_present) {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            menu_height = ImGui::GetFrameHeight();

            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    // Switching to another project: first see to this one's unsaved changes, then
                    // this copy of CamBot closes and the next one opens the project. None of it while
                    // an export is running, as that needs the video to itself.
                    const bool can_switch = !media_container_mgr.recording();
                    if (ImGui::MenuItem("New Project...", nullptr, false, can_switch)) {
                        if (confirm_leaving_project(env_config, project_file_mgr)) {
                            ProjectFileManager new_project;
                            if (new_project.create_new_interactive()) {
                                next_project_path = new_project.get_project_file_path();
                                switching_project = true;
                            }
                        }
                    }
                    if (ImGui::MenuItem("Open Project...", nullptr, false, can_switch)) {
                        std::string chosen = ffsw::file_dialog(L"ffsw", L"Open CamBot Project (*.ffsw)", true);
                        if (!chosen.empty() && confirm_leaving_project(env_config, project_file_mgr)) {
                            next_project_path = chosen;
                            switching_project = true;
                        }
                    }
                    if (ImGui::BeginMenu("Recent Projects", can_switch)) {
                        const RecentProjects& recent = env_config.user_settings().recent_projects;
                        if (recent.count() == 0)
                            ImGui::MenuItem("(none yet)", nullptr, false, false);
                        for (int i = 0; i < recent.count(); i++) {
                            const std::string& recent_path = recent.path(i);
                            const bool is_open = RecentProjects::same_path(recent_path, project_file_mgr.get_project_file_path());
                            const bool exists = fileio::file_exists(recent_path);
                            // Numbered so that two projects with the same file name in different folders can be told apart.
                            const std::string label = std::to_string(i + 1) + "  " + fileio::base_name(recent_path) + (exists ? "" : "  (missing)");
                            if (ImGui::MenuItem(label.c_str(), nullptr, is_open, exists && !is_open) &&
                                confirm_leaving_project(env_config, project_file_mgr)) {
                                next_project_path = recent_path;
                                switching_project = true;
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                ImGui::SetTooltip("%s", recent_path.c_str());
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Close Project", nullptr, false, can_switch)) {
                        if (confirm_leaving_project(env_config, project_file_mgr)) {
                            env_config.forget_last_project();   // So the next start asks instead of reopening it.
                            next_project_path.clear();
                            switching_project = true;
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Save Project", "Ctrl+S")) {
                        // Goes through EnvConfig, not project_file_mgr directly -- that's
                        // what actually pulls the *current* rotation (and telemetry offset,
                        // launch time, etc.) out of their live owners first. Calling
                        // project_file_mgr.save_project() straight would just re-write
                        // whatever was last pushed into it, which for anything only changed
                        // via a nudge/held key (rotation included) could be stale.
                        EnvConfig::instance->save_project();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Save Raw Video Frame...")) {
                        pending_raw_screenshot_path = ffsw::file_dialog(L"png", L"Save Raw Video Frame As (*.png)", false);
                    }
                    if (ImGui::MenuItem("Save Frame with Telemetry Overlay...")) {
                        pending_overlay_screenshot_path = ffsw::file_dialog(L"png", L"Save Frame with Telemetry Overlay As (*.png)", false);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Export Video...", nullptr, false, !media_container_mgr.recording())) {
                        std::string export_path = ffsw::file_dialog(L"mp4", L"Export Video As (*.mp4)", false);
                        if (!export_path.empty()) {
                            size_t slash = export_path.find_last_of("/\\");
                            export_display_name = (slash == std::string::npos) ? export_path : export_path.substr(slash + 1);
                            export_start_wallclock = ffsw::elapsed();
                            // init_video_output() always seeks the video back to the very
                            // beginning before it does anything else -- export covers the
                            // whole input, not just from wherever playback was -- so the
                            // ETA math below always has 0.0 to measure progress against,
                            // not whatever in_elapsed() reads right this moment (before
                            // that seek happens).
                            export_start_video_elapsed = 0.0f;
                            show_export_done = false;
                            if (!media_container_mgr.init_video_output(export_path, SCR_WIDTH, SCR_HEIGHT)) {
                                ffsw::message_box("CamBot - Cannot Export", media_container_mgr.last_error());
                            }
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Exit")) {
                        media_container_mgr.finalize_output();
                        glfwSetWindowShouldClose(window, true);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit")) {
                    if (ImGui::MenuItem("Mark Launch Point")) {
                        env_config.launch_time(env_config.flight_elapsed());
                    }
                    ImGui::Separator();
                    // The same turns the keys bound in MediaContainerMgr's constructor make; the
                    // shortcuts shown are letters, which follow the user's keyboard layout.
                    if (ImGui::MenuItem("Rotate 180 Degrees", "F")) {
                        media_container_mgr.rotate_by(3.141592653f);
                    }
                    if (ImGui::MenuItem("Rotate Left a Little", ",")) {
                        media_container_mgr.rotate_by(3.141592653f / 720.0f);
                    }
                    if (ImGui::MenuItem("Rotate Right a Little", ".")) {
                        media_container_mgr.rotate_by(-3.141592653f / 720.0f);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View")) {
                    if (ImGui::MenuItem("Climb Rate Settings...")) {
                        show_climb_settings = true;
                        climb_settings_resync = true;
                    }
                    if (ImGui::MenuItem("Map Settings...")) {
                        show_map_settings = true;
                        map_settings_resync = true;
                    }
                    if (ImGui::MenuItem("Altitude Reference...")) {
                        show_altitude_settings = true;
                        altitude_settings_resync = true;
                    }
                    if (ImGui::MenuItem("Flight Summary...")) {
                        show_summary_settings = true;
                        summary_settings_resync = true;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help")) {
                    if (ImGui::MenuItem("Keyboard Shortcuts...")) {
                        show_shortcuts_window = true;
                    }
                    if (ImGui::MenuItem("About CamBot")) {
                        show_about_popup = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }

            draw_left_panel(layout.left_panel);
            draw_panel_splitter(layout.panel_splitter, layout.widest_panel, &panel_width);
            draw_splitter(layout.splitter, &timeline_height);
            timeline_view.draw(layout.timeline, edit_model, media_container_mgr, timeline_editor);
            draw_transport_bar(layout.transport, media_container_mgr, &pending_raw_screenshot_path, &pending_overlay_screenshot_path);
            // The preview is the canvas, shown from ImGui's background list rather than in a window of
            // its own, so ImGui doesn't count the mouse as being over a window of its own when it is
            // over the picture (the widgets drawn in the canvas take the clicks there). The canvas
            // texture has row 0 at the bottom, hence the flipped texture coordinates.
            if (layout.preview.w > 0.0f && layout.preview.h > 0.0f) {
                ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(uintptr_t)canvas.texture(),
                    ImVec2(layout.preview.x, layout.preview.y),
                    ImVec2(layout.preview.x + layout.preview.w, layout.preview.y + layout.preview.h),
                    ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            }

            if (show_about_popup) {
                ImGui::OpenPopup("About CamBot");
                show_about_popup = false;
            }
            if (ImGui::BeginPopupModal("About CamBot", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("CamBot %s", CAMBOT_VERSION_STRING);
                ImGui::Separator();
                ImGui::TextWrapped("Overlays custom flight telemetry onto GoPro video for glider flying.");
                ImGui::Spacing();
                if (ImGui::Button("Close")) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // Re-bases the barometer on the altitude references whenever they (or where the video
            // falls in the telemetry) change; a no-op the rest of the time.
            env_config.sync_altitude_correction();

            if (show_climb_settings) {
                draw_climb_settings_window(&show_climb_settings, &climb_settings_resync, media_container_mgr.recording());
            }

            if (show_map_settings) {
                draw_map_settings_window(&show_map_settings, &map_settings_resync, media_container_mgr.recording());
            }

            if (show_altitude_settings) {
                draw_altitude_window(&show_altitude_settings, &altitude_settings_resync, media_container_mgr.recording());
            }

            if (show_summary_settings) {
                draw_summary_settings_window(&show_summary_settings, &summary_settings_resync, media_container_mgr.recording());
            }

            draw_shortcuts_window(&show_shortcuts_window);

            // Export status, bottom-center. Drawn through ImGui -- rather than font_manager,
            // like the other overlays -- specifically because ImGui's own render pass happens
            // after the capture point below, so unlike the telemetry overlays, this text never
            // ends up baked into the exported video itself.
            if (media_container_mgr.recording() || show_export_done) {
                std::string status_text;
                if (media_container_mgr.recording()) {
                    float elapsed_wallclock = ffsw::elapsed() - export_start_wallclock;
                    float video_done = media_container_mgr.in_elapsed() - export_start_video_elapsed;
                    float video_remaining = media_container_mgr.in_duration() - media_container_mgr.in_elapsed();
                    // Seconds of wall-clock per second of video encoded so far, applied to
                    // what's left -- self-corrects as encoding speed varies instead of
                    // assuming a fixed rate.
                    float eta_seconds = (video_done > 0.0f) ? (elapsed_wallclock / video_done) * video_remaining : 0.0f;
                    int eta_minutes = (int)(eta_seconds / 60.0f);
                    int eta_whole_seconds = (int)eta_seconds % 60;
                    status_text = ffsw::format("Exporting %s. ETA %d:%02d", export_display_name.c_str(), eta_minutes, eta_whole_seconds);
                } else {
                    status_text = ffsw::format("Export done: %s", export_display_name.c_str());
                }

                ImGui::SetNextWindowPos(ImVec2(layout.preview.x + layout.preview.w / 2.0f, layout.preview.y + layout.preview.h - 12.0f),
                    ImGuiCond_Always, ImVec2(0.5f, 1.0f));
                ImGui::SetNextWindowBgAlpha(0.0f);
                ImGui::Begin("ExportStatus", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
                ImGui::TextUnformatted(status_text.c_str());
                ImGui::End();
            }
        }

        // Any click on the video itself that doesn't land on one of the widgets drawn over top of
        // it toggles pause (see all_widgets above for which widgets count, and why the
        // distance-flown text deliberately doesn't). ImGui already claims the click instead of us
        // whenever it wants the mouse (e.g. the menu bar), via InteractionMgr::tick().
        if (interaction_mgr->mouse_button_down()) {
            float mouse_x = interaction_mgr->mouse_x_pos();
            float mouse_y = interaction_mgr->mouse_y_pos();
            bool in_video_rect = mouse_x >= -1.0f && mouse_x <= 1.0f && mouse_y >= bottom && mouse_y <= 1.0f;
            if (in_video_rect) {
                bool on_a_widget = false;
                for (WidgetBase* widget : all_widgets) {
                    if (widget->contains(mouse_x, mouse_y)) {
                        on_a_widget = true;
                        break;
                    }
                }
                if (!on_a_widget) {
                    paused = !paused;
                }
            }
        }

        // Let the font manager know that it's time to clear out expired strings:
        font_manager.update_time(media_container_mgr.get_presentation_timestamp());

        float x = interaction_mgr->mouse_x_pos();
        float y = interaction_mgr->mouse_y_pos();
        /*
        font_manager.add_string(
            StringAndProperties(
                ffsw::format("(%.3f,%.3f)", x, y),
                0, glm::vec2(x, y), glm::vec3(1.0, 1.0, 1.0), 1.0, 2.0,
                StringAndProperties::V_ALIGN::V_CENTER, StringAndProperties::H_ALIGN::H_CENTER));
        */
        font_manager.add_string(
            StringAndProperties(
                ffsw::format("%6.2fkm", env_config.telemetry_distance_flown()),
                0, glm::vec2(-.99, .99), glm::vec3(1.0, 1.0, 1.0), 1.0, 2.0,
                StringAndProperties::V_ALIGN::V_TOP, StringAndProperties::H_ALIGN::H_LEFT));

        frame_time = ffsw::elapsed();
        float duration = frame_time - prev_frame_time;
        prev_frame_time = frame_time;
        if (duration_avg < 0.0) {
            duration_avg = duration;
        }
        else {
            duration_avg = 0.95f * duration_avg + 0.05f * duration;
        }

        // render
        // ------
        // Everything is drawn into the off-screen canvas; it reaches the window further down.
        // Its viewport is the whole canvas, and the MENU_HEIGHT strip of the window above it,
        // where the ImGui menu bar goes, is never part of it. That keeps every existing NDC-based
        // widget/video coordinate unchanged, and keeps the menu bar out of the pixels captured
        // below for recording. ImGui's own render pass sets its own full-window viewport and
        // restores this one afterward, so it must be reset here, every frame.
        canvas.bind();
        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT + UI_HEIGHT);
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        media_container_mgr.render();
        save_frame_as_png(canvas, pending_raw_screenshot_path);
        pending_raw_screenshot_path.clear();
        date_time_widget.render();
        telemetry_scrub_widget.render();
        map_widget.render();
        climb_widget.render();
        graph_widget.render();
        flight_summary_overlay.render();
        font_manager.render();
        save_frame_as_png(canvas, pending_overlay_screenshot_path);
        pending_overlay_screenshot_path.clear();

        // Captured right here -- after every telemetry overlay above, before any of the
        // ImGui chrome below -- so the exported frame has exactly the baked-in overlays and
        // none of the menu bar/export status text. Runs every single frame regardless of
        // do_present, since every frame has to make it into the encoded video; export_frame_buf
        // is allocated once, outside the loop, rather than every frame.
        if (media_container_mgr.recording()) {
            canvas.read_rgb(0, UI_HEIGHT, SCR_WIDTH, SCR_HEIGHT, export_frame_buf);
            media_container_mgr.output_video_frame(export_frame_buf);
        }

        // The window is only drawn to on frames that are presented. All of it is ImGui's: the
        // canvas is in the preview, the panels and the menu bar are around it.
        canvas.make_opaque();
        RenderTarget::bind_window();

        // Paired with the ImGui::NewFrame()/menu-bar block up top -- see do_present there.
        if (do_present) {
            int framebuffer_width = 0, framebuffer_height = 0;
            glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
            glViewport(0, 0, framebuffer_width, framebuffer_height);
            glClearColor(0.10f, 0.10f, 0.11f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }

        // glfw: poll IO events (keys pressed/released, mouse moved etc.) -- always, even on a
        // throttled-away frame, so Escape-to-cancel-export stays instant.
        // -------------------------------------------------------------------------------
        glfwPollEvents();

        // Exporting runs flat-out regardless of whatever `paused` happened to be set to when
        // it started -- it isn't "playback", so pause doesn't apply to it. Either way, running
        // off the end of the video no longer closes the app -- it just stops advancing on the
        // last frame, same as an ordinary pause (and, if this was an export, advance_frame()
        // already finalized the output file itself -- see its AVERROR_EOF handling).
        if (!paused || media_container_mgr.recording()) {
            if (!media_container_mgr.advance_frame()) {
                paused = true;
            }
        }
    }

    window_state.timeline_height = (int)timeline_height;
    window_state.panel_width = (int)panel_width;
    env_config.window_settings(window_state);
    env_config.save_user_settings();
    env_config.discard_recovery();   // Leaving normally: saved, or the user chose not to.
    delete[] export_frame_buf;
    canvas.destroy();   // Needs the OpenGL context, which glfwTerminate() ends.

    // Dear ImGui: tear down the backends and context.
    // -------------------------------------------------
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    // glfw: terminate, clearing all previously allocated GLFW resources.
    // ------------------------------------------------------------------
    glfwTerminate();

    // Last of all, so that nothing of this copy is still using the files: the copy that opens the next project.
    if (switching_project && !ffsw::launch_self(next_project_path))
        ffsw::message_box("CamBot", "CamBot could not start itself to open the other project. Start it again by hand.");
    return 0;
}
