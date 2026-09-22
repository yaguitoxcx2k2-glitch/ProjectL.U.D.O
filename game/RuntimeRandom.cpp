#include "RuntimeRandom.h"
namespace game {
quint64 RuntimeRandom::next64() {
    quint64 x = m_state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    m_state = x;
    return x * 2685821657736338717ULL;
}
int RuntimeRandom::bounded(int upperExclusive) {
    if (upperExclusive <= 1) return 0;
    return int(next64() % quint64(upperExclusive));
}
int RuntimeRandom::bounded(int lowerInclusive, int upperExclusive) {
    return lowerInclusive + bounded(upperExclusive - lowerInclusive);
}
}
