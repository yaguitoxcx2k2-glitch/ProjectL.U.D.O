"""Source-level guardrails for the unified LUDO/Team/RPG Maker architecture."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Protocol9Architecture(unittest.TestCase):
    def text(self, relative):
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_client_has_one_protocol_and_no_legacy_write_routes(self):
        client = self.text("ui/CollaborationClient.cpp")
        self.assertIn("serverProtocol!=9", client)
        for route in ('/lease', '/release', 'commit-delta', 'projects/"+project+"/commit'):
            self.assertNotIn(route, client)

    def test_server_does_not_expose_legacy_write_routes(self):
        server = self.text("server/ludo_server.py")
        self.assertIn("TEAM_PROTOCOL = 9", server)
        for action in ("action == 'lease'", "action == 'release'", "action == 'commit'", "action == 'commit-delta'"):
            self.assertNotIn(action, server)

    def test_team_state_and_connection_are_separate_components(self):
        state = self.text("ui/TeamSyncState.h")
        connection = self.text("ui/TeamConnection.h")
        for field in ("localChangesPending", "operationsInFlight", "structureDirty",
                      "roomConflict", "resourceConflict", "reconnectRequired"):
            self.assertIn(field, state)
        self.assertIn("class TeamConnection", connection)

    def test_home_owns_startup_routing(self):
        startup = self.text("main.cpp")
        window = self.text("ui/MainWindow.cpp")
        self.assertNotIn("ProjectManagerDialog manager(core::editor())", startup)
        self.assertIn("if(ed.projectPath.isEmpty())openProjectManager()", window)
        self.assertIn("openRpgMakerProject", window)

    def test_rpg_maker_destination_is_project_authority(self):
        publication = self.text("ui/RpgMakerPublication.cpp")
        self.assertIn("QString root=ed.rpgMakerProjectRoot", publication)
        self.assertNotIn("/publication/", publication)
        self.assertNotIn("Trocar pasta", publication)

    def test_rpg_maker_plugins_and_components_have_one_authority(self):
        for stale in (
            "RPG_Maker_MV/LudoMapSystem.js", "RPG_Maker_MZ/LudoMapSystem.js",
            "RPG_Maker_MZ/LudoReflectionSystem.js", "ui/LudoMzExporter.h",
            "ui/MzMapVisualImport.h", "ui/MzProjectSync.cpp", "ui/MzProjectSync.h",
            "ui/MzPublication.cpp", "ui/MzPublication.h",
        ):
            self.assertFalse((ROOT / stale).exists(), stale)
        cmake = self.text("CMakeLists.txt")
        self.assertIn('set(LUDO_RPG_MAKER_INTEGRATIONS_DIR', cmake)
        self.assertNotIn("LUDO_DEPRECATED_PLUGIN_COPIES", cmake)

    def test_save_is_one_pipeline(self):
        window = self.text("ui/MainWindow.cpp")
        start = window.index("bool MainWindow::saveCurrentMap()")
        body = window[start:window.index("bool MainWindow::maybeSave()", start)]
        self.assertLess(body.index("saveProject(false)"), body.index("m_collaboration->synchronize()"))
        self.assertLess(body.index("m_collaboration->synchronize()"), body.index("saveAllDirtyMaps()"))

    def test_main_window_declares_universal_asset_picker_dependency(self):
        window = self.text("ui/MainWindow.cpp")
        self.assertIn('#include "UniversalAssetPicker.h"', window)
        self.assertIn("UniversalAssetPickerDialog::chooseOne", window)

    def test_editor_theme_never_inherits_windows_light_palette(self):
        theme = self.text("ui/EditorTheme.cpp")
        self.assertIn("setColorScheme(Qt::ColorScheme::Dark)", theme)
        self.assertIn('QStyleFactory::create(QStringLiteral("Fusion"))', theme)
        self.assertIn("app.setPalette(palette)", theme)
        self.assertIn('return QStringLiteral("dark")', theme)

    def test_home_deduplicates_projects_by_identity(self):
        manager = self.text("ui/ProjectManagerDialog.cpp")
        dashboard = self.text("ui/ProjectDashboard.cpp")
        self.assertIn("meta.projectId", manager)
        self.assertIn("projectPathForIdentity", manager)
        self.assertIn('value(QStringLiteral("projectId"))', dashboard)
        self.assertIn('value(QStringLiteral("rpgMakerProjectRoot"))', dashboard)

    def test_stale_room_structure_is_rebased_without_overwriting_local_map(self):
        client = self.text("ui/CollaborationClient.cpp")
        self.assertIn("roomRebaseMaps.contains(mapId)", client)
        self.assertIn("upsertBaseMap(serverBase,mapObject)", client)
        self.assertIn('op.remove("baseSeq")', client)
        self.assertIn("pendingRoomResets.insert(mapId)", client)

    def test_reference_export_contains_real_map_panorama(self):
        exporter = self.text("ui/RpgMakerExporter.h")
        start = exporter.index("inline bool renderReferenceParallax")
        body = exporter[start:exporter.index("inline QPainterPath collisionMaskPath", start)]
        self.assertIn("core::drawMapPanorama(painter,doc.map)", body)

    def test_image_transform_and_clipping_share_core_geometry(self):
        renderer = self.text("core/Renderer.cpp")
        view = self.text("ui/MapView.cpp")
        project = self.text("core/ProjectIO.cpp")
        self.assertIn("QTransform imageLayerTransform", renderer)
        self.assertIn("core::imageLayerTransform(layer)", view)
        self.assertIn("qAlpha(row[x])*maskAlpha>0", renderer)
        self.assertIn('o["children"] = ch', project)

    def test_multilayer_parallax_keeps_mv_base_and_adds_mz_visual_layers(self):
        model = self.text("core/Model.h")
        project = self.text("core/ProjectIO.cpp")
        exporter = self.text("ui/RpgMakerExporter.h")
        self.assertIn("parallaxFactorX", model)
        self.assertIn('o["parallaxLayer"] = true', project)
        self.assertIn('o.value("parallaxFactorX")', project)
        self.assertIn("exportParallaxLayers", exporter)
        self.assertIn('{"repeatX",repeatX}', exporter)
        for plugin in ("integrations/rpg-maker-mz/LudoMapSystem.js",
                       "integrations/rpg-maker-mv/LudoMapSystem.js"):
            runtime = self.text(plugin)
            self.assertIn("parallaxLayers", runtime)
            self.assertIn("parallaxOrder", runtime)
            self.assertIn("updateParallaxSprite", runtime)
            self.assertIn("item.repeatX", runtime)
        mz = self.text("integrations/rpg-maker-mz/LudoMapSystem.js")
        mv = self.text("integrations/rpg-maker-mv/LudoMapSystem.js")
        for capability in ("PARALLAX_EFFECT_FRAGMENT", "parallaxFrameIndex",
                           "oscillationSpeed", "effectStrength"):
            self.assertIn(capability, mz)
            self.assertNotIn(capability, mv)
        self.assertIn("editor.rpgMakerEngine!=core::RpgMakerEngine::MZ", exporter)
        self.assertIn("visualLayerFrameSourceRect", self.text("core/Renderer.cpp"))

    def test_layer_multi_selection_is_one_editor_state(self):
        editor = self.text("core/Editor.cpp")
        panel = self.text("ui/LayerPanel.cpp")
        view = self.text("ui/MapView.cpp")
        self.assertIn("setSelectedLayerIds", editor)
        self.assertIn("ExtendedSelection", panel)
        self.assertIn("m_imageDragStartOffsets", view)
        self.assertIn("Mover camadas", view)

    def test_reflection_layer_is_mz_only_and_exports_alpha_masks(self):
        panel = self.text("ui/LayerPanel.cpp")
        project = self.text("core/ProjectIO.cpp")
        exporter = self.text("ui/RpgMakerExporter.h")
        mv = self.text("integrations/rpg-maker-mv/LudoMapSystem.js")
        self.assertIn("RpgMakerEngine::MZ", panel)
        self.assertIn('o["reflectionLayer"] = true', project)
        self.assertIn('o.value("reflectionPreset")', project)
        self.assertIn("exportAuthoredReflectionLayers", exporter)
        self.assertNotIn("reflectionLayer", mv)

    def test_image_inspector_uses_engine_aware_collapsible_sections(self):
        properties = self.text("ui/PropertiesPanel.cpp")
        for title in ("Transformar", "Parallax e profundidade", "Movimento",
                      "Animação", "Efeitos", "Reflexo"):
            self.assertIn(f'tr("{title}")', properties)
        self.assertIn("class CollapsibleSection final", properties)
        self.assertIn("QSettings().setValue", properties)
        self.assertIn("ed.rpgMakerEngine == RpgMakerEngine::MZ &&", properties)
        self.assertNotIn("somente MZ", properties)
        self.assertNotIn("Camada de Reflexo (MZ)", self.text("ui/LayerPanel.cpp"))

    def test_every_inspector_page_uses_the_same_collapsible_language(self):
        properties = self.text("ui/PropertiesPanel.cpp")
        for key in ("Inspector/Map/GeneralExpanded", "Inspector/Layer/GeneralExpanded",
                    "Inspector/Tileset/GeneralExpanded", "Inspector/Object/TransformExpanded",
                    "Inspector/Random/PoolExpanded", "Inspector/Patterns/LibraryExpanded"):
            self.assertIn(key, properties)
        self.assertNotIn("new QGroupBox", properties)


if __name__ == "__main__":
    unittest.main(verbosity=2)
