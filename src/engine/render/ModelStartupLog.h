// src/engine/render/ModelStartupLog.h
#pragma once

#include "engine/core/Log.h"

#ifndef PAC_VERBOSE_STARTUP
#define PAC_VERBOSE_STARTUP 0
#endif

// Verbose, opt-in startup logging (INFO)
#if PAC_VERBOSE_STARTUP
    #define STARTUP_LOG(msg) do { ::engine::log::info((msg)); } while(0)
#else
    #define STARTUP_LOG(msg) do {} while(0)
#endif

// Warnings that should be visible even when verbose startup logs are disabled (WARN)
#define STARTUP_WARN(msg) do { ::engine::log::warn((msg)); } while(0)

