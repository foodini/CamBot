#include <chrono>
#include <cstring>
#include <stdio.h>
#include <string>
#include <thread>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "glm/glm.hpp"

#include "stb_image.h"

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

bool paused = false;

bool fwd = false;
bool rev = false;

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and 
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);
}

int main()
{
    // glfw: initialize and configure
    // ------------------------------
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // glfw window creation
    // --------------------
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT + UI_HEIGHT, "LearnOpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    //glfwSetKeyCallback(window, &InteractionMgr::key_callback);

    // glad: load all OpenGL function pointers
    // ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    float bottom = (float)(1.0 - 2.0 * SCR_HEIGHT / (SCR_HEIGHT + UI_HEIGHT));
    glm::vec3 extents[4];  //bl, br, tl, tr
    extents[0] = glm::vec3(-1.0, bottom, 0.0);
    extents[1] = glm::vec3(1.0, bottom, 0.0);
    extents[2] = glm::vec3(-1.0, 1.0, 0.0);
    extents[3] = glm::vec3(1.0, 1.0, 0.0);

    ProjectFileManager project_file_mgr = ProjectFileManager();

    MediaContainerMgr media_container_mgr(project_file_mgr.get_video_file_path(), "3.3.shader.vert", "3.3.shader.frag", extents);
    FontManager font_manager("c:\\Windows\\Fonts\\courbd.ttf", 48, SCR_WIDTH, SCR_HEIGHT + UI_HEIGHT);
    //TODO(P1) Hand the telemetry_mgr, instead of its vector, into the env_config.
    EnvConfig env_config(&media_container_mgr, &font_manager, &project_file_mgr, (float)SCR_WIDTH, (float)SCR_HEIGHT + (float)UI_HEIGHT, (float)UI_HEIGHT);

    DateTimeWidget date_time_widget(182.0, 56.0, SCR_WIDTH - 187.0, SCR_HEIGHT + UI_HEIGHT - 61.0);
    MediaScrubWidget media_scrub_widget(SCR_WIDTH - 20.0, 20.0, 10.0, UI_HEIGHT - 5.0 - 20.0);

    MapWidget map_widget(175.0, 175.0, SCR_WIDTH - 25.0 - 5.0 - 175.0 - 5.0, UI_HEIGHT + 5.0);
    ClimbWidget climb_widget(25.0, 175.0, SCR_WIDTH - 25.0 - 5.0, UI_HEIGHT + 5.0);
    GraphWidget graph_widget(300.0, 100.0, 5.0, UI_HEIGHT + 5.0);
    //TODO(P0): This is dumb. Have each widget know whether it needs a poly call. The TelemetryMgr can call all widgets and
    //construct the list of the ones that need data.
    std::vector<WidgetBase*> polygonalized_widgets;
    polygonalized_widgets.push_back(&map_widget);
    polygonalized_widgets.push_back(&graph_widget);

    TelemetryMgr telemetry_mgr(project_file_mgr.get_telemetry_file_path(), &polygonalized_widgets);

    InteractionMgr* interaction_mgr = InteractionMgr::instance();
    interaction_mgr->watch_key(GLFW_KEY_ESCAPE);
    interaction_mgr->watch_key(GLFW_KEY_SPACE);
    interaction_mgr->watch_key(GLFW_KEY_LEFT);
    interaction_mgr->watch_key(GLFW_KEY_RIGHT);
    interaction_mgr->watch_key(GLFW_KEY_UP);
    interaction_mgr->watch_key(GLFW_KEY_DOWN);
    interaction_mgr->watch_key(GLFW_KEY_L);
    interaction_mgr->watch_key(GLFW_KEY_S);
    interaction_mgr->watch_key(GLFW_KEY_R);
    interaction_mgr->watch_key(GLFW_KEY_F);

    float frame_time = ffsw::elapsed();
    float prev_frame_time = frame_time;
    float duration_avg = -1.0;
    bool confirming_launch = false;
    // render loop
    // -----------
    while (!glfwWindowShouldClose(window))
    {
        telemetry_mgr.tick();
        interaction_mgr->tick(window);
        //TODO(P1) Find a way for these things to get their updates automatically, so I don't have to remember
        //         to do it for each widget that receives input.
        media_scrub_widget.handle_input();

        if (interaction_mgr->key_down(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(window, true);
            media_container_mgr.finalize_output();
            break;
        }
        if (interaction_mgr->key_down(GLFW_KEY_SPACE))
            paused = !paused;
        if (interaction_mgr->key_down(GLFW_KEY_RIGHT) || interaction_mgr->key_held(GLFW_KEY_RIGHT) >= 0.25)
            fwd = true;
        if (interaction_mgr->key_down(GLFW_KEY_UP) || interaction_mgr->key_held(GLFW_KEY_UP) >= 0.25)
            env_config.telemetry_offset(env_config.telemetry_offset() - (paused ? 0.1f : 10.0f));
        if (interaction_mgr->key_down(GLFW_KEY_LEFT) || interaction_mgr->key_held(GLFW_KEY_LEFT) >= 0.25)
            rev = true;
        if (interaction_mgr->key_down(GLFW_KEY_DOWN) || interaction_mgr->key_held(GLFW_KEY_DOWN) >= 0.25)
            env_config.telemetry_offset(env_config.telemetry_offset() + (paused ? 0.1f : 10.0f));
        if (interaction_mgr->key_down(GLFW_KEY_F)) {
            media_container_mgr.rotation_angle(media_container_mgr.rotation_angle() + 3.141592653);
        }
        if (interaction_mgr->key_down(GLFW_KEY_L)) {
            if (confirming_launch) {
                confirming_launch = false;
                env_config.launch_time(env_config.media_in_elapsed());
            } else {
                paused = true;
                confirming_launch = true;
            }
        }
        if (interaction_mgr->key_down(GLFW_KEY_S)) {
            uint8_t* buf = new uint8_t[SCR_WIDTH * SCR_HEIGHT * 3];
            //std::memset(buf, 0, SCR_WIDTH * SCR_HEIGHT * 3);
            glReadPixels(0, UI_HEIGHT, SCR_WIDTH, SCR_HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, (void*)buf);
            FILE* ppm_out = fopen("debug.ppm", "wb");
            fprintf(ppm_out, "P6 %d %d 255\n", SCR_WIDTH, SCR_HEIGHT);
            fwrite(buf, 1, SCR_WIDTH * SCR_HEIGHT * 3, ppm_out);
            fclose(ppm_out);
            delete[] buf;
        }
        if (interaction_mgr->key_down(GLFW_KEY_R)) {
            const std::string& video_file_name = 
                   project_file_mgr.get_project_file_path() + ".mp4"; //TODO(P2): trim off the proj's extension.
            media_container_mgr.init_video_output(video_file_name, SCR_WIDTH, SCR_HEIGHT);
        }
        if (fwd) {
            media_container_mgr.advance_by(paused ? 1 : 100); 
            fwd = false;
        }
        if (rev) {
            media_container_mgr.rewind_by(paused ? 1 : 100);
            rev = false;
        }

        // Let the font manager know that it's time to clear out expired strings:
        font_manager.update_time(media_container_mgr.get_presentation_timestamp());

        if (confirming_launch) {
            glm::vec3 yellow(1.0, 0.2, 1.0);
            font_manager.format(0, 0.5, glm::vec2(10.0, 10.0), yellow, 2.0, "Press L again to confirm launch @ this frame");
        }
        float x = interaction_mgr->mouse_x_pos();
        float y = interaction_mgr->mouse_y_pos();
        font_manager.format(0, 1.0, glm::vec2(x, y), glm::vec3(1.0, 1.0, 1.0), 2.0, "(%.1f,%.1f)", x, y);

        frame_time = ffsw::elapsed();
        float duration = frame_time - prev_frame_time;
        prev_frame_time = frame_time;
        if (duration_avg < 0.0) {
            duration_avg = duration;
        }
        else {
            duration_avg = 0.95f * duration_avg + 0.05f * duration;
        }
        
        font_manager.format(0, 1.0, glm::vec2(22.0, 22.0), glm::vec3(1.0, 1.0, 1.0), 3.0, "mph: %9.2lf", telemetry_mgr[env_config.telemetry_index()].speed_mph());
        font_manager.format(0, 1.0, glm::vec2(22.0, 62.0), glm::vec3(1.0, 1.0, 1.0), 3.0, "tel_ind: %d", env_config.telemetry_index());

        // render
        // ------
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        media_container_mgr.render();
        date_time_widget.render();
        media_scrub_widget.render();
        map_widget.render();
        climb_widget.render();
        graph_widget.render();
        font_manager.render();

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

    // glfw: terminate, clearing all previously allocated GLFW resources.
    // ------------------------------------------------------------------
    glfwTerminate();
    return 0;
}
