  if (uMaterialMode >= 1.5f) {
    outLinear = applyReviewLightingProfile(
        outLinear,
        reviewAlbedo,
        i.worldNormal,
        reviewCameraForward);
  }
  outLinear = environmentHaze(outLinear, i.worldPos, uEnvironmentCamera.xyz);
    float toneMappingExposure = ENV_SUN.w > 0.5 ? ENV_AMBIENT.w : __PHLOSION_PBR_TONEMAP_EXPOSURE__;
  const float toneMappingMode = 1.0f;
  float3 mapped = applyViewerToneMapping(
      max(outLinear, float3(0.0f, 0.0f, 0.0f)),
      toneMappingMode,
      toneMappingExposure);
  float3 outSrgb = resolveWorldSceneColor(mapped);
  return float4(outSrgb, outA);
