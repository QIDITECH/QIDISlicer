#version 100

precision highp float;

const vec4 BLACK = vec4(vec3(0.1), 1.0);
const vec4 WHITE = vec4(vec3(1.0), 1.0);

const float emission_factor = 0.25;

uniform vec3 world_center;

// x = tainted, y = specular;
varying vec2 intensity;
varying vec3 world_position;

void main()
{
    vec3 delta = world_position - world_center;
    vec4 color = delta.x * delta.y * delta.z > 0.0 ? BLACK : WHITE;
    gl_FragColor = vec4(vec3(intensity.y) + color.rgb * (intensity.x + emission_factor), 1.0);
}
