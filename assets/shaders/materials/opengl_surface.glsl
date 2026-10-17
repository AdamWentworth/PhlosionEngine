        vec3 srgbToLinear(vec3 c) {
            c = clamp(c, 0.0, 1.0);
            vec3 lo = c / 12.92;
            vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
            return mix(lo, hi, step(vec3(0.04045), c));
        }

        vec3 linearToSrgb(vec3 c) {
            c = max(c, vec3(0.0));
            vec3 lo = c * 12.92;
            vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
            return mix(lo, hi, step(vec3(0.0031308), c));
        }

        vec3 encodeWorldSurfaceColor(vec3 linearColor) {
            // The source writes linear color to UNORM before its dedicated
            // gamma_correction shader applies the standard sRGB transfer.
            return linearToSrgb(clamp(linearColor, 0.0, 1.0));
        }

        vec3 resolveWorldSceneColor(vec3 linearColor) {
    if (WORLD_SCENE_RADIANCE) return max(linearColor, vec3(0.0,0.0,0.0));
            vec3 clamped = clamp(linearColor, 0.0, 1.0);
            return (uSceneColorPostEnabled > 0.5)
                ? clamped
                : encodeWorldSurfaceColor(clamped);
        }

        vec3 safeNormalize(vec3 value, vec3 fallback) {
            float len2 = dot(value, value);
            if (len2 < 1e-8) return fallback;
            return value * inversesqrt(len2);
        }

        int decodeReviewLightingProfile(vec3 cameraForwardPacked) {
            return clamp(
                int(floor(length(cameraForwardPacked) + 0.5)) - 1,
                0,
                4);
        }

        vec3 applyReviewLightingProfile(
            vec3 composite,
            vec3 resolvedAlbedo,
            vec3 sourceNormal,
            vec3 cameraForwardPacked) {
            if (ENV_SUN.w > 0.5) return max(composite, vec3(0.0));
            int profile = decodeReviewLightingProfile(cameraForwardPacked);
            if (profile == 0) return max(composite, vec3(0.0));
            if (profile == 4) return max(composite, vec3(0.0));
            vec3 albedo = max(resolvedAlbedo, vec3(0.0));
            if (profile == 1) {
                vec3 shadowFloor = albedo * 0.50;
                return max(
                    mix(composite, max(composite, shadowFloor), 0.56),
                    vec3(0.0));
            }
            if (profile == 2) {
                vec3 highlight = max(composite - albedo, vec3(0.0));
                return max(
                    mix(composite, albedo, 0.82) + highlight * 0.16,
                    vec3(0.0));
            }
            vec3 cameraForward = safeNormalize(
                cameraForwardPacked,
                vec3(0.0, -0.6139406, -0.7893522));
            vec3 cameraRight = safeNormalize(
                cross(cameraForward, vec3(0.0, 1.0, 0.0)),
                vec3(1.0, 0.0, 0.0));
            vec3 normal = safeNormalize(
                sourceNormal,
                vec3(0.0, 1.0, 0.0));
            float grazing = pow(abs(dot(normal, cameraRight)), 0.70);
            vec3 grazingSurface = albedo * (0.36 + 0.78 * grazing);
            vec3 highlight = max(composite - albedo, vec3(0.0));
            return max(
                mix(max(composite, albedo * 0.34), grazingSurface, 0.62) +
                    highlight * 0.20,
                vec3(0.0));
        }

__PHLOSION_SHARED_WORLD_PBR_SECTION__

        vec3 perturbNormal2Arb(vec3 eyePos, vec3 surfNorm, vec3 mapN, vec2 uv, float faceDirection) {
            // Mirrors three.js perturbNormal2Arb derivative basis construction.
            vec3 q0 = dFdx(eyePos.xyz);
            vec3 q1 = dFdy(eyePos.xyz);
            vec2 st0 = dFdx(uv);
            vec2 st1 = dFdy(uv);

            vec3 N = surfNorm;
            vec3 q1perp = cross(q1, N);
            vec3 q0perp = cross(N, q0);
            vec3 T = q1perp * st0.x + q0perp * st1.x;
            vec3 B = q1perp * st0.y + q0perp * st1.y;

            float det = max(dot(T, T), dot(B, B));
            float scale = (det <= 1e-10) ? 0.0 : faceDirection * inversesqrt(det);
            return normalize(T * (mapN.x * scale) + B * (mapN.y * scale) + N * mapN.z);
        }

        vec3 computeMappedNormalFromTexture(
            sampler2D normalTexture,
            vec2 sampleUv,
            vec2 uvDx,
            vec2 uvDy,
            float sourceNormalScale) {
            // Keep OpenGL tangent-space face handling tied to native front-face
            // classification for stable normal-map response.
            bool isFrontFace = worldReflectionFrontFace(gl_FrontFacing);
            float faceDirection = isFrontFace ? 1.0 : -1.0;
            vec3 n = normalize(vWorldNormal);
            if (dot(n, n) < 1e-6) {
                vec3 dx = dFdx(vWorldPos);
                vec3 dy = dFdy(vWorldPos);
                n = normalize(cross(dx, dy));
            }
            n *= faceDirection;

            vec3 normalTexel = sampleTextureWithWrap(
                normalTexture,
                sampleUv,
                uvDx,
                uvDy).xyz;
            vec2 mapXY = normalTexel.xy * 2.0 - 1.0;
            mapXY *= max(sourceNormalScale, 0.0);
            // Support both standard tangent-space normals (RGB) and
            // two-channel packed XY normals. Decoded XY maps can use either
            // blue=0 or blue=255 as a sentinel; reconstruct Z in both cases.
            float authoredZ = normalTexel.z * 2.0 - 1.0;
            float reconZ = sqrt(max(1.0 - clamp(dot(mapXY, mapXY), 0.0, 1.0), 0.0));
            float useReconstructedZ =
                (normalTexel.z <= (1.5 / 255.0) ||
                 normalTexel.z >= (253.5 / 255.0))
                    ? 1.0
                    : 0.0;
            float mapZ = mix(authoredZ, reconZ, useReconstructedZ);
            vec3 mapN = normalize(vec3(mapXY, mapZ));

            vec3 mapped = vec3(0.0);
            vec3 tangent = vWorldTangent.xyz;
            float tangentLenSq = dot(tangent, tangent);
            bool hasAuthoredTangent = tangentLenSq > 1e-6 && abs(vWorldTangent.w) > 0.5;
            if (hasAuthoredTangent) {
                tangent *= inversesqrt(tangentLenSq);
                tangent = tangent - n * dot(n, tangent);
                float orthoLenSq = dot(tangent, tangent);
                if (orthoLenSq > 1e-10) {
                    tangent *= inversesqrt(orthoLenSq);
                    float tangentSign = (vWorldTangent.w < 0.0) ? -1.0 : 1.0;
                    vec3 bitangent = normalize(cross(n, tangent)) * tangentSign;
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
                mapped = perturbNormal2Arb(vWorldPos, n, mapN, sampleUv, faceDirection);
            }
            return mapped;
        }

        vec3 computeMappedNormal(
            vec2 sampleUv,
            vec2 uvDx,
            vec2 uvDy,
            float materialNormalMultiplier) {
            return computeMappedNormalFromTexture(
                uNormalTexture,
                sampleUv,
                uvDx,
                uvDy,
                max(uNormalScale, 0.0) * 1.25 *
                    max(materialNormalMultiplier, 0.0));
        }

        vec3 applyWorldLitModel(vec3 linearColor, vec3 n, vec2 sampleUv, vec2 uvDx, vec2 uvDy) {
#include "material/opengl_surface_textures.glsl"
            float roughness = clamp(orm.g * clamp(uRoughnessFactor, 0.0, 1.0), 0.16, 1.0);
            float metallic = clamp(orm.b * clamp(uMetallicFactor, 0.0, 1.0), 0.0, 1.0);
            float occTex = sampleTextureWithWrap(
                uOcclusionTexture,
                sampleUv,
                uvDx,
                uvDy).r;
            float ao = mix(1.0, occTex, clamp(uOcclusionStrength, 0.0, 1.0));

            vec3 albedo = clamp(linearColor, 0.0, 1.0);
#include "material/opengl_surface_parameters.glsl"
            environmentWetMaterial(vWorldPos, n, albedo, roughness, dielectricSpecular);
            vec3 F0 = mix(vec3(dielectricSpecular), albedo, metallic);
            vec3 diffuseColor = albedo * (1.0 - metallic);
            const float specularF90 = 1.0;
            vec3 camForward = safeNormalize(
                uCameraForward,
                normalize(vec3(0.0, -0.6139406, -0.7893522)));
            vec3 camRight = cross(camForward, vec3(0.0, 1.0, 0.0));
            if (dot(camRight, camRight) < 1e-6) {
                camRight = cross(camForward, vec3(0.0, 0.0, 1.0));
            }
            camRight = safeNormalize(camRight, vec3(1.0, 0.0, 0.0));
            vec3 camUp = safeNormalize(cross(camRight, camForward), vec3(0.0, 1.0, 0.0));
            vec3 v = safeNormalize(uCameraPos - vWorldPos, -camForward);
            vec3 directColor = ENV_SUN.w > 0.5 ? environmentLightRadiance() : vec3(1.0);
            float directIntensity = ENV_SUN.w > 0.5 ? 1.0 : __PHLOSION_PBR_DIRECT_INTENSITY__ * 3.14159265;
            vec3 ambientColor = ENV_SUN.w > 0.5 ? ENV_AMBIENT.rgb : vec3(1.0);
            float ambientIntensity = ENV_SUN.w > 0.5 ? 1.0 : __PHLOSION_PBR_AMBIENT_INTENSITY__;

            vec3 lightPos = uCameraPos + camRight * 0.5 + camUp * 0.0 - camForward * 0.8660254;
            vec3 l0 = ENV_SUN.w > 0.5 ? environmentLightDirection() : safeNormalize(lightPos - uCameraTarget, vec3(0.45, 0.86, 0.24));
            vec3 direct = evalDirectPbr(
                n, v, l0, directColor * directIntensity, albedo, F0, roughness, metallic);
            direct += environmentLocalPbr(vWorldPos, n, v, albedo, F0, roughness, metallic);

            float NdotV = max(dot(n, v), 0.0);
            vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

            vec3 r = reflect(-v, n);
            vec3 envIrradiance = ENV_SUN.w > 0.5 ? ENV_AMBIENT.rgb * 3.14159265 : 3.14159265 * sampleNeutralEnvironment(n, 1.0);
            vec3 envRadiance = ENV_SUN.w > 0.5 ? ENV_AMBIENT.rgb * 0.5 : sampleNeutralEnvironment(r, roughness);
            vec3 singleScattering = vec3(0.0);
            vec3 multiScattering = vec3(0.0);
            computeMultiscattering(n, v, F0, specularF90, roughness, singleScattering, multiScattering);
            vec3 cosineWeightedIrradiance = envIrradiance * (1.0 / 3.14159265);
            vec3 totalScattering = singleScattering + multiScattering;
            float energyComp = 1.0 - max(max(totalScattering.r, totalScattering.g), totalScattering.b);
            vec3 diffuseIBL = diffuseColor * max(energyComp, 0.0) * cosineWeightedIrradiance;
            vec3 specularIBL = envRadiance * singleScattering + multiScattering * cosineWeightedIrradiance;
            diffuseIBL *= __PHLOSION_PBR_DIFFUSE_IBL_SCALE__;
            specularIBL *= __PHLOSION_PBR_SPECULAR_IBL_SCALE__;
            specularIBL = worldPlanarSpecular(specularIBL, vWorldPos, n, v, F0, roughness);
#include "material/opengl_surface_ibl.glsl"
            diffuseIBL *= ao;
            float specularOcclusion = computeSpecularOcclusion(NdotV, ao, roughness);
            specularIBL *= specularOcclusion;
            vec3 ibl = diffuseIBL + specularIBL;

            vec3 ambientLight = kD * albedo * ambientColor * ambientIntensity;
            vec3 shaded = direct + ibl + ambientLight;

            vec3 emissiveTex = clamp(
                sampleTextureWithWrap(
                    uEmissiveTexture,
                    sampleUv,
                    uvDx,
                    uvDy).rgb,
                0.0,
                1.0);
            vec3 emissive = emissiveTex * max(uEmissiveFactor, vec3(0.0));
#include "material/opengl_surface_response.glsl"
            return max(shaded + worldReflectionEmission(emissive), vec3(0.0));
        }
