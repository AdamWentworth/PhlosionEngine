// default.frag

#version 330 core

in vec4 fakeOut;

out vec4 FragColor;

void main() {
    // Use the w component of fakeOut so the compiler keeps it
    FragColor = vec4(0.3, 0.8, fakeOut.w, 1.0); 
}
