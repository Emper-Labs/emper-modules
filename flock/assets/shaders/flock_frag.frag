#version 430 core

layout(location = 0) out vec4 color;
layout(location = 0) in vec3 teamColor;

void main()
{
    color = vec4(teamColor, 1.0);
}
