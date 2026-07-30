#include "engine/render/OpenGLRenderBackend.h"

#include <glad/glad.h>

void OpenGLRenderBackend::beginWorldIndexedBatchSubmission() {
    if (worldIndexedBatchSubmissionState_.active) {
        ++worldIndexedBatchSubmissionState_.depth;
        return;
    }

    auto& state = worldIndexedBatchSubmissionState_;
    state.active = true;
    state.depth = 1;
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.prevArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &state.prevElementArrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.prevActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.prevTexture2DOnActive);
    for (int unit = 0; unit < 8; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.prevTexture2DOnUnit[static_cast<std::size_t>(unit)]);
    }
    glActiveTexture(static_cast<GLenum>(state.prevActiveTexture));

    state.depthEnabled = (glIsEnabled(GL_DEPTH_TEST) == GL_TRUE);
    state.blendEnabled = (glIsEnabled(GL_BLEND) == GL_TRUE);
    state.cullEnabled = (glIsEnabled(GL_CULL_FACE) == GL_TRUE);
    glGetIntegerv(GL_FRONT_FACE, &state.prevFrontFace);
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    state.prevDepthMask = (prevDepthMask == GL_TRUE);
    glGetIntegerv(GL_DEPTH_FUNC, &state.prevDepthFunc);
    glGetIntegerv(GL_BLEND_SRC_RGB, &state.prevBlendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &state.prevBlendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.prevBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &state.prevBlendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.prevBlendEqRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state.prevBlendEqAlpha);
    state.currentProgram = state.prevProgram;
    state.currentVao = state.prevVao;
    state.currentArrayBuffer = state.prevArrayBuffer;
    state.currentElementArrayBuffer = state.prevElementArrayBuffer;
    state.currentActiveTexture = state.prevActiveTexture;
    state.currentTexture2DOnUnit = state.prevTexture2DOnUnit;
    state.currentDepthEnabled = state.depthEnabled;
    state.currentBlendEnabled = state.blendEnabled;
    state.currentCullEnabled = state.cullEnabled;
    state.currentFrontFace = state.prevFrontFace;
    state.currentDepthMask = state.prevDepthMask;
    state.currentDepthFunc = state.prevDepthFunc;
    state.currentBlendSrcRgb = state.prevBlendSrcRgb;
    state.currentBlendDstRgb = state.prevBlendDstRgb;
    state.currentBlendSrcAlpha = state.prevBlendSrcAlpha;
    state.currentBlendDstAlpha = state.prevBlendDstAlpha;
    state.currentBlendEqRgb = state.prevBlendEqRgb;
    state.currentBlendEqAlpha = state.prevBlendEqAlpha;
    state.worldProgramStaticUniformsApplied = false;
}

void OpenGLRenderBackend::endWorldIndexedBatchSubmission() {
    auto& state = worldIndexedBatchSubmissionState_;
    if (!state.active) return;
    if (state.depth > 1) {
        --state.depth;
        return;
    }

    glBindVertexArray(static_cast<GLuint>(state.prevVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.prevArrayBuffer));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(state.prevElementArrayBuffer));
    glUseProgram(static_cast<GLuint>(state.prevProgram));

    for (int unit = 0; unit < 8; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.prevTexture2DOnUnit[static_cast<std::size_t>(unit)]));
    }
    glActiveTexture(static_cast<GLenum>(state.prevActiveTexture));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.prevTexture2DOnActive));

    glDepthMask(state.prevDepthMask ? GL_TRUE : GL_FALSE);
    glDepthFunc(static_cast<GLenum>(state.prevDepthFunc));
    glBlendEquationSeparate(static_cast<GLenum>(state.prevBlendEqRgb), static_cast<GLenum>(state.prevBlendEqAlpha));
    glBlendFuncSeparate(static_cast<GLenum>(state.prevBlendSrcRgb),
                        static_cast<GLenum>(state.prevBlendDstRgb),
                        static_cast<GLenum>(state.prevBlendSrcAlpha),
                        static_cast<GLenum>(state.prevBlendDstAlpha));
    if (state.blendEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    glFrontFace(static_cast<GLenum>(state.prevFrontFace));
    if (state.cullEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    if (state.depthEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    state = WorldIndexedBatchSubmissionState{};
}
