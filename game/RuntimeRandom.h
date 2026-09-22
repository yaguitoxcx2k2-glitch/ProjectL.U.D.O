#pragma once
#include <QtGlobal>

namespace game {
/// Small deterministic PRNG with serializable state (xorshift64*).
class RuntimeRandom {
public:
    explicit RuntimeRandom(quint64 seed = 0x4c55444fULL) { reseed(seed); }
    void reseed(quint64 seed) { m_state = seed ? seed : 0x4c55444fULL; }
    quint64 state() const { return m_state; }
    void setState(quint64 state) { reseed(state); }
    quint64 next64();
    int bounded(int upperExclusive);
    int bounded(int lowerInclusive, int upperExclusive);
private:
    quint64 m_state = 0x4c55444fULL;
};
}
