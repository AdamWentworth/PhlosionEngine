    if (materialMode >= 1.5) {
        linearColor = applyReviewLightingProfile(
            linearColor,
            reviewAlbedo,
            vertexNormal,
            reviewCameraForward);
    }
    linearColor = environmentHaze(linearColor, worldPosition, worldView.cameraPosition.xyz);
    float toneMappingExposure = ENV_SUN.w > 0.5 ? ENV_AMBIENT.w : 1.15;
    vec3 mapped = tonemapACESFilmic(max(linearColor, vec3(0.0)), toneMappingExposure);
    vec3 resolvedColor = pushData.shadingParams.w > 0.5
        ? mapped
        : linearToSrgb(mapped);
    writeWorldColor(vec4(resolvedColor, alpha));
