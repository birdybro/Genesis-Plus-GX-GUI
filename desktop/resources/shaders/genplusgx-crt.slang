#version 450

#pragma parameter SCANLINE_STRENGTH "Scanline Strength" 0.30 0.00 1.00 0.01
#pragma parameter MASK_STRENGTH "Aperture Grille Strength" 0.18 0.00 1.00 0.01
#pragma parameter CURVATURE "Screen Curvature" 0.08 0.00 0.25 0.01
#pragma parameter VIGNETTE "Vignette Strength" 0.18 0.00 1.00 0.01
#pragma parameter BRIGHTNESS "Brightness" 1.08 0.50 1.50 0.01
#pragma name GenesisPlusGXCrt
#pragma format R8G8B8A8_UNORM

layout(set = 0, binding = 0, std140) uniform UBO
{
  mat4 MVP;
  vec4 SourceSize;
  vec4 OriginalSize;
  vec4 OutputSize;
  uint FrameCount;
  float SCANLINE_STRENGTH;
  float MASK_STRENGTH;
  float CURVATURE;
  float VIGNETTE;
  float BRIGHTNESS;
};

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
  gl_Position = MVP * Position;
  vTexCoord = TexCoord;
}

#pragma stage fragment
layout(binding = 1) uniform sampler2D Source;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

void main()
{
  vec2 centered = vTexCoord * 2.0 - 1.0;
  float radius2 = dot(centered, centered);
  vec2 warped = centered * (1.0 + CURVATURE * radius2);
  vec2 uv = warped * 0.5 + 0.5;
  if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  vec3 color = texture(Source, uv).rgb;
  float sourceLine = uv.y * SourceSize.y;
  float scanline = 1.0 - SCANLINE_STRENGTH *
    (0.5 + 0.5 * cos(6.28318530718 * sourceLine));

  int maskColumn = int(mod(floor(gl_FragCoord.x), 3.0));
  vec3 grille = maskColumn == 0 ? vec3(1.0, 0.65, 0.65) :
    (maskColumn == 1 ? vec3(0.65, 1.0, 0.65) : vec3(0.65, 0.65, 1.0));
  color *= mix(vec3(1.0), grille, MASK_STRENGTH);
  color *= scanline * BRIGHTNESS;

  vec2 edge = abs(centered);
  float vignette = 1.0 - VIGNETTE * smoothstep(0.35, 1.25, dot(edge, edge));
  FragColor = vec4(clamp(color * vignette, 0.0, 1.0), 1.0);
}
