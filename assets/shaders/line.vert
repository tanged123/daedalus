#version 330 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in float a_dist;

uniform mat4 u_vp;
uniform mat4 u_model;

out float v_dist;

void main() {
    gl_Position = u_vp * u_model * vec4(a_pos, 1.0);
    v_dist = a_dist;
}
