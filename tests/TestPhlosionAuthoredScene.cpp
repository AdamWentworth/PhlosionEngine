#include "engine/assets/phlosion/PhlosionAuthoredScene.h"

#include <string>

bool test_phlosion_authored_scene_contract(std::string& outFail) {
    using namespace engine::assets::phlosion;
    AuthoredSceneDocument source{
        .sceneId = "routes/test",
        .baseEnvironmentAssetId = "environments/test",
        .coordinateSystem = "centimetres_xyz_y_up",
        .nodes = {
            AuthoredSceneNode{
                .id = "folder/environment",
                .displayName = "Environment"},
            AuthoredSceneNode{
                .id = "source/tree-1",
                .displayName = "Tree 1",
                .parentId = "folder/environment",
                .siblingOrder = 0u,
                .enabled = true,
                .reason = "board_clearance",
                .transform = AuthoredSceneTransform{
                    .translation = {1.0f, 2.0f, 3.0f}},
                .importedSource = ImportedSourceBinding{
                    .targetKind = "tree",
                    .logicalName = "tree_001",
                    .recordIndex = 4u,
                    .expectedSourceTransform =
                        AuthoredSceneTransform{
                            .translation = {1.0f, 2.0f, 0.0f}}}},
            AuthoredSceneNode{
                .id = "authored/tree-copy",
                .displayName = "Tree 1 Copy",
                .parentId = "folder/environment",
                .siblingOrder = 1u,
                .transform = AuthoredSceneTransform{
                    .translation = {4.0f, 2.0f, 3.0f}},
                .prefabInstance = PrefabInstanceBinding{
                    .prototypeNodeId = "source/tree-1",
                    .prefabAssetId = "route/test_tree",
                    .creationTransform = AuthoredSceneTransform{
                        .translation = {4.0f, 2.0f, 3.0f}}}},
            AuthoredSceneNode{
                .id = "terrain-tile/12/-7",
                .displayName = "Terrain Tile (12, -7)",
                .parentId = "folder/environment",
                .siblingOrder = 2u,
                .reason = "terrain_authoring",
                .terrainTile = TerrainTileBinding{
                    .tileSetAssetId = "route/test_tileset",
                    .gridX = 12,
                    .gridZ = -7,
                    .elevationLevel = 3,
                    .surface = "dark_lawn",
                    .shape = "ramp_north"}}}};
    std::string error;
    if (!validateAuthoredSceneDocument(source, &error)) {
        outFail = "valid document failed: " + error;
        return false;
    }
    const std::string encoded =
        serializeAuthoredSceneDocument(source);
    AuthoredSceneDocument decoded;
    if (!parseAuthoredSceneDocument(encoded, decoded, &error)) {
        outFail = "round trip failed: " + error;
        return false;
    }
    if (decoded.sceneId != source.sceneId ||
        decoded.nodes.size() != 4u ||
        !decoded.nodes[1].importedSource ||
        decoded.nodes[1].transform->translation[2] != 3.0f ||
        !decoded.nodes[2].prefabInstance ||
        decoded.nodes[2].prefabInstance->prototypeNodeId !=
            "source/tree-1" ||
        !decoded.nodes[3].terrainTile ||
        decoded.nodes[3].terrainTile->gridX != 12 ||
        decoded.nodes[3].terrainTile->shape != "ramp_north") {
        outFail = "document fields changed during round trip";
        return false;
    }
    decoded.nodes[1].parentId = "authored/tree-copy";
    if (validateAuthoredSceneDocument(decoded, nullptr)) {
        outFail = "document accepted a non-folder parent";
        return false;
    }
    const std::string schemaOne = R"json({
        "schema_version": 1,
        "kind": "phlosion_authored_scene",
        "scene_id": "routes/legacy",
        "base_environment_asset_id": "environments/legacy",
        "coordinate_system": "centimetres_xyz_y_up",
        "nodes": []
    })json";
    if (!parseAuthoredSceneDocument(schemaOne, decoded, &error) ||
        decoded.sceneId != "routes/legacy") {
        outFail = "schema-1 authored scene compatibility failed: " + error;
        return false;
    }
    return true;
}
