            if (uMaterialMode >= 1.5) {
                outLinear = applyReviewLightingProfile(
                    outLinear,
                    reviewAlbedo,
                    vWorldNormal,
                    uCameraForward);
            }
            outLinear = environmentHaze(outLinear, vWorldPos, uCameraPos);
    float toneMappingExposure = ENV_SUN.w > 0.5 ? ENV_AMBIENT.w : __PHLOSION_PBR_TONEMAP_EXPOSURE__;
            const float toneMappingMode = 1.0;
            vec3 mapped = applyViewerToneMapping(max(outLinear, vec3(0.0)), toneMappingMode, toneMappingExposure);
            vec3 outSrgb = resolveWorldSceneColor(mapped);
            float mainOutA = outA;
            if (uDualSourceBlendEnabled > 0.5) {
                mainOutA = floor(clamp(outA, 0.0, 1.0) * 63.0 + 0.5) / 63.0;
            }
            FragColor = vec4(outSrgb, mainOutA);
            FragBlendAlpha = (uDualSourceBlendEnabled > 0.5)
                ? vec4(0.0, 0.0, 0.0, clamp(outA, 0.0, 1.0))
                : vec4(0.0);
