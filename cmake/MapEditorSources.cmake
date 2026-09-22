# ============================================================================
# LUDO Map Editor — source manifest
#
# This manifest is intentionally explicit for the desktop UI.  It prevents
# legacy gameplay/runtime dialogs from silently returning to the build just
# because a .cpp file still exists in the source tree.
# ============================================================================

# Fase 4: o core deixou de usar GLOB. Somente arquivos explicitamente
# aprovados entram no executável. Isso impede que restos da antiga Game Engine
# voltem ao build apenas porque ainda existem no repositório.
set(LUDO_MAP_AUTHORING_SOURCES
    core/AssetDatabase.cpp
    core/AssetImportPipeline.cpp
    core/AssetPickerCatalog.cpp
    core/AssetWorkflow.cpp
    core/AssetWorkflowMetadata.cpp
    core/AutotileTopology.cpp
    core/Editor.cpp
    core/EditorHistory.cpp
    core/LayerTree.cpp
    core/LayerRasterFilters.cpp
    core/LayerRasterFilters.h
    core/MapWorkflow.cpp
    core/MapWorkspace.cpp
    core/Model.cpp
    core/PaintOps.cpp
    core/Renderer.cpp
    core/ResourceManager.cpp
    core/TilesetCatalog.cpp
    core/TilesetOps.cpp
    core/Wang.cpp
    core/commands/MapCommands.cpp
    core/services/MapService.cpp
)

set(LUDO_MAP_PROJECT_SOURCES
    core/ProjectBootstrap.cpp
    core/ProjectDependencyIndex.cpp
    core/ProjectHealth.cpp
    core/ProjectIO.cpp
    core/ProjectReferenceIndex.cpp
    core/ProjectValidator.cpp
    core/io/MapExportIO.cpp
    core/io/MapProjectSave.cpp
    core/io/MapProjectPayload.cpp
    core/io/ProjectImageCodec.cpp
    core/serialization/AtomicProjectFile.cpp
    core/serialization/ProjectHeaderSerializer.cpp
    core/serialization/ProjectSchema.cpp
    core/pure/FileSystem.cpp
    core/pure/ImageBuffer.cpp
    core/pure/JsonValue.cpp
    core/pure/Value.cpp
)

# Fase D: compatibilidade histórica possui uma fronteira explícita.
# ProjectMigration orquestra migrações do Map Editor e o importador legado
# extrai somente autoria de mapas de antigos LudoEngineProject.
set(LUDO_MAP_MIGRATION_SOURCES
    core/ProjectMigration.cpp
    core/ProjectMigration.h
    core/legacy/LegacyProjectImporter.cpp
)

set(LUDO_MAP_CORE_SOURCES
    ${LUDO_MAP_AUTHORING_SOURCES}
    ${LUDO_MAP_PROJECT_SOURCES}
    ${LUDO_MAP_MIGRATION_SOURCES}
)

set(LUDO_MAP_UI_SOURCES
    ui/RpgMakerPublication.cpp
    ui/RpgMakerPublication.h
    ui/CollaborationClient.cpp
    ui/CollaborationClient.h
    ui/TeamServerManager.cpp
    ui/TeamServerManager.h
    ui/AssetBrowser.cpp
    ui/AssetBrowser.h
    ui/AutotilePaletteWidget.cpp
    ui/AutotilePaletteWidget.h
    ui/EditorActionPalette.cpp
    ui/EditorActionPalette.h
    ui/EditorDialogGeometry.cpp
    ui/EditorDialogGeometry.h
    ui/EditorSessionStore.cpp
    ui/EditorSessionStore.h
    ui/EditorShortcutRegistry.cpp
    ui/EditorShortcutRegistry.h
    ui/EditorTheme.cpp
    ui/EditorTheme.h
    ui/EditorUiPreferences.cpp
    ui/EditorUiPreferences.h
    ui/EditorUiPrimitives.cpp
    ui/EditorUiPrimitives.h
    ui/Icons.cpp
    ui/Icons.h
    ui/LayerPanel.cpp
    ui/LayerPanel.h
    ui/LayerFiltersDialog.cpp
    ui/LayerFiltersDialog.h
    ui/RpgMakerExporter.h
    ui/MainWindow.cpp
    ui/MainWindowActions.cpp
    ui/MainWindow.h
    ui/MapAuthoringDialogs.cpp
    ui/MapAuthoringDialogs.h
    ui/MapView.cpp
    ui/MapViewEditCommands.cpp
    ui/MapView.h
    ui/Minimap.cpp
    ui/Minimap.h
    ui/RpgMakerProjectSync.cpp
    ui/RpgMakerProjectSync.h
    ui/ModuleManagerDialog.cpp
    ui/ModuleManagerDialog.h
    ui/NavigationHistory.cpp
    ui/NavigationHistory.h
    ui/PreferencesDialog.cpp
    ui/PreferencesDialog.h
    ui/ProjectCreationDialog.cpp
    ui/ProjectCreationDialog.h
    ui/ProjectCreationWorkflow.cpp
    ui/ProjectCreationWorkflow.h
    ui/ProjectDashboard.cpp
    ui/ProjectDashboard.h
    ui/ProjectManagerDialog.cpp
    ui/ProjectManagerDialog.h
    ui/ProjectOpenWorkflow.cpp
    ui/ProjectOpenWorkflow.h
    ui/ProjectRecoveryManager.cpp
    ui/ProjectRecoveryManager.h
    ui/PropertiesPanel.cpp
    ui/PropertiesPanel.h
    ui/RecoveryDecisionDialog.cpp
    ui/RecoveryDecisionDialog.h
    ui/TilesetManagerDialog.cpp
    ui/TilesetManagerFlows.cpp
    ui/TilesetManagerDialog.h
    ui/TilesetSourceWatcher.cpp
    ui/TeamSyncState.h
    ui/TeamConnection.cpp
    ui/TeamConnection.h
    ui/TeamRasterTransport.cpp
    ui/TeamRasterTransport.h
    ui/AssetSourceWatcher.cpp
    ui/AssetSourceWatcher.h
    ui/TilesetSourceWatcher.h
    ui/TilesetView.cpp
    ui/TilesetView.h
    ui/UniversalAssetPicker.cpp
    ui/UniversalAssetPicker.h
    ui/WangEditorWidget.cpp
    ui/WangEditorWidget.h
    ui/WorkspaceProfiles.cpp
    ui/WorkspaceProfiles.h
    ui/maps/ResizeMapDialog.cpp
    ui/maps/ResizeMapDialog.h
)

set(LUDO_MAP_MODULE_SOURCES
    modules/EditorModuleRegistry.cpp
    modules/EditorModuleRegistry.h
)

set(LUDO_MAP_PLATFORM_SOURCES
    platform/qt/CoreQtAdapters.cpp
    platform/qt/CoreQtAdapters.h
)

# Tombstones da antiga Game Engine. Os arquivos foram removidos fisicamente
# na Fase A, mas os nomes permanecem aqui como guardrail: se algum source com
# o mesmo papel voltar ao manifest ativo por engano, a configuração deve falhar.
set(LUDO_FORBIDDEN_DESKTOP_SOURCES
    core/RuntimePreloadCache.cpp
    core/RuntimeProject.cpp
    core/RuntimeProjectSnapshot.cpp
    core/RuntimeDistribution.cpp
    core/DerivedDataCache.cpp
    core/SecureAssetPackage.cpp
    core/ProductReadiness.cpp
    core/UiThemePackage.cpp
    core/CommonEventPackage.cpp
    core/DialogueHistory.cpp
    core/EventDiagnostics.cpp
    core/MoveRoutePresetStore.cpp
    core/PictureState.cpp
    core/documents/EditorDocument.cpp
    core/project/ProjectModel.cpp
    core/project/ProjectReferenceGraph.cpp
    core/services/EventService.cpp
    core/services/ProjectService.cpp
    core/Quantize.cpp
    core/ZipWriter.cpp
    core/legacy/LegacyProjectCompat.cpp
    core/CommandRegistry.cpp
    core/CommandWorkflow.cpp
    core/ConditionTree.cpp
    core/CustomDatabase.cpp
    core/Database.cpp
    core/DialogueContent.cpp
    core/EventCommandCodec.cpp
    core/EventCommandValidator.cpp
    core/EventExecutionContext.cpp
    core/EventModel.cpp
    core/SpriteSheetLayout.cpp
    core/ExpressionEvaluator.cpp
    core/FilterSystem.cpp
    core/Fog.cpp
    core/FrameSequence.cpp
    core/GameData.cpp
    core/GameValueRegistry.cpp
    core/IconSet.cpp
    core/InputMap.cpp
    core/Localization.cpp
    core/LudoCommandSystem.cpp
    core/NoCodePlugin.cpp
    core/Picture.cpp
    core/PluginCompatibility.cpp
    core/SubtitleStyle.cpp
    core/TextEffects.cpp
    core/VisualEffects.cpp
    core/Weather.cpp
    core/commands/EditorCommand.cpp
)
foreach(_forbidden IN LISTS LUDO_FORBIDDEN_DESKTOP_SOURCES)
    list(FIND LUDO_MAP_CORE_SOURCES "${_forbidden}" _forbidden_index)
    if(NOT _forbidden_index EQUAL -1)
        message(FATAL_ERROR "Legacy Game Engine source reintroduced in Map Editor: ${_forbidden}")
    endif()
endforeach()

# Guardrail: game/ and player/ are deliberately absent.  Keep this check so a
# future edit cannot accidentally reintroduce the old runtime into the editor.
foreach(_src IN LISTS LUDO_MAP_CORE_SOURCES LUDO_MAP_UI_SOURCES LUDO_MAP_MODULE_SOURCES LUDO_MAP_PLATFORM_SOURCES)
    if(_src MATCHES "(^|/)(game|player)/")
        message(FATAL_ERROR "Legacy runtime source leaked into LUDO Map Editor build: ${_src}")
    endif()
endforeach()
