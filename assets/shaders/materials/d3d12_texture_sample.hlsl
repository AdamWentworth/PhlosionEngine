float4 sampleTextureWithWrap(Texture2D tex,
                             float2 uv,
                             float2 uvDx,
                             float2 uvDy,
                             float wrapS,
                             float wrapT) {
  const float lodScale = exp2(litTextureDetailLodBias());
  uvDx *= lodScale;
  uvDy *= lodScale;
  uint width, height, levels;
  tex.GetDimensions(0, width, height, levels);
  // Match the shared engine's bilinear footprint for authored base-only maps.
  if (levels == 1u) { uvDx = 0.0f.xx; uvDy = 0.0f.xx; }
  bool sClamp = isClampWrap(wrapS);
  bool tClamp = isClampWrap(wrapT);
  bool sMirror = isMirrorWrap(wrapS);
  bool tMirror = isMirrorWrap(wrapT);

  if (sClamp && tClamp) return tex.SampleGrad(gSampCC, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && !tClamp && !tMirror) return tex.SampleGrad(gSampRR, uv, uvDx, uvDy);
  if (sClamp && !tClamp && !tMirror) return tex.SampleGrad(gSampCR, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && tClamp) return tex.SampleGrad(gSampRC, uv, uvDx, uvDy);
  if (sMirror && !tClamp && !tMirror) return tex.SampleGrad(gSampMR, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && tMirror) return tex.SampleGrad(gSampRM, uv, uvDx, uvDy);
  if (sMirror && tMirror) return tex.SampleGrad(gSampMM, uv, uvDx, uvDy);
  if (sClamp && tMirror) return tex.SampleGrad(gSampCM, uv, uvDx, uvDy);
  if (sMirror && tClamp) return tex.SampleGrad(gSampMC, uv, uvDx, uvDy);
  return tex.SampleGrad(gSampRR, uv, uvDx, uvDy);
}
float4 sampleWorldTextureWithWrap(float2 uv, float2 uvDx, float2 uvDy) {
  return sampleTextureWithWrap(gTex, uv, uvDx, uvDy, uWrapS, uWrapT);
}
