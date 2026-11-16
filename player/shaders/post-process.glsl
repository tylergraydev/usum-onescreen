// SPDX-License-Identifier: GPL-3.0-only
// Copyright 2026 USUM One Screen contributors.
// Original shader code; no third-party shader source incorporated.
// Azahar supplies SampleLocation, GetCoordinates, GetInvResolution, SetOutput.
// A single pass of contrast-limited sharpening and/or FXAA-style edge smoothing.
float brightness(vec3 rgb)
{
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main()
{
    vec2 uv = GetCoordinates();
    vec2 pixel = GetInvResolution();
    vec4 center = SampleLocation(uv);
    vec3 north = SampleLocation(uv + vec2(0.0, pixel.y)).rgb;
    vec3 south = SampleLocation(uv - vec2(0.0, pixel.y)).rgb;
    vec3 east = SampleLocation(uv + vec2(pixel.x, 0.0)).rgb;
    vec3 west = SampleLocation(uv - vec2(pixel.x, 0.0)).rgb;
    vec3 low = min(center.rgb, min(min(north, south), min(east, west)));
    vec3 high = max(center.rgb, max(max(north, south), max(east, west)));
    vec3 result = center.rgb;
    float smoothing = 0.0;
#if USUM_AA
    float ln = brightness(north), ls = brightness(south);
    float le = brightness(east), lw = brightness(west);
    float lc = brightness(center.rgb);
    float darkest = min(lc, min(min(ln, ls), min(le, lw)));
    float lightest = max(lc, max(max(ln, ls), max(le, lw)));
    float contrast = lightest - darkest;
    // Skip flat areas. Follow the local edge tangent, sampling on both sides
    // to soften stair steps while retaining sharp high-contrast boundaries.
    vec2 gradient = vec2(le - lw, ln - ls);
    float magnitude = length(gradient);
    if (contrast > max(0.04, lightest * 0.12) && magnitude > 0.001)
    {
        vec2 tangent = vec2(-gradient.y, gradient.x) / magnitude;
        vec3 along = 0.5 * (SampleLocation(uv + tangent * pixel * 0.75).rgb +
                            SampleLocation(uv - tangent * pixel * 0.75).rgb);
        smoothing = clamp(abs(0.25 * (ln + ls + le + lw) - lc) / contrast, 0.0, 0.75);
        result = mix(result, clamp(along, low, high), smoothing);
    }
#endif
#if USUM_SHARPEN
    // Attenuate sharpening where AA acted; local bounds limit ringing.
    vec3 detail = center.rgb - 0.25 * (north + south + east + west);
    result = clamp(result + detail * (0.35 * (1.0 - smoothing)), low, high);
#endif
    SetOutput(vec4(clamp(result, 0.0, 1.0), center.a));
}
