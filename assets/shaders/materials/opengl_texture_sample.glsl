        vec4 sampleTextureWithWrap(sampler2D tex, vec2 uv, vec2 uvDx, vec2 uvDy) {
            float lodScale = exp2(litTextureDetailLodBias());
            return textureGrad(tex, uv, uvDx * lodScale, uvDy * lodScale);
        }
