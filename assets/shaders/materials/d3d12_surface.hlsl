WorldSurfaceMaterial prepareWorldMaterial(PSIn i,
                                           bool isFrontFace,
                                           float3 linearColor,
                                           float2 sampleUv,
                                           float2 uvDx,
                                           float2 uvDy,
                                           bool useNormalTexture,
                                           bool useMetallicRoughnessTexture,
                                           bool useSpecularStrengthTexture,
                                           bool useOcclusionTexture,
                                           float normalScale,
                                           float metallicFactor,
                                           float roughnessFactor,
                                           float specularIntensity,
                                           float occlusionStrength) {
#ifndef PHLOSION_LAYERED_DISABLED
  if(uLayeredMaterial.x!=0u)
      return worldLayeredSurface(uLayeredMaterial.x,sampleUv,i.worldPos,i.worldNormal,i.worldTangent,litTextureDetailLodBias());
#endif
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
  float roughness = environmentMaterialRoughness(orm.g * saturate(roughnessFactor));
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
  environmentWetMaterial(i.worldPos, n, albedo, roughness, dielectricSpecular, metallic, i.worldNormal);
  WorldSurfaceMaterial surface;
  surface.albedo = albedo;
  surface.normal = n;
  surface.orm = orm;
  surface.metallic = metallic;
  surface.roughness = roughness;
  surface.occlusion = ao;
  surface.dielectricSpecular = dielectricSpecular;
  return surface;
}

// Lighting consumes evaluated attributes independently of mesh interpolation/material sampling.
float3 lightWorldSurface(WorldSurfaceMaterial surface, float3 emissive,
                         float3 position, float3 sourceNormal,
                         float3 cameraPos, float3 cameraForwardPacked,
                         float3 cameraTarget) {
    // Evaluated PBR already contains wetness; haze must not dampen it again.
    environmentWetMaterialApplied = 1.0;
  float3 n = surface.normal;
  float4 orm = surface.orm;
  float roughness = surface.roughness;
  float metallic = surface.metallic;
  float ao = surface.occlusion;
  float3 albedo = worldWetEmissiveColor(surface.albedo,emissive);
  float dielectricSpecular = surface.dielectricSpecular;
  if(worldContactPrepass())return safeNormalize(sourceNormal,n)*.5+.5;
  float4 contact=worldContactVisibility(position,safeNormalize(sourceNormal,n),cameraPos);
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
  float3 v = safeNormalize(cameraPos - position, -camForward);
  float rawContact=min(ao,worldContactDiffuseVisibility(contact,n,v));
  float4 contactResponse=worldContactResponse(rawContact,worldContactSpecularVisibility(contact,n,v,roughness),0.0);
  float contactOcclusion=min(ao,contactResponse.x);
  float3 directContact=environmentDiffuseOcclusion(contactResponse.z,albedo);
  float3 directColor = ENV_SUN.w > 0.5 ? environmentLightRadiance() : float3(1.0f, 1.0f, 1.0f);
  float directIntensity = ENV_SUN.w > 0.5 ? 1.0 : __PHLOSION_PBR_DIRECT_INTENSITY__ * 3.14159265f;
  float3 ambientColor = ENV_SUN.w > 0.5 ? ENV_AMBIENT.rgb : float3(1.0f, 1.0f, 1.0f);
  float ambientIntensity = ENV_SUN.w > 0.5 ? 1.0 : __PHLOSION_PBR_AMBIENT_INTENSITY__;

  float3 lightPos = cameraPos + camRight * 0.5f + camUp * 0.0f - camForward * 0.8660254f;
  float3 l0 = ENV_SUN.w > 0.5 ? environmentLightDirection() : safeNormalize(lightPos - cameraTarget, float3(0.45f, 0.86f, 0.24f));
  // Preserve the established direct-light lobe during this reflection pass.
  float directRoughness = max(roughness, .16);
  float3 direct = evalDirectPbr(n, v, l0, directColor * directIntensity, albedo, F0, directRoughness, metallic,directContact,contactResponse.w);
  direct += environmentStandardLocalPbr(position,n,v,albedo,F0,directRoughness,metallic,contactOcclusion,directContact,contactResponse.w);

  float NdotV = max(dot(n, v), 0.0f);
  float3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
  float3 kS = F;
  float3 kD = (float3(1.0f, 1.0f, 1.0f) - kS) * (1.0f - metallic);

  float3 r = reflect(-v, n);
  float3 envIrradiance = ENV_SUN.w > 0.5 ? worldIndirectAmbient(position,n) * 3.14159265f : 3.14159265f * sampleNeutralEnvironment(n, 1.0f);
  float3 envRadiance = ENV_SUN.w > 0.5 ? ENV_AMBIENT.rgb * 0.5 : sampleNeutralEnvironment(r, roughness);
  float3 singleScattering = float3(0.0f, 0.0f, 0.0f);
  float3 multiScattering = float3(0.0f, 0.0f, 0.0f);
  computeMultiscattering(n, v, F0, specularF90, roughness, singleScattering, multiScattering);
  environmentIndirectScattering(n, v, F0, roughness, singleScattering, multiScattering);
  envRadiance = worldReflectionRadiance(envRadiance, position, n, v, roughness);
  envRadiance *= contactResponse.y;
  float3 indirectOcclusion = environmentDiffuseOcclusion(contactOcclusion, albedo);
  float3 cosineWeightedIrradiance = envIrradiance * (1.0f / 3.14159265f);
  float3 totalScattering = singleScattering + multiScattering;
  float energyComp = 1.0f - max(max(totalScattering.r, totalScattering.g), totalScattering.b);
  float3 diffuseIBL = diffuseColor * max(energyComp, 0.0f) * cosineWeightedIrradiance;
  float3 reflectionResponse = environmentReflectionResponse(n, v, F0, roughness, albedo, rawContact, singleScattering);
  float3 specularIBL = envRadiance * reflectionResponse + multiScattering * cosineWeightedIrradiance;
  diffuseIBL *= worldIndirectEnabled() ? 1.0 : __PHLOSION_PBR_DIFFUSE_IBL_SCALE__;
  specularIBL *= environmentSpecularScale(__PHLOSION_PBR_SPECULAR_IBL_SCALE__);
#include "material/d3d12_surface_ibl.hlsl"
  diffuseIBL *= indirectOcclusion;
  float specularOcclusion = computeSpecularOcclusion(NdotV, ao, roughness);
  specularIBL *= specularOcclusion;
  float3 ibl = diffuseIBL + specularIBL;

  float3 ambientLight = kD * albedo * ambientColor * ambientIntensity * (ENV_SUN.w > 0.5 ? indirectOcclusion : float3(1.0,1.0,1.0));
  if(worldIndirectEnabled())ambientLight=float3(0,0,0);
  float3 shaded = direct + ibl + ambientLight;


#include "material/d3d12_surface_response.hlsl"
  return max(shaded + worldReflectionEmission(emissive), float3(0.0f, 0.0f, 0.0f));
}

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
  WorldSurfaceMaterial surface;
  float3 storedEmission;
  bool storedSurface=worldStoredSurface(i.worldPos,surface,storedEmission);
  if(!storedSurface)surface=prepareWorldMaterial(
      i, isFrontFace, linearColor, sampleUv, uvDx, uvDy, useNormalTexture,
      useMetallicRoughnessTexture, useSpecularStrengthTexture, useOcclusionTexture,
      normalScale, metallicFactor, roughnessFactor, specularIntensity, occlusionStrength);
#ifdef PHLOSION_WORLD_SURFACE_CAPTURE
  float3 surfaceEmission = (useEmissiveTexture
      ? saturate(sampleTextureWithWrap(gEmissiveTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).rgb)
      : float3(1.0f, 1.0f, 1.0f)) * max(emissiveFactor, float3(0.0f, 0.0f, 0.0f));
  storeWorldSurface(surface, surfaceEmission);
  return float3(0.0f, 0.0f, 0.0f);
#else
  float3 emissiveTex = useEmissiveTexture
      ? saturate(sampleTextureWithWrap(gEmissiveTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).rgb)
      : float3(1.0f, 1.0f, 1.0f);
  float3 emissive = storedSurface ? storedEmission : emissiveTex * max(emissiveFactor, float3(0.0f, 0.0f, 0.0f));
  return lightWorldSurface(surface, emissive, i.worldPos, i.worldNormal,
      cameraPos, cameraForwardPacked, cameraTarget);
#endif
}
