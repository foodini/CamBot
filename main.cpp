#include <chrono>
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
const unsigned int SCR_WIDTH = 1920 / 2;
const unsigned int SCR_HEIGHT = 1080 / 2;
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
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT+UI_HEIGHT, "LearnOpenGL", NULL, NULL);
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
    extents[1] = glm::vec3( 1.0, bottom, 0.0);
    extents[2] = glm::vec3(-1.0, 1.0, 0.0);
    extents[3] = glm::vec3( 1.0, 1.0, 0.0);
    MediaContainerMgr media_container_mgr("c:\\Users\\foodi\\Videos\\VIRB\\VIRB_0347.mp4", 0, "3.3.shader.vert", "3.3.shader.frag", extents);
    FontManager font_manager("c:\\Windows\\Fonts\\courbd.ttf", 48, SCR_WIDTH, SCR_HEIGHT + UI_HEIGHT);
    TelemetryMgr telemetry_mgr("11222122.log");
    //TODO(P1) Hand the telemetry_mgr, instead of its vector, into the env_config.
    EnvConfig env_config(media_container_mgr, telemetry_mgr, font_manager, (float)SCR_WIDTH, (float)SCR_HEIGHT + (float)UI_HEIGHT, (float)UI_HEIGHT);
    InteractionMgr* interaction_mgr = InteractionMgr::instance();
    interaction_mgr->watch_key(GLFW_KEY_ESCAPE);
    interaction_mgr->watch_key(GLFW_KEY_SPACE);
    interaction_mgr->watch_key(GLFW_KEY_LEFT);
    interaction_mgr->watch_key(GLFW_KEY_RIGHT);
    interaction_mgr->watch_key(GLFW_KEY_UP);
    interaction_mgr->watch_key(GLFW_KEY_DOWN);
    interaction_mgr->watch_key(GLFW_KEY_L);

    DateTimeWidget date_time_widget(140.0, 56.0, SCR_WIDTH - 145.0, SCR_HEIGHT + UI_HEIGHT - 61.0);
    MediaScrubWidget media_scrub_widget(SCR_WIDTH - 20.0, 20.0, 10.0, UI_HEIGHT - 5.0 - 20.0);
    MapWidget map_widget(175.0, 175.0, SCR_WIDTH - 175.0 - 5.0, UI_HEIGHT + 5.0);

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
            break;
        }
        if (interaction_mgr->key_down(GLFW_KEY_SPACE))
            paused = !paused;
        if (interaction_mgr->key_down(GLFW_KEY_RIGHT) || interaction_mgr->key_held(GLFW_KEY_RIGHT) >= 0.25)
            fwd = true;
        if (interaction_mgr->key_down(GLFW_KEY_UP) || interaction_mgr->key_held(GLFW_KEY_UP) >= 0.25)
            env_config.telemetry_offset(env_config.telemetry_offset() + (paused ? 0.1f : 10.0f));
        if (interaction_mgr->key_down(GLFW_KEY_LEFT) || interaction_mgr->key_held(GLFW_KEY_LEFT) >= 0.25)
            rev = true;
        if (interaction_mgr->key_down(GLFW_KEY_DOWN) || interaction_mgr->key_held(GLFW_KEY_DOWN) >= 0.25)
            env_config.telemetry_offset(env_config.telemetry_offset() - (paused ? 0.1f : 10.0f));
        if (interaction_mgr->key_down(GLFW_KEY_L)) {
            if (confirming_launch) {
                confirming_launch = false;
                env_config.launch_time(env_config.media_in_elapsed());
            } else {
                paused = true;
                confirming_launch = true;
            }
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
        font_manager.render();

        // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
        // -------------------------------------------------------------------------------
        glfwSwapBuffers(window);
        glfwPollEvents();

        //std::this_thread::sleep_for(std::chrono::milliseconds(0));

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
