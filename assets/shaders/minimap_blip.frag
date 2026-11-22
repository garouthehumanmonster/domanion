#version 460 core

in vec2 v_local_px;
in float v_r;
flat in vec3 v_color;

out vec4 frag_color;

void main() {
    float dist = length(v_local_px);
    float alpha = 1.0 - smoothstep(v_r - 1.0, v_r, dist);
    if (alpha <= 0.0) discard;
    frag_color = vec4(v_color, alpha);
}
