// SPDX-License-Identifier: Apache-2.0

#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void on_key(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

static void draw_face(float r, float g, float b,
                      float ax, float ay, float az,
                      float bx, float by, float bz,
                      float cx, float cy, float cz,
                      float dx, float dy, float dz) {
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex3f(ax, ay, az);
    glVertex3f(bx, by, bz);
    glVertex3f(cx, cy, cz);
    glVertex3f(dx, dy, dz);
    glEnd();
}

static void draw_cube(float glow) {
    draw_face(0.95f, 0.12f + glow, 0.22f, -1, -1,  1,  1, -1,  1,  1,  1,  1, -1,  1,  1);
    draw_face(0.05f, 0.55f, 0.95f,        -1, -1, -1, -1,  1, -1,  1,  1, -1,  1, -1, -1);
    draw_face(0.18f, 0.82f, 0.34f,        -1,  1, -1, -1,  1,  1,  1,  1,  1,  1,  1, -1);
    draw_face(1.00f, 0.74f, 0.12f,        -1, -1, -1,  1, -1, -1,  1, -1,  1, -1, -1,  1);
    draw_face(0.72f, 0.28f, 0.96f,         1, -1, -1,  1,  1, -1,  1,  1,  1,  1, -1,  1);
    draw_face(0.95f, 0.42f, 0.10f + glow, -1, -1, -1, -1, -1,  1, -1,  1,  1, -1,  1, -1);

    glColor3f(1.0f, 1.0f, 1.0f);
    glLineWidth(1.5f);
    glBegin(GL_LINES);
    glVertex3f(-1, -1, -1); glVertex3f( 1, -1, -1);
    glVertex3f( 1, -1, -1); glVertex3f( 1,  1, -1);
    glVertex3f( 1,  1, -1); glVertex3f(-1,  1, -1);
    glVertex3f(-1,  1, -1); glVertex3f(-1, -1, -1);
    glVertex3f(-1, -1,  1); glVertex3f( 1, -1,  1);
    glVertex3f( 1, -1,  1); glVertex3f( 1,  1,  1);
    glVertex3f( 1,  1,  1); glVertex3f(-1,  1,  1);
    glVertex3f(-1,  1,  1); glVertex3f(-1, -1,  1);
    glVertex3f(-1, -1, -1); glVertex3f(-1, -1,  1);
    glVertex3f( 1, -1, -1); glVertex3f( 1, -1,  1);
    glVertex3f( 1,  1, -1); glVertex3f( 1,  1,  1);
    glVertex3f(-1,  1, -1); glVertex3f(-1,  1,  1);
    glEnd();
}

static void draw_grid(void) {
    glColor3f(0.18f, 0.24f, 0.32f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int i = -12; i <= 12; ++i) {
        glVertex3f((float)i, -1.65f, -12.0f);
        glVertex3f((float)i, -1.65f,  12.0f);
        glVertex3f(-12.0f, -1.65f, (float)i);
        glVertex3f( 12.0f, -1.65f, (float)i);
    }
    glEnd();
}

static void draw_stars(double elapsed) {
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < 90; ++i) {
        float x = (float)((i * 37) % 48) - 24.0f;
        float y = (float)((i * 19) % 20) + 2.0f;
        float z = -10.0f - (float)((i * 23) % 34);
        float pulse = 0.55f + 0.35f * sinf((float)elapsed * 2.0f + (float)i);
        glColor3f(pulse, pulse, 1.0f);
        glVertex3f(x, y, z);
    }
    glEnd();
}

static void draw_scene(double elapsed) {
    draw_stars(elapsed);
    draw_grid();

    for (int i = 0; i < 5; ++i) {
        float phase = (float)i * 1.2566371f;
        float radius = i == 0 ? 0.0f : 3.2f;
        float x = cosf((float)elapsed * 0.75f + phase) * radius;
        float z = sinf((float)elapsed * 0.75f + phase) * radius;
        float scale = i == 0 ? 1.15f : 0.48f;
        float glow = 0.18f * (0.5f + 0.5f * sinf((float)elapsed * 3.0f + phase));

        glPushMatrix();
        glTranslatef(x, 0.0f, z);
        glRotatef((float)(elapsed * (55.0 + i * 18.0)), 0.35f, 1.0f, 0.25f);
        glScalef(scale, scale, scale);
        draw_cube(glow);
        glPopMatrix();
    }
}

int main(int argc, char** argv) {
    double seconds = 0.0;
    if (argc > 1) {
        seconds = atof(argv[1]);
    }

    if (!glfwInit()) {
        fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Vyb Native 3D", NULL, NULL);
    if (!window) {
        fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, on_key);
    glfwSwapInterval(1);
    glEnable(GL_DEPTH_TEST);

    double start = glfwGetTime();
    while (!glfwWindowShouldClose(window) && (seconds <= 0.0 || glfwGetTime() - start < seconds)) {
        int width = 0;
        int height = 0;
        double elapsed = glfwGetTime() - start;

        glfwGetFramebufferSize(window, &width, &height);
        if (height == 0) {
            height = 1;
        }

        glViewport(0, 0, width, height);
        glClearColor(0.035f, 0.040f, 0.055f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        float aspect = (float)width / (float)height;
        glFrustum(-aspect, aspect, -1.0, 1.0, 1.5, 60.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0.0f, -0.25f, -10.0f);
        glRotatef(18.0f, 1.0f, 0.0f, 0.0f);
        glRotatef((float)(elapsed * 10.0), 0.0f, 1.0f, 0.0f);
        draw_scene(elapsed);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
