float3 applyWorldLitModel(PSIn i,
                          bool isFrontFace,
                          float3 linearColor,
                          float2 sampleUv,
                          float2 uvDx,
                          float2 uvDy,
                          bool useNormalTexture,
                          bool useMetallicRoughnessTexture,
                          bool useSpecularStrengthTexture,
                          bool useOcclusionTexture,
                          bool useEmissiveTexture,
                          float normalScale,
                          float metallicFactor,
                          float roughnessFactor,
                          float specularIntensity,
                          float occlusionStrength,
                          float3 emissiveFactor,
                          float3 cameraPos,
                          float3 cameraForwardPacked,
                          float3 cameraTarget) {
  float3 n = computeMappedNormal(i, isFrontFace, sampleUv, uvDx, uvDy, useNormalTexture, normalScale);
  float4 orm = float4(1.0f, 1.0f, 1.0f, 1.0f);
  if (useMetallicRoughnessTexture) {
    orm = sampleTextureWithWrap(
              gMetallicRoughnessTex,
              sampleUv,
              uvDx,
              uvDy,
              uWrapS,
              uWrapT);
  }
  float roughness = clamp(orm.g * saturate(roughnessFactor), 0.16f, 1.0f);
  float metallic = clamp(orm.b * saturate(metallicFactor), 0.0f, 1.0f);
  float ao = 1.0f;
  if (useOcclusionTexture) {
    float occTex = sampleTextureWithWrap(gOcclusionTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).r;
    ao = lerp(1.0f, occTex, saturate(occlusionStrength));
  }

  float3 albedo = saturate(linearColor);
  float dielectricSpecular = useSpecularStrengthTexture
      ? saturate(specularIntensity) * saturate(orm.a)
      : 0.04f;
  environmentWetMaterial(i.worldPos, n, albedo, roughness, dielectricSpecular);
  float3 F0 = lerp(
      float3(dielectricSpecular, dielectricSpecular, dielectricSpecular),
      albedo,
      metallic);
  float3 diffuseColor = albedo * (1.0f - metallic);
  const float specularF90 = 1.0f;
  float3 camForward = safeNormalize(cameraForwardPacked, normalize(float3(0.0f, -0.6139406f, -0.7893522f)));
  float3 camRight = cross(camForward, float3(0.0f, 1.0f, 0.0f));
  if (dot(camRight, camRight) < 1e-6f) {
    camRight = cross(camForward, float3(0.0f, 0.0f, 1.0f));
  }
  camRight = safeNormalize(camRight, float3(1.0f, 0.0f, 0.0f));
  float3 camUp = safeNormalize(cross(camRight, camForward), float3(0.0f, 1.0f, 0.0f));
  float3 v = safeNormalize(cameraPos - i.worldPos, -camForward);
  float3 directColor = ENV_SUN.w > 0.5 ? environmentLightRadiance() : float3(1.0f, 1.0f, 1.0f);
  float directIntensity = ENV_SUN.w > 0.5 ? 1.0 : __PHLOSION_PBR_DIRECT_INTENSITY__ * 3.14159265f;
  float3 ambientColor = ENV_SUN.w > 0.5 ? ENV_AMBIENT.rgb : float3(1.0f, 1.0f, 1.0f);
  float ambientIntensity = ENV_SUN.w > 0.5 ? 1.0 : __PHLOSION_PBR_AMBIENT_INTENSITY__;

  float3 lightPos = cameraPos + camRight * 0.5f + camUp * 0.0f - camForward * 0.8660254f;
  float3 l0 = ENV_SUN.w > 0.5 ? environmentLightDirection() : safeNormalize(lightPos - cameraTarget, float3(0.45f, 0.86f, 0.24f));
  float3 direct = evalDirectPbr(n, v, l0, directColor * directIntensity, albedo, F0, roughness, metallic);
  direct += environmentLocalPbr(i.worldPos, n, v, albedo, F0, roughness, metallic);

  float NdotV = max(dot(n, v), 0.0f);
  float3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
  float3 kS = F;
  float3 kD = (float3(1.0f, 1.0f, 1.0f) - kS) * (1.0f - metallic);

  float3 r = reflect(-v, n);
  float3 envIrradiance = ENV_SUN.w > 0.5 ? ENV_AMBIENT.rgb * 3.14159265f : 3.14159265f * sampleNeutralEnvironment(n, 1.0f);
  float3 envRadiance = ENV_SUN.w > 0.5 ? ENV_AMBIENT.rgb * 0.5 : sampleNeutralEnvironment(r, roughness);
  float3 singleScattering = float3(0.0f, 0.0f, 0.0f);
  float3 multiScattering = float3(0.0f, 0.0f, 0.0f);
  computeMultiscattering(n, v, F0, specularF90, roughness, singleScattering, multiScattering);
  float3 cosineWeightedIrradiance = envIrradiance * (1.0f / 3.14159265f);
  float3 totalScattering = singleScattering + multiScattering;
  float energyComp = 1.0f - max(max(totalScattering.r, totalScattering.g), totalScattering.b);
  float3 diffuseIBL = diffuseColor * max(energyComp, 0.0f) * cosineWeightedIrradiance;
  float3 specularIBL = envRadiance * singleScattering + multiScattering * cosineWeightedIrradiance;
  diffuseIBL *= __PHLOSION_PBR_DIFFUSE_IBL_SCALE__;
  specularIBL *= __PHLOSION_PBR_SPECULAR_IBL_SCALE__;
  specularIBL = worldPlanarSpecular(specularIBL, i.worldPos, n, v, F0, roughness);
#include "material/d3d12_surface_ibl.hlsl"
  diffuseIBL *= ao;
  float specularOcclusion = computeSpecularOcclusion(NdotV, ao, roughness);
  specularIBL *= specularOcclusion;
  float3 ibl = diffuseIBL + specularIBL;

  float3 ambientLight = kD * albedo * ambientColor * ambientIntensity;
  float3 shaded = direct + ibl + ambientLight;

  float3 emissiveTex = useEmissiveTexture
      ? saturate(sampleTextureWithWrap(gEmissiveTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).rgb)
      : float3(1.0f, 1.0f, 1.0f);
  float3 emissive = emissiveTex * max(emissiveFactor, float3(0.0f, 0.0f, 0.0f));
#include "material/d3d12_surface_response.hlsl"
  return max(shaded + worldReflectionEmission(emissive), float3(0.0f, 0.0f, 0.0f));
}
