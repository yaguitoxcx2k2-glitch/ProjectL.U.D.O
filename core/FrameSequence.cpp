#include "FrameSequence.h"

#include <QtGlobal>
#include <cmath>
#include <limits>

namespace core {

int FrameSequence::frameAt(double ageSeconds) const
{
    const int safeCount = qMax(1, count);
    int frame = qBound(0, firstFrame, safeCount - 1);
    if (!dynamic()) return frame;

    const int advance = int(std::floor(qMax(0.0, ageSeconds) * fps));
    return loop ? (frame + advance) % safeCount
                : qMin(safeCount - 1, frame + advance);
}

FrameSequenceValidation FrameSequence::validate(const QSize& imageSize) const
{
    FrameSequenceValidation out;
    const int safeColumns = qMax(1, columns);
    const int safeRows = qMax(1, rows);
    const qint64 cap64 = qint64(safeColumns) * qint64(safeRows);
    out.capacity = int(qMin<qint64>(cap64, std::numeric_limits<int>::max()));

    // Uma Picture legada com count > 1 usa a grade mesmo quando o antigo
    // booleano animated não veio gravado. A validação acompanha o runtime.
    const bool usesFrames = enabled || count > 1;
    if (!usesFrames) {
        out.frameSize = imageSize;
        return out;
    }
    if (imageSize.isEmpty()) {
        out.issue = FrameSequenceIssue::ImageUnavailable;
        return out;
    }
    if (qMax(1, count) > out.capacity) {
        out.issue = FrameSequenceIssue::CountExceedsGrid;
        return out;
    }
    if (safeColumns > imageSize.width() || safeRows > imageSize.height()) {
        out.issue = FrameSequenceIssue::GridExceedsImage;
        return out;
    }
    if ((imageSize.width() % safeColumns) != 0 || (imageSize.height() % safeRows) != 0) {
        out.issue = FrameSequenceIssue::ImageNotDivisible;
        return out;
    }
    out.frameSize = QSize(imageSize.width() / safeColumns, imageSize.height() / safeRows);
    return out;
}

QRect FrameSequence::rectAt(const QSize& imageSize, double ageSeconds) const
{
    if (imageSize.isEmpty()) return QRect();
    const int safeColumns = qMax(1, columns);
    const int safeRows = qMax(1, rows);
    const int fw = qMax(1, imageSize.width() / safeColumns);
    const int fh = qMax(1, imageSize.height() / safeRows);
    const int frame = frameAt(ageSeconds);
    const int col = frame % safeColumns;
    const int row = frame / safeColumns;
    return QRect(col * fw, row * fh, fw, fh)
        .intersected(QRect(QPoint(0, 0), imageSize));
}

} // namespace core
