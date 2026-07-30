#include "ModelFastGltfSceneData.h"

#include <glm/gtc/type_ptr.hpp>

#include <type_traits>

#include "ModelFastGltfLoaderHelpers.h"

namespace {

template <typename T, typename = void>
struct fg_has_has_value : std::false_type {};

template <typename T>
struct fg_has_has_value<T, std::void_t<decltype(std::declval<const T&>().has_value())>>
    : std::true_type {};

template <typename T, typename = void>
struct fg_has_value_fn : std::false_type {};

template <typename T>
struct fg_has_value_fn<T, std::void_t<decltype(std::declval<const T&>().value())>>
    : std::true_type {};

template <typename T, typename = void>
struct fg_has_get : std::false_type {};

template <typename T>
struct fg_has_get<T, std::void_t<decltype(std::declval<const T&>().get())>>
    : std::true_type {};

template <typename T, typename = void>
struct fg_has_value_member : std::false_type {};

template <typename T>
struct fg_has_value_member<T, std::void_t<decltype((std::declval<const T&>().value))>>
    : std::true_type {};

template <typename T, typename = void>
struct fg_is_deref : std::false_type {};

template <typename T>
struct fg_is_deref<T, std::void_t<decltype(*std::declval<const T&>())>>
    : std::true_type {};

template <typename Opt>
bool fgOptHas(const Opt& o) {
    if constexpr (std::is_integral_v<std::decay_t<Opt>> || std::is_enum_v<std::decay_t<Opt>>) {
        return true;
    } else if constexpr (fg_has_has_value<Opt>::value) {
        return o.has_value();
    } else {
        return static_cast<bool>(o);
    }
}

template <typename Opt>
std::size_t fgOptGet(const Opt& o) {
    if constexpr (std::is_integral_v<std::decay_t<Opt>> || std::is_enum_v<std::decay_t<Opt>>) {
        return static_cast<std::size_t>(o);
    } else if constexpr (fg_has_get<Opt>::value) {
        return static_cast<std::size_t>(o.get());
    } else if constexpr (fg_has_value_fn<Opt>::value) {
        return static_cast<std::size_t>(o.value());
    } else if constexpr (fg_has_value_member<Opt>::value) {
        return static_cast<std::size_t>(o.value);
    } else if constexpr (fg_is_deref<Opt>::value) {
        return static_cast<std::size_t>(*o);
    } else {
        static_assert(!sizeof(Opt), "fgOptGet: unsupported optional type");
    }
}

}  // namespace

namespace pac::model_fastgltf {

void buildSceneData(const fastgltf::Asset& asset,
                    fastgltf::DefaultBufferDataAdapter& adapter,
                    std::vector<pac_model_types::NodeTRS>& outNodesDefault,
                    std::vector<std::string>* outNodeNames,
                    std::vector<std::vector<int>>& outNodeChildren,
                    std::vector<int>& outNodeMesh,
                    std::vector<int>& outNodeSkin,
                    std::vector<int>& outSceneRoots,
                    std::vector<pac_model_types::SkinData>& outSkins,
                    std::vector<pac_model_types::AnimationClip>& outAnimations) {
    outNodesDefault.clear();
    outNodeChildren.clear();
    outNodeMesh.clear();
    outNodeSkin.clear();
    outSceneRoots.clear();
    outSkins.clear();
    outAnimations.clear();
    if (outNodeNames) outNodeNames->clear();

    // ---- Nodes + scene roots ----
    outNodesDefault.resize(asset.nodes.size());
    if (outNodeNames) outNodeNames->resize(asset.nodes.size());
    outNodeChildren.resize(asset.nodes.size());
    outNodeMesh.assign(asset.nodes.size(), -1);
    outNodeSkin.assign(asset.nodes.size(), -1);

    if (!asset.scenes.empty()) {
        size_t sceneIndex = 0;
        if (asset.defaultScene.has_value()) sceneIndex = asset.defaultScene.value();
        if (sceneIndex >= asset.scenes.size()) sceneIndex = 0;

        outSceneRoots.clear();
        for (auto n : asset.scenes[sceneIndex].nodeIndices) {
            outSceneRoots.push_back(static_cast<int>(n));
        }
    }

    for (size_t i = 0; i < asset.nodes.size(); ++i) {
        const auto& n = asset.nodes[i];
        if (outNodeNames) {
            (*outNodeNames)[i] = std::string(n.name.begin(), n.name.end());
        }

        outNodeChildren[i].clear();
        outNodeChildren[i].reserve(n.children.size());
        for (auto c : n.children) outNodeChildren[i].push_back(static_cast<int>(c));

        if (n.meshIndex.has_value()) outNodeMesh[i] = static_cast<int>(n.meshIndex.value());
        if (n.skinIndex.has_value()) outNodeSkin[i] = static_cast<int>(n.skinIndex.value());

        pac_model_types::NodeTRS trs;
        trs.hasMatrix = false;

        if (const auto* t = std::get_if<fastgltf::TRS>(&n.transform)) {
            trs.t = glm::vec3(t->translation[0], t->translation[1], t->translation[2]);
            trs.r = glm::normalize(glm::quat(t->rotation[3], t->rotation[0], t->rotation[1], t->rotation[2]));
            trs.s = glm::vec3(t->scale[0], t->scale[1], t->scale[2]);
        } else if (const auto* m = std::get_if<fastgltf::math::fmat4x4>(&n.transform)) {
            trs.hasMatrix = true;
            trs.matrix = glm::make_mat4(m->data());
        }

        outNodesDefault[i] = trs;
    }

    // ---- Skins ----
    outSkins.resize(asset.skins.size());
    for (size_t si = 0; si < asset.skins.size(); ++si) {
        const auto& s = asset.skins[si];
        pac_model_types::SkinData out;
        out.joints.reserve(s.joints.size());
        for (auto j : s.joints) out.joints.push_back(static_cast<int>(j));

        if (s.inverseBindMatrices.has_value()) {
            const size_t accIndex = s.inverseBindMatrices.value();
            if (accIndex < asset.accessors.size()) {
                std::vector<glm::mat4> mats;
                readMat4(asset, asset.accessors[accIndex], mats, adapter);
                out.inverseBind = std::move(mats);
            }
        }

        if (out.inverseBind.size() != out.joints.size()) {
            out.inverseBind.assign(out.joints.size(), glm::mat4(1.0f));
        }

        outSkins[si] = std::move(out);
    }

    // ---- Animations ----
    outAnimations.reserve(asset.animations.size());
    for (const auto& anim : asset.animations) {
        pac_model_types::AnimationClip clip;
        clip.name = std::string(anim.name.begin(), anim.name.end());
        clip.durationSec = 0.0f;

        clip.samplers.resize(anim.samplers.size());

        for (size_t si = 0; si < anim.samplers.size(); ++si) {
            const auto& s = anim.samplers[si];
            pac_model_types::AnimationSampler samp;

            switch (s.interpolation) {
                case fastgltf::AnimationInterpolation::Step:
                    samp.interpolation = "STEP";
                    break;
                case fastgltf::AnimationInterpolation::CubicSpline:
                    samp.interpolation = "CUBICSPLINE";
                    break;
                case fastgltf::AnimationInterpolation::Linear:
                default:
                    samp.interpolation = "LINEAR";
                    break;
            }

            if (s.inputAccessor < asset.accessors.size()) {
                readScalarFloat(asset, asset.accessors[s.inputAccessor], samp.inputs, adapter);
                if (!samp.inputs.empty()) {
                    clip.durationSec = (std::max)(clip.durationSec, samp.inputs.back());
                }
            }

            if (s.outputAccessor < asset.accessors.size()) {
                const auto& outAcc = asset.accessors[s.outputAccessor];
                std::vector<glm::vec4> raw;

                if (outAcc.type == fastgltf::AccessorType::Vec3) {
                    readVec3AsVec4(asset, outAcc, raw, adapter);
                    samp.isVec4 = false;
                } else {
                    readVec4(asset, outAcc, raw, adapter);
                    samp.isVec4 = true;
                }

                if (samp.interpolation == "CUBICSPLINE" && !samp.inputs.empty()) {
                    const size_t keys = samp.inputs.size();
                    std::vector<glm::vec4> values;
                    values.reserve(keys);
                    for (size_t k = 0; k < keys; ++k) {
                        const size_t idx = k * 3 + 1;
                        if (idx < raw.size()) values.push_back(raw[idx]);
                    }
                    samp.outputs = std::move(values);
                } else {
                    samp.outputs = std::move(raw);
                }
            }

            clip.samplers[si] = std::move(samp);
        }

        clip.channels.reserve(anim.channels.size());
        for (const auto& ch : anim.channels) {
            if (!fgOptHas(ch.nodeIndex)) continue;
            if (!fgOptHas(ch.samplerIndex)) continue;

            pac_model_types::AnimationChannel c;
            c.targetNode = static_cast<int>(fgOptGet(ch.nodeIndex));
            c.samplerIndex = static_cast<int>(fgOptGet(ch.samplerIndex));

            switch (ch.path) {
                case fastgltf::AnimationPath::Translation:
                    c.path = pac_model_types::ChannelPath::Translation;
                    break;
                case fastgltf::AnimationPath::Rotation:
                    c.path = pac_model_types::ChannelPath::Rotation;
                    break;
                case fastgltf::AnimationPath::Scale:
                    c.path = pac_model_types::ChannelPath::Scale;
                    break;
                default:
                    continue;
            }

            clip.channels.push_back(c);
        }

        outAnimations.push_back(std::move(clip));
    }
}

}  // namespace pac::model_fastgltf
