#include "unige_tactile_sensors_cpp/sensor_serial.hpp"

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <vector>
#include <mutex>
#include <iostream>

SensorSerial *g_sensor = nullptr;
std::vector<double> g_values(8, 0.0);
std::mutex g_mutex;

void sensorCallback(uint32_t ts, uint16_t seq, const std::vector<double> &vals)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_values = vals;
}

int main()
{
    // -------------------------
    // INIT SENSOR
    // -------------------------
    g_sensor = new SensorSerial();
    g_sensor->setDataCallback(sensorCallback);

    // -------------------------
    // INIT GLFW
    // -------------------------
    if (!glfwInit())
    {
        std::cerr << "Failed to init GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Sensor GUI", NULL, NULL);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // -------------------------
    // INIT IMGUI + IMPLOT
    // -------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    std::vector<double> plot_vals(1000, 0.0);
    int plot_index = 0;

    // -------------------------
    // MAIN GUI LOOP
    // -------------------------
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Window
        ImGui::Begin("Live Sensor Values");

        {
            std::lock_guard<std::mutex> lock(g_mutex);

            // Print numbers
            for (int i = 0; i < g_values.size(); i++)
            {
                ImGui::Text("Sensor %d: %.3f", i+1, g_values[i]);
            }

            // Add to circular buffer for plot
            plot_vals[plot_index] = g_values[0];
            plot_index = (plot_index + 1) % plot_vals.size();

            // Plot
            if (ImPlot::BeginPlot("Sensor 1 Plot"))
            {
                ImPlot::PlotLine("S1", plot_vals.data(), plot_vals.size());
                ImPlot::EndPlot();
            }
        }

        ImGui::End();

        // Render
        ImGui::Render();
        glViewport(0, 0, 800, 600);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwTerminate();
    delete g_sensor;

    return 0;
}

