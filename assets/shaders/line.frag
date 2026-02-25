#version 330 core

in float v_dist;
in vec3 v_world_pos;

uniform vec4 u_color;
uniform float u_soft_edge;
uniform vec3 u_camera_pos;
uniform int u_clip_backside;

out vec4 frag_color;

void main() {
    if (u_clip_backside != 0) {
        vec3 to_camera = u_camera_pos - v_world_pos;
        float to_camera_len = length(to_camera);
        if (to_camera_len > 1e-6) {
            vec3 normal_dir = normalize(v_world_pos);
            vec3 view_dir = to_camera / to_camera_len;
            if (dot(normal_dir, view_dir) <= 0.0) {
                discard;
            }
        }
    }

    float alpha = exp(-v_dist * v_dist * u_soft_edge) * u_color.a;
    frag_color = vec4(u_color.rgb, alpha);
}
