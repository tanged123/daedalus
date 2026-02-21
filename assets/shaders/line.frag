#version 330 core

in float v_dist;

uniform vec4 u_color;
uniform float u_soft_edge;

out vec4 frag_color;

void main() {
    float alpha = exp(-v_dist * v_dist * u_soft_edge) * u_color.a;
    frag_color = vec4(u_color.rgb, alpha);
}
