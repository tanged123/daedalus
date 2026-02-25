#version 330 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in float a_dist;

uniform mat4 u_vp;
uniform mat4 u_model;

out float v_dist;
out vec3 v_world_pos;

void main() {
    vec4 world_pos = u_model * vec4(a_pos, 1.0);
    gl_Position = u_vp * world_pos;
    v_dist = a_dist;
    v_world_pos = world_pos.xyz;
}
