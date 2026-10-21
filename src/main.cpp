#include "version.h"
#include <algorithm>
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
#include "font_manager.h"
#include "interaction_manager.h"
#include "lines.h"
#include "project_file_manager.h"
#include "media_container_manager.h"
#include "telemetry.h"
#include "util.h"
#include "widget.h"

//TODO(P1): remove the std::thread use. You can't terminate a running thread with std::thread, and I need
//          to be able to kill the parse thread if it's still running when the program terminates. Also,
//          I just want the thing to go away when it finishes. I'd rather not have to join it.

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow* window);

// settings
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;
const unsigned int UI_HEIGHT = 134;        // Divisible by FOUR, RIGHT?
const unsigned int MENU_HEIGHT = 19;       // Exact pixel height ImGui renders the main menu bar at with
                                            // the default font/style: FontSize (13) + FramePadding.y (3) * 2.
                                            // Kept separate from SCR_HEIGHT/UI_HEIGHT so the menu never
                                            // ends up inside the region read back for recording. If a
                                            // custom font or DPI scaling is ever added, this needs to
                                            // change to match, or a gap/overlap will reappear here.

bool paused = false;

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
// capture below, both of which read this same rectangle) -- from whatever's currently in the
// back buffer, and writes it out as a PNG at `path`. No-op if `path` is empty, so callers can
// just call this unconditionally once per frame and let it decide. Called at two different
// points in the same frame's render sequence (see the call sites in the render loop below) so
// each capture sees exactly the pixels it's supposed to: right after the video renders (raw, no
// overlay yet) or right after every widget/text has (telemetry overlay baked in).
static void save_frame_as_png(const std::string& path) {
    if (path.empty())
        return;
    std::vector<uint8_t> buf((size_t)SCR_WIDTH * SCR_HEIGHT * 3);
    glReadPixels(0, UI_HEIGHT, SCR_WIDTH, SCR_HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
    // glReadPixels' first row is the bottom of the image; PNG (and stb_image_write) expect the
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
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT + UI_HEIGHT + MENU_HEIGHT, "CamBot", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
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

    MediaContainerMgr media_container_mgr(project_file_mgr.get_video_file_path(), "3.3.shader.vert", "3.3.shader.frag", extents);
    // Restore whatever rotation was saved with this project (see EnvConfig::save_project()
    // and the "Save Project" menu item below for where it's written back out).
    media_container_mgr.rotation_angle(project_file_mgr.get_rotation_angle());
    FontManager font_manager("c:\\Windows\\Fonts\\courbd.ttf", 48, SCR_WIDTH, SCR_HEIGHT + UI_HEIGHT);
    //TODO(P1) Hand the telemetry_mgr, instead of its vector, into the env_config.
    EnvConfig env_config(&media_container_mgr, &font_manager, &project_file_mgr, (float)SCR_WIDTH, (float)SCR_HEIGHT + (float)UI_HEIGHT, (float)UI_HEIGHT);

    DateTimeWidget date_time_widget(.29f, .18f, 1.0f-.29f-.015f, 1.0f-.18f-.01f);
    MediaScrubWidget media_scrub_widget(1.96f, 0.04f, -.98f, -.835f);
    TelemetryScrubWidget telemetry_scrub_widget(1.96f, 0.04f, -.98f, -.885f);

    MapWidget map_widget(.35f, .35f*SCR_WIDTH/(SCR_HEIGHT+UI_HEIGHT), 0.6f, -0.765f);
    ClimbWidget climb_widget(.02f, .35f * SCR_WIDTH / (SCR_HEIGHT + UI_HEIGHT), 0.9625f, -0.765f);
    GraphWidget graph_widget(.5f, .3f, -0.99f, -1.0f + .24f);
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
    all_widgets.push_back(&media_scrub_widget);
    all_widgets.push_back(&telemetry_scrub_widget);
    all_widgets.push_back(&map_widget);
    all_widgets.push_back(&climb_widget);
    all_widgets.push_back(&graph_widget);

    TelemetryMgr telemetry_mgr(project_file_mgr.get_telemetry_file_path(), &polygonalized_widgets, project_file_mgr.get_telemetry_offset(), project_file_mgr.get_window_start_elapsed(), env_config.user_settings().climb_filter, env_config.user_settings().map_zoom);

    InteractionMgr* interaction_mgr = InteractionMgr::instance();
    // Pause toggle stays owned here for now rather than by a specific subsystem -- see the
    // discussion in the project history for why (nothing else is an obvious owner yet).
    interaction_mgr->bind_key(GLFW_KEY_SPACE,
        []() { paused = !paused; },
        nullptr,
        nullptr);

    float frame_time = ffsw::elapsed();
    float prev_frame_time = frame_time;
    float duration_avg = -1.0;
    bool show_about_popup = false;
    // The Climb Rate Settings window (View menu): open flag, plus a request for the window to
    // reload its sliders from the live settings the next time it draws.
    bool show_climb_settings = false;
    bool climb_settings_resync = false;
    bool show_map_settings = false;
    bool map_settings_resync = false;
    bool show_altitude_settings = false;
    bool altitude_settings_resync = false;
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

    // render loop
    // -----------
    while (!glfwWindowShouldClose(window))
    {
        telemetry_mgr.tick();
        interaction_mgr->tick(window);
        //TODO(P1) Find a way for these things to get their updates automatically, so I don't have to remember
        //         to do it for each widget that receives input.
        media_scrub_widget.handle_input();
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

            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Save Project")) {
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
                            media_container_mgr.init_video_output(export_path, SCR_WIDTH, SCR_HEIGHT);
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
                        env_config.launch_time(env_config.media_in_elapsed());
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
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help")) {
                    if (ImGui::MenuItem("About CamBot")) {
                        show_about_popup = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
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

                ImGui::SetNextWindowPos(ImVec2(SCR_WIDTH / 2.0f, (float)(SCR_HEIGHT + UI_HEIGHT) - 12.0f),
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
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Restrict the 3D scene (video + widgets) to the original SCR_HEIGHT+UI_HEIGHT canvas at
        // the bottom of the window, leaving the MENU_HEIGHT strip above it untouched for the ImGui
        // menu bar. This keeps every existing NDC-based widget/video coordinate unchanged, and keeps
        // the menu bar out of the pixels captured below for recording. ImGui's own render pass sets
        // its own full-window viewport and restores this one afterward, so it must be reset here,
        // every frame.
        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT + UI_HEIGHT);

        media_container_mgr.render();
        save_frame_as_png(pending_raw_screenshot_path);
        pending_raw_screenshot_path.clear();
        date_time_widget.render();
        media_scrub_widget.render();
        telemetry_scrub_widget.render();
        map_widget.render();
        climb_widget.render();
        graph_widget.render();
        font_manager.render();
        save_frame_as_png(pending_overlay_screenshot_path);
        pending_overlay_screenshot_path.clear();

        // Captured right here -- after every telemetry overlay above, before any of the
        // ImGui chrome below -- so the exported frame has exactly the baked-in overlays and
        // none of the menu bar/export status text. Runs every single frame regardless of
        // do_present, since every frame has to make it into the encoded video; export_frame_buf
        // is allocated once, outside the loop, rather than every frame.
        if (media_container_mgr.recording()) {
            glReadPixels(0, UI_HEIGHT, SCR_WIDTH, SCR_HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, (void*)export_frame_buf);
            media_container_mgr.output_video_frame(export_frame_buf);
        }

        // Paired with the ImGui::NewFrame()/menu-bar block up top -- see do_present there.
        if (do_present) {
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

    delete[] export_frame_buf;

    // Dear ImGui: tear down the backends and context.
    // -------------------------------------------------
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    // glfw: terminate, clearing all previously allocated GLFW resources.
    // ------------------------------------------------------------------
    glfwTerminate();
    return 0;
}
