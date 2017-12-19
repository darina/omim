// Warning! Beware to use this shader. "discard" command may significally reduce performance.
// Unfortunately some CG algorithms cannot be implemented on OpenGL ES 2.0 without discarding
// fragments from depth buffer.

#ifdef SAMSUNG_GOOGLE_NEXUS
uniform sampler2D u_colorTex;
#endif

varying vec3 v_radius;
varying vec4 v_color;

const float kAntialiasingPixelsCount = 2.5;

void main()
{
  vec4 finalColor = v_color;
  
  float aaRadius = max(v_radius.z - kAntialiasingPixelsCount, 0.0);
  float stepValue = smoothstep(aaRadius * aaRadius, v_radius.z * v_radius.z,
                               dot(v_radius.xy, v_radius.xy));
  finalColor.a = finalColor.a * (1.0 - stepValue);

  gl_FragColor = samsungGoogleNexusWorkaround(finalColor);
}
