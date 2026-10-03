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
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT + UI_HEIGHT + MENU_HEIGHT, "LearnOpenGL", NULL, NULL);
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

    TelemetryMgr telemetry_mgr(project_file_mgr.get_telemetry_file_path(), &polygonalized_widgets, project_file_mgr.get_telemetry_offset(), project_file_mgr.get_window_start_elapsed());

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
    // Set by the two "Save ... Frame" menu items below; consumed (and cleared) at the exact
    // point in this same frame's render sequence that produces the right pixels -- see the two
    // save_frame_as_png() call sites further down.
    std::string pending_raw_screenshot_path;
    std::string pending_overlay_screenshot_path;
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

        // Start the ImGui frame and draw the File/Edit/View/Help menu bar.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save Project")) {
                    project_file_mgr.save_project();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save Raw Video Frame...")) {
                    pending_raw_screenshot_path = ffsw::file_dialog(L"png", L"Save Raw Video Frame As (*.png)", false);
                }
                if (ImGui::MenuItem("Save Frame with Telemetry Overlay...")) {
                    pending_overlay_screenshot_path = ffsw::file_dialog(L"png", L"Save Frame with Telemetry Overlay As (*.png)", false);
                }
                ImGui::Separator();
                // Not wired up yet -- placeholder until the overlays are where we want them. See
                // MediaContainerMgr::init_video_output()/output_video_frame().
                ImGui::MenuItem("Start Recording (coming soon)", nullptr, false, false);
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
                ImGui::MenuItem("(nothing here yet)", nullptr, false, false);
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
            ImGui::Text("CamBot");
            ImGui::Separator();
            ImGui::TextWrapped("Overlays custom flight telemetry onto GoPro video for glider flying.");
            ImGui::Spacing();
            if (ImGui::Button("Close")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
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
        
        font_manager.add_string(
            StringAndProperties(
                ffsw::format("tel_ind: %d", env_config.telemetry_index()),
                0, glm::vec2(-.98, -.98), glm::vec3(1.0, 1.0, 1.0), 1.0, 3.0));

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

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
        // -------------------------------------------------------------------------------
        glfwSwapBuffers(window);
        if (media_container_mgr.recording()) {
            uint8_t* buf = new uint8_t[SCR_WIDTH * SCR_HEIGHT * 4];
            //std::memset(buf, 0, SCR_WIDTH * SCR_HEIGHT * 3);
            glReadPixels(0, UI_HEIGHT, SCR_WIDTH, SCR_HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, (void*)buf);
            media_container_mgr.output_video_frame(buf);
            delete[] buf;
        }
        glfwPollEvents();

        if (!paused) {
            if (!media_container_mgr.advance_frame()) {
                break;
            }
        }
    }

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
