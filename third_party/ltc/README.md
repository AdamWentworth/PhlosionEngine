# Linearly Transformed Cosines

Real-Time Polygonal-Light Shading with Linearly Transformed Cosines.
Eric Heitz, Jonathan Dupuy, Stephen Hill and David Neubelt.
ACM Transactions on Graphics (Proceedings of ACM SIGGRAPH 2016) 35(4), 2016.
https://eheitzresearch.wordpress.com/415-2/

Source: https://github.com/selfshadow/ltc_code
Revision: 31e5e96b54f98f33098f8503003119ba2231a1c6
License: LICENSE in this directory (retain with distributions).

AreaLightLut.h contains fit/results/ltc.js rounded to IEEE binary16, without
resampling. Its two 64x64 RGBA tables are packed horizontally, matrix on the
left and BRDF magnitude/Fresnel on the right. Source SHA256: 21071160163defd419b8f754ab604a8e60c44fcf539bfff9916e0e8e82316c1d.
The edge integral in world_lighting.glsl adapts the paper's reference code;
horizon clipping and portable vector transforms are implemented locally.
