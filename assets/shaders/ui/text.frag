// text.frag

#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D u_Texture;
uniform vec3 u_TextColor;
/* NEW: global alpha to allow proper fading */
uniform float u_GlobalAlpha;

void main()
{
    // Sample the glyph (RGBA), keep texture alpha and modulate by global alpha.
    vec4 sampled = texture(u_Texture, TexCoord);
    FragColor = vec4(u_TextColor, sampled.a * u_GlobalAlpha);
}
