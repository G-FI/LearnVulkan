#version 450 
#extension GL_ARB_separate_shader_objects : enable

layout (location=0) in vec3 inFragColor;

layout (location=0) out vec4 fragColor;
void main(){
	fragColor = vec4(inFragColor, 1.0);
}

