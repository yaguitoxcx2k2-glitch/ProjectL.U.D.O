// ============================================================================
// Weather.h — estado climático compartilhado pelo editor e pelo runtime.
//
// Bloco 2 (LUDO 4.0): Propriedades do Mapa e o comando "Alterar clima"
// trabalham com o MESMO tipo normalizado. O runtime acrescenta apenas o tempo
// vivo e a origem (mapa/override) no próprio WeatherState; não existe uma
// segunda representação paralela de tipo/intensidade/trovão.
// ============================================================================
#pragma once

#include <QString>

namespace core {

enum class WeatherKind : unsigned char {
    None = 0,
    Rain,
    Snow,
    Storm
};

WeatherKind weatherKindFromString(QString type);
QString weatherKindId(WeatherKind kind);

struct WeatherState {
    WeatherKind kind = WeatherKind::None;
    int intensity = 0;             ///< 0..100; clima ativo nunca fica em 0
    QString thunderSePath;
    int thunderVolume = 90;        ///< 0..100

    // Estado vivo. Em MapEnvironment estes campos ficam nos defaults; a mesma
    // estrutura é copiada/aplicada pelo runtime para eliminar schemas paralelos.
    double elapsed = 0.0;          ///< segundos desde a última troca/reinício
    bool runtimeOverride = false;  ///< comando/save domina o default do mapa

    bool active() const { return kind != WeatherKind::None && intensity > 0; }
    QString typeId() const { return weatherKindId(kind); }

    /// Normaliza ids antigos/traduzidos, intensidade e volume. Se a configuração
    /// visual muda, a fase reinicia. `overrideMap` só informa a origem do estado.
    void setConfig(QString type, int requestedIntensity,
                   const QString& thunderSe = QString(), int requestedThunderVolume = 90,
                   bool overrideMap = false);

    /// Aplica outra configuração WeatherState usando a mesma normalização.
    /// `restartPhase` é usado ao trocar de mapa: mesmo dois mapas com chuva igual
    /// possuem ciclos climáticos independentes. Teleporte no mesmo mapa não usa
    /// esta opção e preserva a fase/override existente.
    void applyConfig(const WeatherState& config, bool overrideMap, bool restartPhase = false);

    /// Compara somente os campos persistentes/visuais, ignorando tempo e origem.
    bool sameConfig(const WeatherState& other) const;
};

} // namespace core
