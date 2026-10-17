float3 safeNormalize(float3 value, float3 fallback) {
  float len2 = dot(value, value);
  if (len2 < 1e-8f) return fallback;
  return value * rsqrt(len2);
}

int decodeReviewLightingProfile(float3 cameraForwardPacked) {
  return clamp(
      (int)floor(length(cameraForwardPacked) + 0.5f) - 1,
      0,
      4);
}

float3 applyReviewLightingProfile(float3 composite,
                                  float3 resolvedAlbedo,
                                  float3 sourceNormal,
                                  float3 cameraForwardPacked) {
  if (ENV_SUN.w > 0.5) return max(composite, float3(0.0f, 0.0f, 0.0f));
  const int profile = decodeReviewLightingProfile(cameraForwardPacked);
  if (profile == 0) return max(composite, float3(0.0f, 0.0f, 0.0f));
  if (profile == 4) return max(composite, float3(0.0f, 0.0f, 0.0f));
  const float3 albedo = max(resolvedAlbedo, float3(0.0f, 0.0f, 0.0f));
  if (profile == 1) {
    const float3 shadowFloor = albedo * 0.50f;
    return max(
        lerp(composite, max(composite, shadowFloor), 0.56f),
        float3(0.0f, 0.0f, 0.0f));
  }
  if (profile == 2) {
    const float3 highlight = max(composite - albedo, float3(0.0f, 0.0f, 0.0f));
    return max(
        lerp(composite, albedo, 0.82f) + highlight * 0.16f,
        float3(0.0f, 0.0f, 0.0f));
  }
  const float3 cameraForward = safeNormalize(
      cameraForwardPacked,
      float3(0.0f, -0.6139406f, -0.7893522f));
  const float3 cameraRight = safeNormalize(
      cross(cameraForward, float3(0.0f, 1.0f, 0.0f)),
      float3(1.0f, 0.0f, 0.0f));
  const float3 normal = safeNormalize(
      sourceNormal,
      float3(0.0f, 1.0f, 0.0f));
  const float grazing = pow(abs(dot(normal, cameraRight)), 0.70f);
  const float3 grazingSurface = albedo * (0.36f + 0.78f * grazing);
  const float3 highlight = max(composite - albedo, float3(0.0f, 0.0f, 0.0f));
  return max(
      lerp(max(composite, albedo * 0.34f), grazingSurface, 0.62f) +
          highlight * 0.20f,
      float3(0.0f, 0.0f, 0.0f));
}

__PHLOSION_SHARED_WORLD_PBR_SECTION__

float3 perturbNormal2Arb(float3 eyePos, float3 surfNorm, float3 mapN, float2 uv, float faceDirection) {
  float3 q0 = ddx(eyePos.xyz);
  float3 q1 = ddy(eyePos.xyz);
  float2 st0 = ddx(uv);
  float2 st1 = ddy(uv);

  float3 N = surfNorm;
  float3 q1perp = cross(q1, N);
  float3 q0perp = cross(N, q0);
  float3 T = q1perp * st0.x + q0perp * st1.x;
  float3 B = q1perp * st0.y + q0perp * st1.y;

  float det = max(dot(T, T), dot(B, B));
  float scale = (det <= 1e-10f) ? 0.0f : faceDirection * rsqrt(det);
  return normalize(T * (mapN.x * scale) + B * (mapN.y * scale) + N * mapN.z);
}
float3 computeMappedNormalFromTexel(PSIn i,
                                    bool isFrontFace,
                                    float2 sampleUv,
                                    float3 normalTexel,
                                    float sourceNormalScale) {
  float faceDirection = isFrontFace ? 1.0f : -1.0f;
  float3 n = normalize(i.worldNormal);
  if (dot(n, n) < 1e-6f) {
    float3 dx = ddx(i.worldPos);
    float3 dy = ddy(i.worldPos);
    n = normalize(cross(dx, dy));
  }
  n *= faceDirection;
  float2 mapXY = normalTexel.xy * 2.0f - 1.0f;
  mapXY *= max(sourceNormalScale, 0.0f);
  // Support standard RGB tangent-space normals and two-channel packed XY
  // normals. Decoded XY maps can use blue=0 or blue=255 as a sentinel, so
  // reconstruct Z for both encodings.
  float authoredZ = normalTexel.z * 2.0f - 1.0f;
  float reconZ = sqrt(max(1.0f - saturate(dot(mapXY, mapXY)), 0.0f));
  float useReconstructedZ =
      (normalTexel.z <= (1.5f / 255.0f) ||
       normalTexel.z >= (253.5f / 255.0f))
          ? 1.0f
          : 0.0f;
  float mapZ = lerp(authoredZ, reconZ, useReconstructedZ);
  float3 mapN = normalize(float3(mapXY, mapZ));
  float3 mapped = float3(0.0f, 0.0f, 0.0f);
  float3 tangent = i.worldTangent.xyz;
  float tangentLen2 = dot(tangent, tangent);
  bool hasAuthoredTangent = tangentLen2 > 1e-6f && abs(i.worldTangent.w) > 0.5f;
  if (hasAuthoredTangent) {
    tangent *= rsqrt(tangentLen2);
    tangent = tangent - n * dot(n, tangent);
    float orthoLen2 = dot(tangent, tangent);
    if (orthoLen2 > 1e-10f) {
      tangent *= rsqrt(orthoLen2);
      float tangentSign = (i.worldTangent.w < 0.0f) ? -1.0f : 1.0f;
      float3 bitangent = normalize(cross(n, tangent)) * tangentSign;
      if (!isFrontFace) {
        tangent = -tangent;
        bitangent = -bitangent;
      }
      mapped = normalize(tangent * mapN.x + bitangent * mapN.y + n * mapN.z);
    } else {
      hasAuthoredTangent = false;
    }
  }
  if (!hasAuthoredTangent) {
    mapped = perturbNormal2Arb(i.worldPos, n, mapN, sampleUv, faceDirection);
  }
  return mapped;
}

float3 computeMappedNormal(PSIn i,
                           bool isFrontFace,
                           float2 sampleUv,
                           float2 uvDx,
                           float2 uvDy,
                           bool useNormalTexture,
                           float normalScale) {
  float3 normalTexel = useNormalTexture
      ? sampleTextureWithWrap(
            gNormalTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).xyz
      : float3(0.5f, 0.5f, 1.0f);
  return computeMappedNormalFromTexel(
      i,
      isFrontFace,
      sampleUv,
      normalTexel,
      useNormalTexture ? max(normalScale, 0.0f) * 1.25f : 0.0f);
}
